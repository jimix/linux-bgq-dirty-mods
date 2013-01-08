/*                                                                  */
/* IBM Blue Gene/Q OFED Device Driver                               */
/*                                                                  */
/* Author: Andrew Tauferner <ataufer@us.ibm.com>                    */
/*                                                                  */
/* Licensed Materials - Property of IBM                             */
/*                                                                  */
/* Blue Gene/Q                                                      */
/*                                                                  */
/* (c) Copyright IBM Corp. 2011, 2012 All Rights Reserved           */
/*                                                                  */
/* US Government Users Restricted Rights - Use, duplication or      */
/* disclosure restricted by GSA ADP Schedule Contract with IBM      */
/* Corporation.                                                     */
/*                                                                  */
/* This software is available to you under the GNU General Public   */
/* License (GPL) version 2.                                         */
/*                                                                  */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/bitops.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/bgvrnic_user.h>

#include <mudm/include/mudm.h>

#include "bgvrnic.h"


extern struct device* bg_mu_dev;

extern Personality_t _bgpers;

static int bgvrnic_process_send_wqes(struct bgvrnic_qp* vrnic_qp);

struct bgvrnic_mr* bgvrnic_get_mr(struct bgvrnic_device* vrnic_dev,
					 u32 lkey)
{
        unsigned long flags;
	struct bgvrnic_mr* vrnic_mr;
ENTER
        spin_lock_irqsave(&vrnic_dev->mr_pool.lock, flags);
        vrnic_mr = idr_find(&vrnic_dev->mr_pool.idr, lkey);
        spin_unlock_irqrestore(&vrnic_dev->mr_pool.lock, flags);
EXIT
        return vrnic_mr;
}


/* The caller must be holding the rq_lock for the QP. */
int bgvrnic_process_recv_wqes(struct bgvrnic_qp* vrnic_qp)
{
	int rc = 0;
	struct bgvrnic_wqe* wqe;
	struct bgvrnic_wqe* tmp;

ENTER
	BUG_ON(!spin_is_locked(&vrnic_qp->rq_lock));

	list_for_each_entry_safe(wqe, tmp, &vrnic_qp->rq, list) {
		switch (wqe->state) {
			case BGVRNIC_WQES_DONE:
				rc = bgvrnic_add_cqe(vrnic_qp->rcq, vrnic_qp, NULL, &wqe->wr.ib_recv, wqe->length, wqe->status, wqe->imm_data_valid, wqe->imm_data);
				list_del(&wqe->list);
				bgvrnic_free_wqe(wqe);
			break;

			case BGVRNIC_WQES_INIT:
			case BGVRNIC_WQES_RUN:
				goto out;

			break;

			default:
				BUG();
		}				
	}

out:

EXIT
	return rc;
}



int mu_status_cb(void* requestID[],
	         u32 status[],
		 void* cb_context,
		 u32 error_return[],
		 u32 num_request_ids)
{
	int i;
	int rc = 0;

	for (i = 0; i < num_request_ids; i++) {
		struct bgvrnic_wqe* wqe = (struct bgvrnic_wqe*) requestID[i];
		struct bgvrnic_qp* vrnic_qp;

		if (!wqe) {
			Kernel_WriteFlightLog(FLIGHTLOG, FL_BUG_VERBS, __LINE__, num_request_ids, 0, 0);
			BUG();
		}	

		vrnic_qp = wqe->vrnic_qp;
		if (!vrnic_qp) {
			Kernel_WriteFlightLog(FLIGHTLOG, FL_BUG_VERBS, __LINE__, num_request_ids, (u64) wqe, 0);
			BUG();
		}

		switch (wqe->type) {
			case BGVRNIC_WQET_SEND:
				spin_lock(&vrnic_qp->sq_lock);
				Kernel_WriteFlightLog(FLIGHTLOG, FL_STAT_SEND, (u64) wqe, vrnic_qp->ib_qp.qp_num, status[i], 0); 
       				if (wqe->status < 0) 
					wqe->status = status[i];
       				wqe->state = BGVRNIC_WQES_DONE;
				smp_wmb();
				bgvrnic_process_send_wqes(vrnic_qp);
				spin_unlock(&vrnic_qp->sq_lock);
			break;

			case BGVRNIC_WQET_RECV:
				spin_lock(&vrnic_qp->rq_lock);
				Kernel_WriteFlightLog(FLIGHTLOG, FL_STAT_RECV, (u64) wqe, vrnic_qp->ib_qp.qp_num, status[i], 0);
				if (wqe->status < 0)
					wqe->status = status[i];
				wqe->state = BGVRNIC_WQES_DONE;
				smp_wmb();
				bgvrnic_process_recv_wqes(vrnic_qp);
				spin_unlock(&vrnic_qp->rq_lock);
			break;

			case BGVRNIC_WQET_RDMA:
				Kernel_WriteFlightLog(FLIGHTLOG, FL_STAT_RDMA, (u64) wqe, vrnic_qp->ib_qp.qp_num, status[i], 0);
				if (status[i])
					printk(KERN_EMERG "WQE %p failed, rc=%d\n", wqe, status[i]);
				bgvrnic_free_wqe(wqe);
			break;

			default:
				BUG();
		}

		error_return[i] = 0;
	}

EXIT	
	return rc;
}


static int bgvrnic_query_device(struct ib_device*      ib_dev,
				struct ib_device_attr* attrs)
{
	int rc = 0;
	struct bgvrnic_device* vrnic_dev = container_of(ib_dev, struct bgvrnic_device, ib_dev);

ENTER
	*attrs = vrnic_dev->ib_dev_attrs;
EXIT
	return rc;
}


static int bgvrnic_query_gid(struct ib_device* ib_dev,
			     u8		       port,
			     int	       index,
			     union ib_gid*     gid)
{
	int rc = 0;

ENTER
	memset(gid->raw, 0, sizeof(gid->raw));
	memcpy(gid->raw, &ib_dev->node_guid, min((size_t) 6, sizeof(gid->raw)));

EXIT
	return rc;
}



static int bgvrnic_query_port(struct ib_device* ib_dev,
			      u8 port,
			      struct ib_port_attr* attrs)
{
	struct bgvrnic_device* vrnic_dev = container_of(ib_dev, struct bgvrnic_device, ib_dev);
ENTER
	*attrs = vrnic_dev->ib_port_attrs[port-1];

EXIT
	return 0;
}


static int bgvrnic_query_pkey(struct ib_device* ib_dev,
			      u8		port,
			      u16 		index,
			      u16* 		key)
{
ENTER
	*key = 0;

EXIT
	return 0;
}


static struct ib_ucontext* bgvrnic_alloc_ucontext(struct ib_device* ib_dev,
						  struct ib_udata*  ib_data)
{
	struct ib_ucontext* ib_ucon = (struct ib_ucontext*) kzalloc(sizeof(*ib_ucon), GFP_KERNEL);

ENTER
	if (!ib_ucon)
		return ERR_PTR(-ENOMEM);

EXIT
	return ib_ucon;
}


static int bgvrnic_dealloc_ucontext(struct ib_ucontext* ib_ucon)
{
ENTER
	kfree((void*) ib_ucon);

EXIT
	return 0;
}	


static struct ib_pd* bgvrnic_alloc_pd(struct ib_device* ib_dev,
					struct ib_ucontext* context,
					struct ib_udata* udata)
{
	struct bgvrnic_pd* pd = NULL;

ENTER
	pd = (struct bgvrnic_pd*) kmalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	if (context) {
		if (ib_copy_to_udata(udata, &pd->pdn, sizeof(pd->pdn))) {
			kfree(pd);
			return ERR_PTR(-EFAULT);
		}
	}

EXIT
	return &pd->ib_pd; 
}


static int bgvrnic_dealloc_pd(struct ib_pd* ib_pd)
{
	struct bgvrnic_pd* vrnic_pd = container_of(ib_pd, struct bgvrnic_pd, ib_pd);
ENTER
	kfree(vrnic_pd);
EXIT
	return 0;
}


static struct ib_ah* bgvrnic_create_ah(struct ib_pd* ib_pd,
				struct ib_ah_attr* ib_ah_attr)
{
ENTER
EXIT
	return ERR_PTR(-ENOSYS);
}


static int bgvrnic_destroy_ah(struct ib_ah* ib_ah)
{
ENTER
EXIT
	return -ENOSYS;
}


static int bgvrnic_alloc_qpn(struct bgvrnic_device* vrnic_dev, 
			     struct bgvrnic_qp* vrnic_qp)
{
        int rc = 0;

        do {
                spin_lock_irq(&vrnic_dev->qp_pool.lock);
                rc = idr_get_new_above(&vrnic_dev->qp_pool.idr, vrnic_qp,
                                        vrnic_dev->qp_pool.prev_id++, &vrnic_qp->ib_qp.qp_num);
		spin_unlock_irq(&vrnic_dev->qp_pool.lock);
		if (rc == -EAGAIN) {
			if (idr_pre_get(&vrnic_dev->qp_pool.idr, GFP_KERNEL))
				continue;
			else
				break;
		}
        } while (rc == -EAGAIN);

        return rc;
}


static void bgvrnic_free_qpn(struct bgvrnic_device* vrnic_dev, 
			     int qpn)
{
        spin_lock_irq(&vrnic_dev->qp_pool.lock);
        idr_remove(&vrnic_dev->qp_pool.idr, qpn);
        spin_unlock_irq(&vrnic_dev->qp_pool.lock);

	return;
}


static int bgvrnic_alloc_lkey(struct bgvrnic_device* vrnic_dev,
                             struct bgvrnic_mr* vrnic_mr)
{
        int rc = 0;

        do {
                spin_lock_irq(&vrnic_dev->mr_pool.lock);
                rc = idr_get_new_above(&vrnic_dev->mr_pool.idr, vrnic_mr,
                                        vrnic_dev->mr_pool.prev_id++, &vrnic_mr->ib_mr.lkey);
                spin_unlock_irq(&vrnic_dev->mr_pool.lock);
                if (rc == -EAGAIN) {
                        if (idr_pre_get(&vrnic_dev->mr_pool.idr, GFP_KERNEL))
                                continue;
                        else
                                break;
                }
        } while (rc == -EAGAIN);

	if (!rc) 
		vrnic_mr->ib_mr.rkey = vrnic_mr->ib_mr.lkey;
	else
		printk(KERN_EMERG "failure allocating lkey.  rc=%d\n", rc);

        return rc;
}


static void bgvrnic_free_lkey(struct bgvrnic_device* vrnic_dev,
				u32 lkey)
{
	spin_lock_irq(&vrnic_dev->mr_pool.lock);	
	idr_remove(&vrnic_dev->mr_pool.idr, lkey);
	spin_unlock_irq(&vrnic_dev->mr_pool.lock);

	return;
}


static struct ib_qp* bgvrnic_create_qp(struct ib_pd* ib_pd,
				struct ib_qp_init_attr* ib_qp_init_attr,
				struct ib_udata* ib_udata)
{
	int rc;
	struct ib_qp* qp;
	struct bgvrnic_qp* vrnic_qp;
	struct bgvrnic_pd* vrnic_pd = container_of(ib_pd, struct bgvrnic_pd, ib_pd);
	struct bgvrnic_device* vrnic_dev = container_of(ib_pd->device, struct bgvrnic_device, ib_dev);
ENTER
	if (ib_qp_init_attr->create_flags) {
		qp = ERR_PTR(-EINVAL);
		goto out;
	}
	if (ib_qp_init_attr->qp_type != IB_QPT_RC) {
		qp = ERR_PTR(-EINVAL);
		goto out;
	}

	vrnic_qp = kzalloc(sizeof(*vrnic_qp), GFP_KERNEL);
	if (!vrnic_qp) {
		printk(KERN_EMERG "QP allocation failure!\n");
		qp = ERR_PTR(-ENOMEM);
		goto out;
	}
	qp = &vrnic_qp->ib_qp;

	rc = bgvrnic_alloc_qpn(vrnic_dev, vrnic_qp);  // stores QPN in the QP
	if (rc)
		goto out1;

	vrnic_qp->ib_qp_init_attrs = *ib_qp_init_attr;
	INIT_LIST_HEAD(&vrnic_qp->sq);
	INIT_LIST_HEAD(&vrnic_qp->rq);
	spin_lock_init(&vrnic_qp->rq_lock);
	spin_lock_init(&vrnic_qp->sq_lock);
	spin_lock_init(&vrnic_qp->lock);
	init_waitqueue_head(&vrnic_qp->waitq);
	vrnic_qp->scq = container_of(ib_qp_init_attr->send_cq, struct bgvrnic_cq, ib_cq);
	vrnic_qp->rcq = container_of(ib_qp_init_attr->recv_cq, struct bgvrnic_cq, ib_cq);
	vrnic_qp->pd = vrnic_pd;
	atomic_set(&vrnic_qp->references, 1);
	vrnic_qp->ib_qp_attrs.qp_state = IB_QPS_RESET;
	vrnic_qp->ib_qp_attrs.rq_psn = 0;
	vrnic_qp->ib_qp_attrs.sq_psn = 0;
	vrnic_qp->vrnic_dev = vrnic_dev;
	
	if (ib_udata) {
		struct bgvrnic_uresp_create_qp uresp; 		

		uresp.sq_size = ib_qp_init_attr->cap.max_send_wr; 
		uresp.rq_size = ib_qp_init_attr->cap.max_recv_wr;
		uresp.qp_id = vrnic_qp->ib_qp.qp_num;

		rc = ib_copy_to_udata(ib_udata, &uresp, sizeof(uresp));
		if (rc) {
			qp = ERR_PTR(rc);	
			goto out2;
		}
	}

	goto out;

out2:
	bgvrnic_free_qpn(vrnic_dev, vrnic_qp->ib_qp.qp_num);

out1:
	kfree(vrnic_qp);

out:

EXIT
	return qp; 
}


static int bgvrnic_modify_qp(struct ib_qp* ib_qp,
				struct ib_qp_attr* ib_qp_attrs,
				int ib_attr_mask,
				struct ib_udata* ib_udata)
{
	int rc = 0; 
	struct bgvrnic_qp* vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);

ENTER
	if (ib_attr_mask & IB_QP_STATE) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_attr_mask, ib_qp_attrs->qp_state, vrnic_qp->ib_qp_attrs.qp_state);
		ib_attr_mask &= ~IB_QP_STATE;
		if (ib_qp_attrs->qp_state < 0 || ib_qp_attrs->qp_state > IB_QPS_ERR) {
			rc = -EINVAL;
			goto out;
		}

		if (ib_qp_attrs->qp_state == IB_QPS_RTS && !vrnic_qp->mu_context) {
			/* Can't go to this state because we aren't connected to another QP. */
			rc = -EINVAL;
			goto out;
		}

		/* SQD is a special case for iWARP.  This isn't really a valid state but we 
 		 * have to do something with it.  If the QP doesn't have any outstanding 
 		 * WQEs then send disconnect/close events.  Otherwise, set sq_draining flag 
 		 * and return. */
		if (ib_qp_attrs->qp_state == IB_QPS_SQD) {
			int remaining_wqes = 0;

			spin_lock(&vrnic_qp->sq_lock);
			remaining_wqes = bgvrnic_process_send_wqes(vrnic_qp);	
			spin_unlock(&vrnic_qp->sq_lock);
			ib_qp_attrs->sq_draining = vrnic_qp->ib_qp_attrs.sq_draining = (remaining_wqes <= 0 ? 0 : 1);

			if (!ib_qp_attrs->sq_draining && vrnic_qp->cm_id) {
                        	struct iw_cm_event e;

                        	e.event = IW_CM_EVENT_DISCONNECT;
                        	e.status = 0;
                        	vrnic_qp->ib_qp_attrs.qp_state = IB_QPS_SQE;
                        	e.remote_addr = vrnic_qp->cm_id->remote_addr;
                        	e.local_addr = vrnic_qp->cm_id->local_addr;
                        	e.private_data = e.provider_data = NULL;
                        	e.private_data_len = 0;
                        	rc = vrnic_qp->cm_id->event_handler(vrnic_qp->cm_id, &e);
                        	if (rc)
                                	printk(KERN_EMERG "%s:%d - CM event type %d returned %d\n", __func__, __LINE__, e.event, rc);
                       		else {
                                	e.event = IW_CM_EVENT_CLOSE;
                                	e.status = 0;
                                	e.remote_addr = vrnic_qp->cm_id->remote_addr;
                               		e.local_addr = vrnic_qp->cm_id->local_addr;
                                	e.private_data = e.provider_data = NULL;
                                	e.private_data_len = 0;
                                	rc = vrnic_qp->cm_id->event_handler(vrnic_qp->cm_id, &e);
                                	if (rc)
                                        	printk(KERN_EMERG "%s:%d - CM event type %d returned %d\n", __func__, __LINE__, e.event, rc);
                        	}

				vrnic_qp->cm_id->rem_ref(vrnic_qp->cm_id);
				vrnic_qp->cm_id = NULL;
				mudm_disconnect(vrnic_qp->mu_context);
			}
		}

		vrnic_qp->ib_qp_attrs.qp_state = ib_qp_attrs->qp_state;
	}

	if (ib_attr_mask & IB_QP_CUR_STATE) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->cur_qp_state, vrnic_qp->ib_qp_attrs.cur_qp_state, 
					vrnic_qp->ib_qp_attrs.qp_state);
		ib_attr_mask &= ~IB_QP_CUR_STATE;
		if (ib_qp_attrs->cur_qp_state != IB_QPS_RTR &&
		    ib_qp_attrs->cur_qp_state != IB_QPS_RTS &&
		    ib_qp_attrs->cur_qp_state != IB_QPS_SQD &&
		    ib_qp_attrs->cur_qp_state != IB_QPS_SQE) {
			rc = -EINVAL;
			goto out;
		} else
			vrnic_qp->ib_qp_attrs.cur_qp_state = ib_qp_attrs->cur_qp_state;
	}

	if (ib_attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->en_sqd_async_notify, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_EN_SQD_ASYNC_NOTIFY;
		vrnic_qp->ib_qp_attrs.en_sqd_async_notify = ib_qp_attrs->en_sqd_async_notify;
	}

	if (ib_attr_mask & IB_QP_QKEY) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->qkey, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_QKEY;
		vrnic_qp->ib_qp_attrs.qkey = ib_qp_attrs->qkey;
	}

	if (ib_attr_mask & IB_QP_AV) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, vrnic_qp->ib_qp_attrs.qp_state, 0, 0);
		ib_attr_mask &= ~IB_QP_AV;
	}

	if (ib_attr_mask & IB_QP_PATH_MTU) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->path_mtu, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_PATH_MTU;
		vrnic_qp->ib_qp_attrs.path_mtu = ib_qp_attrs->path_mtu;
	}

	if (ib_attr_mask & IB_QP_TIMEOUT) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->timeout, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_TIMEOUT;
		vrnic_qp->ib_qp_attrs.timeout = ib_qp_attrs->timeout;
	}

	if (ib_attr_mask & IB_QP_RETRY_CNT) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->retry_cnt, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_RETRY_CNT;
		vrnic_qp->ib_qp_attrs.retry_cnt = ib_qp_attrs->retry_cnt;
	}

	if (ib_attr_mask & IB_QP_RNR_RETRY) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->rnr_retry, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_RNR_RETRY;
		vrnic_qp->ib_qp_attrs.rnr_retry = ib_qp_attrs->rnr_retry;
	}

	if (ib_attr_mask & IB_QP_RQ_PSN) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->rq_psn, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_RQ_PSN;
		vrnic_qp->ib_qp_attrs.rq_psn = ib_qp_attrs->rq_psn;
	}

	if (ib_attr_mask & IB_QP_MAX_QP_RD_ATOMIC) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->max_rd_atomic, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_MAX_QP_RD_ATOMIC;
		vrnic_qp->ib_qp_attrs.max_rd_atomic = ib_qp_attrs->max_rd_atomic;
	}

	if (ib_attr_mask & IB_QP_MIN_RNR_TIMER) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->min_rnr_timer, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_MIN_RNR_TIMER;
		vrnic_qp->ib_qp_attrs.min_rnr_timer = ib_qp_attrs->min_rnr_timer;
	}

        if (ib_attr_mask & IB_QP_SQ_PSN) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->sq_psn, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_SQ_PSN;
		vrnic_qp->ib_qp_attrs.sq_psn = ib_qp_attrs->sq_psn;
        }

        if (ib_attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->max_dest_rd_atomic, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_MAX_DEST_RD_ATOMIC;
		vrnic_qp->ib_qp_attrs.max_dest_rd_atomic = ib_qp_attrs->max_dest_rd_atomic;
        }

	if (ib_attr_mask & IB_QP_DEST_QPN) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->dest_qp_num, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_DEST_QPN;
		vrnic_qp->ib_qp_attrs.dest_qp_num = ib_qp_attrs->dest_qp_num;
	}

	if (ib_attr_mask & IB_QP_PATH_MIG_STATE) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->path_mig_state, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_PATH_MIG_STATE;
		vrnic_qp->ib_qp_attrs.path_mig_state = ib_qp_attrs->path_mig_state;
	}

	if (ib_attr_mask & IB_QP_CAP) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, vrnic_qp->ib_qp_attrs.qp_state, 0, 0);	
		ib_attr_mask &= ~IB_QP_CAP;
		vrnic_qp->ib_qp_attrs.cap = ib_qp_attrs->cap;
	}

	if (ib_attr_mask & IB_QP_ACCESS_FLAGS) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->qp_access_flags, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_ACCESS_FLAGS;
		vrnic_qp->ib_qp_attrs.qp_access_flags = ib_qp_attrs->qp_access_flags;
	}

	if (ib_attr_mask & IB_QP_PKEY_INDEX) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->pkey_index, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_PKEY_INDEX;
		vrnic_qp->ib_qp_attrs.pkey_index = ib_qp_attrs->pkey_index;
	}

	if (ib_attr_mask & IB_QP_PORT) {
		Kernel_WriteFlightLog(FLIGHTLOG, FL_MODIFY_QP, __LINE__, ib_qp_attrs->port_num, vrnic_qp->ib_qp_attrs.qp_state, 0);
		ib_attr_mask &= ~IB_QP_PORT;
		vrnic_qp->ib_qp_attrs.port_num = ib_qp_attrs->port_num;
	}

	if (ib_attr_mask)
		printk(KERN_INFO "%s - Unhandled attributes: 0x%x\n", __func__, ib_attr_mask);	

out:

EXIT
	return rc;
}


int bgvrnic_query_qp(struct ib_qp *ib_qp,
		     struct ib_qp_attr *ib_qp_attrs,
		     int ib_attr_mask,
		     struct ib_qp_init_attr *ib_init_attr)
{
	int rc = 0;
	struct bgvrnic_qp* vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);

ENTER
	if (ib_attr_mask & IB_QP_STATE)
		ib_qp_attrs->qp_state = vrnic_qp->ib_qp_attrs.qp_state;
	if (ib_attr_mask & IB_QP_CUR_STATE)
		ib_qp_attrs->cur_qp_state = vrnic_qp->ib_qp_attrs.cur_qp_state;
	if (ib_attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY)
		ib_qp_attrs->en_sqd_async_notify = vrnic_qp->ib_qp_attrs.en_sqd_async_notify;
	if (ib_attr_mask & IB_QP_ACCESS_FLAGS)
		ib_qp_attrs->qp_access_flags = vrnic_qp->ib_qp_attrs.qp_access_flags;
	if (ib_attr_mask & IB_QP_PKEY_INDEX)
		ib_qp_attrs->pkey_index = vrnic_qp->ib_qp_attrs.pkey_index;
	if (ib_attr_mask & IB_QP_PORT)
		ib_qp_attrs->port_num = vrnic_qp->ib_qp_attrs.port_num;
	if (ib_attr_mask & IB_QP_QKEY)
		ib_qp_attrs->qkey = vrnic_qp->ib_qp_attrs.qkey;
	if (ib_attr_mask & IB_QP_PATH_MTU)
		ib_qp_attrs->path_mtu = vrnic_qp->ib_qp_attrs.path_mtu;
	if (ib_attr_mask & IB_QP_TIMEOUT)
		ib_qp_attrs->timeout = vrnic_qp->ib_qp_attrs.timeout;
	if (ib_attr_mask & IB_QP_RETRY_CNT)
		ib_qp_attrs->retry_cnt = vrnic_qp->ib_qp_attrs.retry_cnt;	
	if (ib_attr_mask & IB_QP_RNR_RETRY)
		ib_qp_attrs->rnr_retry = vrnic_qp->ib_qp_attrs.rnr_retry;
	if (ib_attr_mask & IB_QP_RQ_PSN)
		ib_qp_attrs->rq_psn = vrnic_qp->ib_qp_attrs.rq_psn;
	if (ib_attr_mask & IB_QP_MAX_QP_RD_ATOMIC)
		ib_qp_attrs->max_rd_atomic = vrnic_qp->ib_qp_attrs.max_rd_atomic;
	if (ib_attr_mask & IB_QP_MIN_RNR_TIMER)
		ib_qp_attrs->min_rnr_timer = vrnic_qp->ib_qp_attrs.min_rnr_timer;
	if (ib_attr_mask & IB_QP_SQ_PSN)
		ib_qp_attrs->sq_psn = vrnic_qp->ib_qp_attrs.sq_psn;
	if (ib_attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		ib_qp_attrs->max_dest_rd_atomic = vrnic_qp->ib_qp_attrs.max_dest_rd_atomic;
	if (ib_attr_mask & IB_QP_PATH_MIG_STATE)
		ib_qp_attrs->path_mig_state = vrnic_qp->ib_qp_attrs.path_mig_state;
	if (ib_attr_mask & IB_QP_CAP)
		ib_qp_attrs->cap = vrnic_qp->ib_qp_attrs.cap;
	if (ib_attr_mask & IB_QP_DEST_QPN)
		ib_qp_attrs->dest_qp_num = vrnic_qp->ib_qp_attrs.dest_qp_num; 
	*ib_init_attr = vrnic_qp->ib_qp_init_attrs;

EXIT
	return rc;
}



static int bgvrnic_destroy_qp(struct ib_qp* ib_qp)
{
	int rc = 0;
	struct bgvrnic_qp* vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);

ENTER
	bgvrnic_free_qpn(vrnic_qp->vrnic_dev, vrnic_qp->ib_qp.qp_num);

	atomic_dec(&vrnic_qp->references);
	wait_event(vrnic_qp->waitq, atomic_read(&vrnic_qp->references) <= 1);
	if (vrnic_qp->cm_id) {
		mudm_disconnect(vrnic_qp->mu_context);
		if (vrnic_qp->ib_qp_attrs.qp_state == IB_QPS_RTS) {
			struct iw_cm_event e;

			e.event = IW_CM_EVENT_DISCONNECT;
                	e.status = 0;
                	vrnic_qp->ib_qp_attrs.qp_state = IB_QPS_SQE;
                	e.remote_addr = vrnic_qp->cm_id->remote_addr;
                	e.local_addr = vrnic_qp->cm_id->local_addr;
                	e.private_data = e.provider_data = NULL;
                	e.private_data_len = 0;
                	rc = vrnic_qp->cm_id->event_handler(vrnic_qp->cm_id, &e);
                	if (rc) 
				printk(KERN_EMERG "%s:%d - CM event type %d returned %d\n", __func__, __LINE__, e.event, rc);
                	else {
                        	e.event = IW_CM_EVENT_CLOSE;
                        	e.status = 0;
                        	e.remote_addr = vrnic_qp->cm_id->remote_addr;
                        	e.local_addr = vrnic_qp->cm_id->local_addr; 
                        	e.private_data = e.provider_data = NULL;
                        	e.private_data_len = 0;
                        	rc = vrnic_qp->cm_id->event_handler(vrnic_qp->cm_id, &e);
                        	if (rc)
                                	printk(KERN_EMERG "%s:%d - CM event type %d returned %d\n", __func__, __LINE__, e.event, rc);
                	}
		}
		vrnic_qp->cm_id->rem_ref(vrnic_qp->cm_id);
		vrnic_qp->cm_id = NULL;
	}
	
	kfree(vrnic_qp);
EXIT
	return rc;
}


static int bgvrnic_process_send_wqes(struct bgvrnic_qp* vrnic_qp)
{
	int rc = 0;
	u32 remaining_wqes = 0;
	struct bgvrnic_wqe* wqe;
	struct bgvrnic_wqe* tmp;
ENTER

	list_for_each_entry_safe(wqe, tmp, &vrnic_qp->sq, list) {
		// Send any WQEs that haven't been sent.  Complete any WQEs that are done.  Stop when encountering 
		// the first WQE that isn't complete because we can't complete any after that WQE until it is done.  
		switch (wqe->state) {
			case BGVRNIC_WQES_INIT:
			case BGVRNIC_WQES_WAIT: { // Send this
				struct ib_send_wr* ib_wr = &wqe->wr.ib_send;
				struct ionet_send_remote_payload payload;

				payload.sgl_num = MUDM_MAX_SGE;
				payload.RequestID = (u64) wqe;
				payload.data_length = bgvrnic_xlate_sgl(vrnic_qp->vrnic_dev, ib_wr->sg_list, ib_wr->num_sge, 
									payload.SGE, &payload.sgl_num);
				if (payload.data_length < 0) {
					printk(KERN_EMERG "%s:%d - bgvrnic_xlate_sgl failed with %d\n", __func__, __LINE__, payload.data_length);
					if (vrnic_qp->ib_qp_init_attrs.sq_sig_type == IB_SIGNAL_ALL_WR ||
					    (ib_wr->send_flags & IB_SEND_SIGNALED)) {
						rc = bgvrnic_add_cqe(vrnic_qp->scq, vrnic_qp, &wqe->wr.ib_send, NULL, wqe->length,
								     wqe->status, wqe->imm_data_valid, wqe->imm_data);
						if (rc)
							printk(KERN_EMERG "%s:%d - Failure adding CQE.  rc=%d\n", __func__,
                                                                        __LINE__, rc);
                                        }
					list_del(&wqe->list);
					bgvrnic_free_wqe(wqe);
					return payload.data_length;
				}

				if (wqe->imm_data_valid) {
					payload.flags = MUDM_IMMEDIATE_DATA; 
					payload.immediate_data = wqe->imm_data; 
				} else
					payload.flags = 0;

				if (ib_wr->send_flags & IB_SEND_SOLICITED)
					payload.flags |= MUDM_SOLICITED;
				wqe->length = payload.data_length;

				switch (ib_wr->opcode) {
					case IB_WR_SEND:
						if (payload.sgl_num == 1 && payload.data_length <= MUDM_MAX_PAYLOAD_SIZE)				
							wqe->status = mudm_send_pkt(vrnic_qp->mu_context, (void*) wqe, MUDM_PKT_DATA_IMM, 
										    (void*) (u64) payload.SGE[0].physicalAddr, 
										    payload.SGE[0].memlength);
						else 
							wqe->status = mudm_send_message_remote(vrnic_qp->mu_context, MUDM_PKT_DATA_REMOTE, 
												&payload);
					break;
			
					case IB_WR_SEND_WITH_IMM:
						wqe->status = mudm_send_message_remote(vrnic_qp->mu_context, MUDM_PKT_DATA_REMOTE, &payload);
					break;

					case IB_WR_RDMA_READ: 
						payload.remote_vaddr = ib_wr->wr.rdma.remote_addr;
						payload.rkey = ib_wr->wr.rdma.rkey;
						wqe->status = mudm_send_message_remote(vrnic_qp->mu_context, MUDM_RDMA_WRITE, &payload);

					break;

					case IB_WR_RDMA_WRITE:
					case IB_WR_RDMA_WRITE_WITH_IMM:
						payload.remote_vaddr = ib_wr->wr.rdma.remote_addr;
						payload.rkey = ib_wr->wr.rdma.rkey;
						wqe->status = mudm_send_message_remote(vrnic_qp->mu_context, MUDM_RDMA_READ, &payload);

					break;

					default:
						printk(KERN_EMERG "Can't do opcode %d yet.\n", ib_wr->opcode);
						
				}

				switch (wqe->status) {
					case -ECANCELED:
						wqe->status = IB_WC_REM_ABORT_ERR;
					case 0: /* Already done.  Move to completion queue. */
						wqe->state = BGVRNIC_WQES_DONE;
						if (vrnic_qp->ib_qp_init_attrs.sq_sig_type == IB_SIGNAL_ALL_WR || 
						    (ib_wr->send_flags & IB_SEND_SIGNALED)) {
							rc = bgvrnic_add_cqe(vrnic_qp->scq, vrnic_qp, &wqe->wr.ib_send, NULL, wqe->length,
									     wqe->status, wqe->imm_data_valid, wqe->imm_data);
							if (rc) 
								printk(KERN_EMERG "%s:%d - Failure adding CQE.  rc=%d\n", __func__, 
									__LINE__, rc);
						}
						list_del(&wqe->list);
						bgvrnic_free_wqe(wqe);
						break;

					case -EINPROGRESS:
						remaining_wqes++;
						wqe->state = BGVRNIC_WQES_RUN;
						break;

					case -EBUSY:
						remaining_wqes++;
						wqe->state = BGVRNIC_WQES_WAIT;
						break;
					
					default:
						printk(KERN_EMERG "%s:%d - Failed.  wqe->status=%d\n", __func__, __LINE__, wqe->status);
						BUG();
				}
			}
			break;

			case BGVRNIC_WQES_RUN: // Not done.  No need to look further.
				remaining_wqes++;
				goto out;
			
				break;

			case BGVRNIC_WQES_DONE:  // Done.  Move to complete queue.
			{
				struct ib_send_wr* ib_wr = &wqe->wr.ib_send;

				if (vrnic_qp->ib_qp_init_attrs.sq_sig_type == IB_SIGNAL_ALL_WR ||
				    (ib_wr->send_flags & IB_SEND_SIGNALED)) {
					 rc = bgvrnic_add_cqe(vrnic_qp->scq, vrnic_qp, &wqe->wr.ib_send, NULL, wqe->length,
							      wqe->status, wqe->imm_data_valid, wqe->imm_data); 
					if (rc)
						printk(KERN_EMERG "%s:%d - Failure adding CQE.  rc=%d\n", __func__,
					       		__LINE__, rc);
				}
				list_del(&wqe->list);
				bgvrnic_free_wqe(wqe);
			}
			break;

			default:
				printk(KERN_EMERG "%s:%d - Unknown WQE state %d\n", __func__, __LINE__, wqe->state);
				BUG();
		}
	}

out:
EXIT

	return (rc < 0 ? rc : remaining_wqes);
}


static int bgvrnic_post_send(struct ib_qp* ib_qp,
				struct ib_send_wr* ib_wr,
				struct ib_send_wr** bad_wr)
{
	int rc = 0;
	struct bgvrnic_qp* vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);

ENTER
	if (vrnic_qp->ib_qp_attrs.qp_state != IB_QPS_RTS) {
		printk(KERN_EMERG "Incorrect QP state of %d\n", vrnic_qp->ib_qp_attrs.qp_state);
		return -EINVAL;
	}

	while (ib_wr) {
		struct bgvrnic_wqe* wqe; 

		if (ib_wr->num_sge > vrnic_qp->ib_qp_init_attrs.cap.max_recv_sge) {
			printk(KERN_EMERG "WR %p has %d SGEs but only %d are allowed!\n", ib_wr,
				ib_wr->num_sge, vrnic_qp->ib_qp_init_attrs.cap.max_recv_sge);
			rc = -EINVAL;
			*bad_wr = ib_wr;
			break;
		}

		wqe = bgvrnic_alloc_wqe(BGVRNIC_WQET_SEND, ib_wr, vrnic_qp);
		if (!wqe) {
			rc = -ENOMEM;
			*bad_wr = ib_wr;
			break;
		}

		switch (ib_wr->opcode) {
			case IB_WR_SEND:
			case IB_WR_RDMA_WRITE:
				break;

			case IB_WR_SEND_WITH_IMM:
			case IB_WR_RDMA_WRITE_WITH_IMM:
				wqe->imm_data_valid = 1;		
				wqe->imm_data = ib_wr->ex.imm_data;
				break;

			case IB_WR_RDMA_READ:
				if (ib_wr->num_sge > 1) {
					rc = -EINVAL;
					*bad_wr = ib_wr;
					goto out;
				}
				break;

			default:
				printk(KERN_EMERG "Opcode %d not supported!\n", ib_wr->opcode);
				rc = -EOPNOTSUPP;
				*bad_wr = ib_wr;
				kfree(wqe);
				goto out;
		}

		spin_lock(&vrnic_qp->sq_lock);
		list_add_tail(&wqe->list, &vrnic_qp->sq);
		spin_unlock(&vrnic_qp->sq_lock);
		ib_wr = ib_wr->next;
	}

	spin_lock(&vrnic_qp->sq_lock);
	bgvrnic_process_send_wqes(vrnic_qp);
	spin_unlock(&vrnic_qp->sq_lock);
out: 

EXIT

	return rc;
}


static int bgvrnic_post_recv(struct ib_qp* ib_qp,
				struct ib_recv_wr* ib_wr,
				struct ib_recv_wr** bad_wr)
{
	int rc = 0; 
	struct bgvrnic_qp* vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);

ENTER
	if (vrnic_qp->ib_qp_attrs.qp_state == IB_QPS_RESET || vrnic_qp->ib_qp_attrs.qp_state >= IB_QPS_ERR) {
		rc = -EINVAL;
		goto out;
	}

	if (ib_qp->srq) {
		printk(KERN_EMERG "Shared Rx Qs not supported.\n");
		rc = -EOPNOTSUPP;
		goto out;
	}

	spin_lock(&vrnic_qp->rq_lock);
	while (ib_wr) {
		struct bgvrnic_wqe* wqe;

		if (ib_wr->num_sge > vrnic_qp->ib_qp_init_attrs.cap.max_recv_sge) {
			printk(KERN_EMERG "WR %p has %d SGEs but only %d are allowed!\n", ib_wr, 
				ib_wr->num_sge, vrnic_qp->ib_qp_init_attrs.cap.max_recv_sge);
			rc = -EINVAL;
			*bad_wr = ib_wr;
			break;
		}
	
		wqe = bgvrnic_alloc_wqe(BGVRNIC_WQET_RECV, (void*) ib_wr, vrnic_qp);
		Kernel_WriteFlightLog(FLIGHTLOG, FL_POST_RECV, (u64) vrnic_qp, (u64) ib_wr, vrnic_qp->ib_qp_attrs.qp_state, (u64) wqe);
		if (!wqe) {
			rc = -ENOMEM;
			*bad_wr = ib_wr;
			break;	
		}

		list_add_tail(&wqe->list, &vrnic_qp->rq);

		ib_wr = ib_wr->next;
	}
	spin_unlock(&vrnic_qp->rq_lock);
out:
	
EXIT
	return rc;
}


static struct ib_cq* bgvrnic_create_cq(struct ib_device* ib_dev,
					int entries,
					int vector,
					struct ib_ucontext* ib_ucontext,
					struct ib_udata* ib_udata)
{
	int rc = 0;
	struct bgvrnic_cq* vrnic_cq = (struct bgvrnic_cq*) kzalloc(sizeof(*vrnic_cq), GFP_KERNEL);
	struct ib_cq* cq = &vrnic_cq->ib_cq;

ENTER
	if (!vrnic_cq) {
		printk(KERN_EMERG "Completion queue allocation failure!\n");
		return ERR_PTR(-ENOMEM);	
	}

	INIT_LIST_HEAD(&vrnic_cq->entries);
	spin_lock_init(&vrnic_cq->lock);
	init_waitqueue_head(&vrnic_cq->waitq);
	atomic_set(&vrnic_cq->references, 1);
	vrnic_cq->ib_cq.cqe = entries - 1;

	if (ib_ucontext) {
		struct bgvrnic_uresp_create_cq uresp;

		uresp.cq_id = 1; //vrnic_cq->id;
		rc = ib_copy_to_udata(ib_udata, &uresp, sizeof(uresp));
		if (rc)
			goto out1;
	}

	goto out;

out1:
	cq = ERR_PTR(-rc);
	kfree(vrnic_cq);
	
out:
	
EXIT
	return cq; 
}


static int bgvrnic_destroy_cq(struct ib_cq* ib_cq)
{
	struct bgvrnic_cq* vrnic_cq = container_of(ib_cq, struct bgvrnic_cq, ib_cq);
ENTER

	spin_lock_irq(&vrnic_cq->lock);
	atomic_dec(&vrnic_cq->references);
	spin_unlock_irq(&vrnic_cq->lock);
	wait_event(vrnic_cq->waitq, atomic_read(&vrnic_cq->references) <= 1);

	kfree(vrnic_cq);
EXIT
	return 0;
}


static int bgvrnic_req_notify_cq(struct ib_cq* ib_cq,
				 enum ib_cq_notify_flags flags)
{
	int rc = 0;
	struct bgvrnic_cq* vrnic_cq = container_of(ib_cq, struct bgvrnic_cq, ib_cq);
ENTER
	switch (flags) {
		case IB_CQ_SOLICITED:
		case IB_CQ_NEXT_COMP:
		case IB_CQ_SOLICITED_MASK:
			vrnic_cq->flags |= flags;
			break;
		default:
			rc = -EINVAL;
	}
EXIT
	return rc;
}



static int bgvrnic_remove_cqe(struct bgvrnic_cq* vrnic_cq,
			      struct ib_wc* ib_wc)
{
	int rc = 0;

	if (!list_empty(&vrnic_cq->entries)) {
		struct bgvrnic_cqe* cqe = list_first_entry(&vrnic_cq->entries, struct bgvrnic_cqe, list);

		*ib_wc = cqe->ib_wc;
		list_del(&cqe->list);
		kfree(cqe);
		rc = 1;
	}

	return rc;
}


static int bgvrnic_poll_cq(struct ib_cq* ib_cq,
			   int numCQEs,
			   struct ib_wc* ib_wc)
{
	int rc = 0; 
	u32 count;
	unsigned long flags;
	struct bgvrnic_cq* vrnic_cq = container_of(ib_cq, struct bgvrnic_cq, ib_cq);


	spin_lock_irqsave(&vrnic_cq->lock, flags);
	for (count = 0; count < numCQEs; count++) {
		rc = bgvrnic_remove_cqe(vrnic_cq, ib_wc + count);	
		if (rc <= 0)
			break;
	}
	spin_unlock_irqrestore(&vrnic_cq->lock, flags);

	return (rc < 0 ? rc : count);
}


static struct ib_mr* bgvrnic_get_dma_mr(struct ib_pd* ib_pd,
					int access)
{
	struct ib_mr* mr = ERR_PTR(-ENOSYS);

ENTER

EXIT
	return mr;
}


static struct ib_mr* bgvrnic_reg_phys_mr(struct ib_pd* ib_pd,
					 struct ib_phys_buf* phys_buf,
					 int num_buffs,
					 int access,
					 u64* iova_start)
{
	struct ib_mr* mr = ERR_PTR(-ENOSYS);

ENTER

EXIT
	return mr;
}


static struct ib_mr* bgvrnic_reg_user_mr(struct ib_pd* ib_pd,
					 u64 start,
					 u64 length,
					 u64 virt_addr,
					 int access,
					 struct ib_udata* ib_udata)
{
	int rc;
	u32 j;
	u32 k;
	u32 len;
	u32 shift;
	u32 bytes_to_map = length; 
	struct ib_umem* umem;
	struct ib_umem_chunk* chunk;
	struct ib_mr* ib_mr;
	struct bgvrnic_device* vrnic_dev = container_of(ib_pd->device, struct bgvrnic_device, ib_dev);
	struct bgvrnic_pd* vrnic_pd = container_of(ib_pd, struct bgvrnic_pd, ib_pd);
	struct bgvrnic_uresp_reg_mr uresp;
	struct bgvrnic_mr* vrnic_mr;

ENTER
	if (!length) {
		ib_mr = ERR_PTR(-EINVAL);
		goto out;
	}

	umem = ib_umem_get(ib_pd->uobject->context, start, length, access, 0);
	if (IS_ERR(umem)) {
		ib_mr = (struct ib_mr*) umem;
		goto out;
	}

	vrnic_mr = kmalloc(sizeof(*vrnic_mr), GFP_KERNEL);
	if (!vrnic_mr) {
		ib_mr = ERR_PTR(-ENOMEM);
		goto out1;
	}

	rc = bgvrnic_alloc_lkey(vrnic_dev, vrnic_mr);
	if (rc) {
		ib_mr = ERR_PTR(-EINVAL);
		goto out2;
	}

	// Allocate storage for physical SGL and initialize.  Combine contiguous pages into one SGE. 
	vrnic_mr->sge = (struct mudm_sgl*) kmalloc(sizeof(vrnic_mr->sge[0]) * ib_umem_page_count(umem), GFP_KERNEL);
	if (!vrnic_mr->sge) {
		ib_mr = ERR_PTR(-ENOMEM);
		goto out3;
	}
	vrnic_mr->sge_num = 0;
	shift = ilog2(umem->page_size);
	list_for_each_entry(chunk, &umem->chunk_list, list) { 
		for (j = 0; j < chunk->nmap; j++) {
			len = sg_dma_len(&chunk->page_list[j]) >> shift;
			for (k = 0; k < len; k++) {
				u64 address = sg_dma_address(&chunk->page_list[j]) + umem->page_size * k;
				u32 p_len = sg_dma_len(&chunk->page_list[j]);

				/* Account for the memory region offset if this is the first SGE. */
				if (vrnic_mr->sge_num == 0) {
					address += umem->offset;
					p_len -= umem->offset;
				}

				/* Truncate the length if the remaining bytes to map is less. */
				p_len = min_t(u32, p_len, bytes_to_map);
				if (vrnic_mr->sge_num == 0 || 
				    vrnic_mr->sge[vrnic_mr->sge_num-1].physicalAddr + vrnic_mr->sge[vrnic_mr->sge_num-1].memlength != address ||
				    vrnic_mr->sge[vrnic_mr->sge_num-1].memlength + p_len >= MUDM_MAX_SGL_LENGTH) {
					vrnic_mr->sge[vrnic_mr->sge_num].physicalAddr = address;
					vrnic_mr->sge[vrnic_mr->sge_num].memlength = p_len;
					vrnic_mr->sge_num++;
				} else { 
					vrnic_mr->sge[vrnic_mr->sge_num-1].memlength += p_len; 
				} 

				bytes_to_map -= p_len;
			}
		}
	}

	/* Reallocate SGL of the correct size.  Copy the SGEs to the new SGL. */
	if (vrnic_mr->sge_num < ib_umem_page_count(umem)) {
		struct mudm_sgl* new_sge = (struct mudm_sgl*) kmalloc(sizeof(vrnic_mr->sge[0]) * vrnic_mr->sge_num, GFP_KERNEL);

		if (!new_sge) {
			ib_mr = ERR_PTR(-ENOMEM);	
			goto out4;
		} else {
			memcpy(new_sge, vrnic_mr->sge, sizeof(vrnic_mr->sge[0]) * vrnic_mr->sge_num);
			kfree(vrnic_mr->sge);
			vrnic_mr->sge = new_sge;
		}
	} 

	ib_mr = &vrnic_mr->ib_mr;
	vrnic_mr->umem = umem;
	vrnic_mr->pd = vrnic_pd;
	vrnic_mr->length = length;
	vrnic_mr->start = start;

	if (ib_udata) {
		uresp.num_frags = vrnic_mr->sge_num;
		ib_copy_to_udata(ib_udata, &uresp, sizeof(uresp));
	}

	goto out;

out4:
	kfree(vrnic_mr->sge);

out3:
	bgvrnic_free_lkey(vrnic_dev, vrnic_mr->ib_mr.lkey);

out2: 
	kfree(vrnic_mr);

out1:
	ib_umem_release(umem);
out:

EXIT
	return ib_mr;
}


static int bgvrnic_dereg_mr(struct ib_mr* ib_mr)
{
	int rc = 0; 
	struct bgvrnic_mr* vrnic_mr = container_of(ib_mr, struct bgvrnic_mr, ib_mr);
	struct bgvrnic_device* vrnic_dev = container_of(ib_mr->device, struct bgvrnic_device, ib_dev);
ENTER
	if (vrnic_mr->umem)
		ib_umem_release(vrnic_mr->umem);
	bgvrnic_free_lkey(vrnic_dev, vrnic_mr->ib_mr.lkey);
	kfree(vrnic_mr);

EXIT
	return rc;
}


static int bgvrnic_get_protocol_stats(struct ib_device* ib_dev,
				      union rdma_protocol_stats* stats)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_modify_device(struct ib_device* ib_dev,
				 int device_modify_mask,
				 struct ib_device_modify* ib_device_modify)
{
	int rc = -ENOSYS;
ENTER
	
EXIT
	return rc;
}


static int bgvrnic_modify_port(struct ib_device* ib_dev,
				u8 port_num, 
				int port_modify_mask,
				struct ib_port_modify* ib_port_modify)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_mmap(struct ib_ucontext* context,
			struct vm_area_struct* vma)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_modify_ah(struct ib_ah* ah,
			     struct ib_ah_attr* ah_attr)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


int bgvrnic_query_ah(struct ib_ah* ah,
		     struct ib_ah_attr* ah_attr)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static struct ib_srq* bgvrnic_create_srq(struct ib_pd* ib_pd,
					 struct ib_srq_init_attr* ib_srq_init_attr,
					 struct ib_udata* udata)
{
ENTER

EXIT
	return ERR_PTR(-ENOSYS);
}


static int bgvrnic_modify_srq(struct ib_srq* ib_srq,
				struct ib_srq_attr* ib_srq_attr,
				enum ib_srq_attr_mask ib_srq_attr_mask,
				struct ib_udata* udata)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_query_srq(struct ib_srq* ib_srq,
			     struct ib_srq_attr* ib_srq_attr)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_destroy_srq(struct ib_srq* ib_srq)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_post_srq_recv(struct ib_srq* ib_srq,
				 struct ib_recv_wr* ib_recv_wr,
				 struct ib_recv_wr** bad_recv_wr)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_resize_cq(struct ib_cq* cq, 
			     int cqe,
			     struct ib_udata* udata)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_peek_cq(struct ib_cq* ib_cq,
			   int wc_cnt)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_req_ncomp_notif(struct ib_cq* ib_cq,
				   int wc_cnt)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_query_mr(struct ib_mr* ib_mr,
			    struct ib_mr_attr* mr_attr)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static struct ib_mr* bgvrnic_alloc_fast_reg_mr(struct ib_pd* pd,
						int max_page_list_len)
{
ENTER

EXIT
	return ERR_PTR(-ENOSYS);
}


static struct ib_fast_reg_page_list* bgvrnic_alloc_fast_reg_page_list(struct ib_device* ib_dev,
									int page_list_len)
{
ENTER

EXIT
	return ERR_PTR(-ENOSYS);
}


static void bgvrnic_free_fast_reg_page_list(struct ib_fast_reg_page_list* page_list)
{
ENTER

EXIT
	return;
}


static int bgvrnic_rereg_phys_mr(struct ib_mr* mr,
				 int mr_rereg_mask,
				 struct ib_pd* pd,
				 struct ib_phys_buf* phys_buf_array,
				 int num_phys_buf,
				 int mr_access_flags,
				 u64* iova_start)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static struct ib_mw* bgvrnic_alloc_mw(struct ib_pd* pd)
{
ENTER

EXIT
	return ERR_PTR(-ENOSYS);
}


static int bgvrnic_bind_mw(struct ib_qp* qp,
			   struct ib_mw* mw,
			   struct ib_mw_bind* mw_bind)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_dealloc_mw(struct ib_mw* mw)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static struct ib_fmr* bgvrnic_alloc_fmr(struct ib_pd* ib_pd,
				 int mr_access_flags,
				 struct ib_fmr_attr* fmr_attr)
{
ENTER

EXIT
	return ERR_PTR(-ENOSYS);
}


static int bgvrnic_map_phys_fmr(struct ib_fmr* fmr,
				u64* page_list,
				int list_len,
				u64 iova)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_unmap_fmr(struct list_head* fmr_list)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_dealloc_fmr(struct ib_fmr* fmr)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_attach_mcast(struct ib_qp* qp,
				union ib_gid* gid,
				u16 lid)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_detach_mcast(struct ib_qp* ib_qp,
				union ib_gid* gid,
				u16 lid)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


static int bgvrnic_process_mad(struct ib_device* ib_device,
				int process_mad_flags,
				u8 port_num,
				struct ib_wc* in_wc,
				struct ib_grh* in_grh,
				struct ib_mad* in_mad,
				struct ib_mad* out_mad)
{
	int rc = -ENOSYS;
ENTER

EXIT
	return rc;
}


struct bgvrnic_device* __devinit bgvrnic_alloc_device(void)
{
	struct bgvrnic_device* vrnic_dev = (struct bgvrnic_device*) ib_alloc_device(sizeof(*vrnic_dev));

ENTER
	if (!vrnic_dev) {
		printk(KERN_EMERG "ib_alloc_device failed!\n");
		goto out;
	}

	memset(vrnic_dev, 0, sizeof(*vrnic_dev));
	spin_lock_init(&vrnic_dev->lock);
	vrnic_dev->ib_dev.dma_device = bg_mu_dev;
	vrnic_dev->ib_dev.owner = THIS_MODULE;
	vrnic_dev->ib_dev.node_type = RDMA_NODE_RNIC;
        spin_lock_init(&vrnic_dev->qp_pool.lock);
        idr_init(&vrnic_dev->qp_pool.idr);
	vrnic_dev->qp_pool.prev_id = 0;
	spin_lock_init(&vrnic_dev->mr_pool.lock);
	idr_init(&vrnic_dev->mr_pool.idr);
	vrnic_dev->mr_pool.prev_id = 0;
	INIT_LIST_HEAD(&vrnic_dev->ceps);
	spin_lock_init(&vrnic_dev->ceps_lock);
	INIT_LIST_HEAD(&vrnic_dev->listeners);
	spin_lock_init(&vrnic_dev->listeners_lock);
	strlcpy(vrnic_dev->ib_dev.name, BGVRNIC_NAME, sizeof(vrnic_dev->ib_dev.name));
	vrnic_dev->ib_dev.phys_port_cnt = 1;
	vrnic_dev->ib_dev.num_comp_vectors = 1;
	vrnic_dev->ib_dev.query_device = bgvrnic_query_device;
	vrnic_dev->ib_dev.query_port = bgvrnic_query_port;
	vrnic_dev->ib_dev.query_pkey = bgvrnic_query_pkey;
	vrnic_dev->ib_dev.query_gid = bgvrnic_query_gid;
	vrnic_dev->ib_dev.alloc_ucontext = bgvrnic_alloc_ucontext;
	vrnic_dev->ib_dev.dealloc_ucontext = bgvrnic_dealloc_ucontext;	
	vrnic_dev->ib_dev.alloc_pd = bgvrnic_alloc_pd;
	vrnic_dev->ib_dev.dealloc_pd = bgvrnic_dealloc_pd;
	vrnic_dev->ib_dev.create_ah = bgvrnic_create_ah;
	vrnic_dev->ib_dev.destroy_ah = bgvrnic_destroy_ah;
	vrnic_dev->ib_dev.modify_ah = bgvrnic_modify_ah;
	vrnic_dev->ib_dev.query_ah = bgvrnic_query_ah;
	vrnic_dev->ib_dev.create_qp = bgvrnic_create_qp;
	vrnic_dev->ib_dev.destroy_qp = bgvrnic_destroy_qp;
	vrnic_dev->ib_dev.query_qp = bgvrnic_query_qp;
	vrnic_dev->ib_dev.modify_qp = bgvrnic_modify_qp;
	vrnic_dev->ib_dev.create_cq = bgvrnic_create_cq;
	vrnic_dev->ib_dev.req_notify_cq = bgvrnic_req_notify_cq;
	vrnic_dev->ib_dev.poll_cq = bgvrnic_poll_cq;
	vrnic_dev->ib_dev.destroy_cq = bgvrnic_destroy_cq;
	vrnic_dev->ib_dev.resize_cq = bgvrnic_resize_cq;
	vrnic_dev->ib_dev.peek_cq = bgvrnic_peek_cq;
	vrnic_dev->ib_dev.post_send = bgvrnic_post_send;
	vrnic_dev->ib_dev.post_recv = bgvrnic_post_recv;
	vrnic_dev->ib_dev.get_dma_mr = bgvrnic_get_dma_mr;
	vrnic_dev->ib_dev.reg_phys_mr = bgvrnic_reg_phys_mr;
	vrnic_dev->ib_dev.rereg_phys_mr = bgvrnic_rereg_phys_mr;
	vrnic_dev->ib_dev.reg_user_mr = bgvrnic_reg_user_mr;
	vrnic_dev->ib_dev.dereg_mr = bgvrnic_dereg_mr;
	vrnic_dev->ib_dev.query_mr = bgvrnic_query_mr;
	vrnic_dev->ib_dev.alloc_fast_reg_mr = bgvrnic_alloc_fast_reg_mr;
	vrnic_dev->ib_dev.alloc_fast_reg_page_list = bgvrnic_alloc_fast_reg_page_list;
	vrnic_dev->ib_dev.free_fast_reg_page_list = bgvrnic_free_fast_reg_page_list;
	vrnic_dev->ib_dev.alloc_fmr = bgvrnic_alloc_fmr;
	vrnic_dev->ib_dev.map_phys_fmr = bgvrnic_map_phys_fmr;
	vrnic_dev->ib_dev.unmap_fmr = bgvrnic_unmap_fmr;
	vrnic_dev->ib_dev.dealloc_fmr = bgvrnic_dealloc_fmr;
	vrnic_dev->ib_dev.get_protocol_stats = bgvrnic_get_protocol_stats;
	vrnic_dev->ib_dev.modify_device = bgvrnic_modify_device;
	vrnic_dev->ib_dev.modify_port = bgvrnic_modify_port;
	vrnic_dev->ib_dev.mmap = bgvrnic_mmap;
	vrnic_dev->ib_dev.create_srq = bgvrnic_create_srq;
	vrnic_dev->ib_dev.destroy_srq = bgvrnic_destroy_srq;
	vrnic_dev->ib_dev.modify_srq = bgvrnic_modify_srq;
	vrnic_dev->ib_dev.query_srq = bgvrnic_query_srq;
	vrnic_dev->ib_dev.post_srq_recv = bgvrnic_post_srq_recv;
	vrnic_dev->ib_dev.req_ncomp_notif = bgvrnic_req_ncomp_notif;
	vrnic_dev->ib_dev.alloc_mw = bgvrnic_alloc_mw;
	vrnic_dev->ib_dev.bind_mw = bgvrnic_bind_mw;
	vrnic_dev->ib_dev.dealloc_mw = bgvrnic_dealloc_mw;
	vrnic_dev->ib_dev.attach_mcast = bgvrnic_attach_mcast;
	vrnic_dev->ib_dev.detach_mcast = bgvrnic_detach_mcast;
	vrnic_dev->ib_dev.process_mad = bgvrnic_process_mad;
	vrnic_dev->ib_dev.uverbs_abi_ver = 3;
	vrnic_dev->ib_dev.uverbs_cmd_mask = 
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE) |
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT) |
		(1ull << IB_USER_VERBS_CMD_QUERY_QP) |
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT) |
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD) |
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD) | 
		(1ull << IB_USER_VERBS_CMD_CREATE_QP) |
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP) |
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP) |
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ) |
		(1ull << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ) |
		(1ull << IB_USER_VERBS_CMD_POLL_CQ) |
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ) |
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
		(1ull << IB_USER_VERBS_CMD_REG_MR) |
		(1ull << IB_USER_VERBS_CMD_DEREG_MR) |
		(1ull << IB_USER_VERBS_CMD_POST_SEND) | 
		(1ull << IB_USER_VERBS_CMD_POST_RECV);

	vrnic_dev->ib_dev.iwcm = kzalloc(sizeof(*vrnic_dev->ib_dev.iwcm), GFP_KERNEL);
	if (!vrnic_dev->ib_dev.iwcm) {
		printk(KERN_EMERG "kzalloc for IWCM failed!\n");
		goto out1;
	}
	vrnic_dev->ib_dev.iwcm->add_ref = bgvrnic_add_ref;
	vrnic_dev->ib_dev.iwcm->rem_ref = bgvrnic_rem_ref;
	vrnic_dev->ib_dev.iwcm->get_qp = bgvrnic_get_qp;
	vrnic_dev->ib_dev.iwcm->accept = bgvrnic_accept;
	vrnic_dev->ib_dev.iwcm->connect = bgvrnic_connect;
	vrnic_dev->ib_dev.iwcm->reject = bgvrnic_reject;
	vrnic_dev->ib_dev.iwcm->create_listen = bgvrnic_create_listen;
	vrnic_dev->ib_dev.iwcm->destroy_listen = bgvrnic_destroy_listen;

	memset(&vrnic_dev->ib_dev_attrs, 0, sizeof(vrnic_dev->ib_dev_attrs));
	vrnic_dev->ib_dev_attrs.fw_ver = 0x000100010000;
	vrnic_dev->ib_dev_attrs.max_mr_size = 1024 * 1024 * 1024;
	vrnic_dev->ib_dev_attrs.vendor_id = 0x145e;
	vrnic_dev->ib_dev_attrs.vendor_part_id = 7;
	vrnic_dev->ib_dev_attrs.hw_ver = 1;
	vrnic_dev->ib_dev_attrs.max_qp = 16384;
	vrnic_dev->ib_dev_attrs.max_qp_wr = 8192;
	vrnic_dev->ib_dev_attrs.max_sge = vrnic_dev->ib_dev_attrs.max_sge_rd = 32;
	vrnic_dev->ib_dev_attrs.max_cq = 16384;
	vrnic_dev->ib_dev_attrs.max_cqe = vrnic_dev->ib_dev_attrs.max_qp_wr * 100;
	vrnic_dev->ib_dev_attrs.max_mr = 32768;
	vrnic_dev->ib_dev_attrs.max_pd = 32768;
	vrnic_dev->ib_dev_attrs.max_qp_rd_atom = 16;
	vrnic_dev->ib_dev_attrs.max_qp_init_rd_atom = 16;
	vrnic_dev->ib_dev_attrs.device_cap_flags = 0;
        vrnic_dev->ib_port_attrs[0].max_mtu = vrnic_dev->ib_port_attrs[0].active_mtu = IB_MTU_512;
        vrnic_dev->ib_port_attrs[0].state = IB_PORT_ACTIVE;
        vrnic_dev->ib_port_attrs[0].phys_state = 5;
        vrnic_dev->ib_port_attrs[0].port_cap_flags = IB_PORT_CM_SUP | IB_PORT_SYS_IMAGE_GUID_SUP | IB_PORT_VENDOR_CLASS_SUP;
        vrnic_dev->ib_port_attrs[0].gid_tbl_len = 1;
        vrnic_dev->ib_port_attrs[0].active_width = ib_width_enum_to_int(IB_WIDTH_1X);
        vrnic_dev->ib_port_attrs[0].active_speed = 4; 
	vrnic_dev->ib_port_attrs[0].max_vl_num = 1;
	vrnic_dev->ib_port_attrs[0].lid = _bgpers.Network_Config.Acoord;
	vrnic_dev->ib_port_attrs[0].lid <<= 4;
	vrnic_dev->ib_port_attrs[0].lid |= _bgpers.Network_Config.Bcoord;
	vrnic_dev->ib_port_attrs[0].lid <<= 4;
	vrnic_dev->ib_port_attrs[0].lid |= _bgpers.Network_Config.Ccoord;
	vrnic_dev->ib_port_attrs[0].lid <<= 4;
	vrnic_dev->ib_port_attrs[0].lid |= _bgpers.Network_Config.Dcoord;
	vrnic_dev->ib_port_attrs[0].lid++; // increment to ensure non-zero

	goto out;

out1:
	bgvrnic_dealloc_device(vrnic_dev);
	vrnic_dev = NULL;

out:
EXIT
	return vrnic_dev;
}	 


void bgvrnic_dealloc_device(struct bgvrnic_device* vrnic_dev)
{
ENTER
	if (vrnic_dev->net_dev) {
		bgvrnic_free_netdev(vrnic_dev->net_dev);
		vrnic_dev->net_dev = NULL;
	}

	if (vrnic_dev->ib_dev.iwcm) {
		kfree(vrnic_dev->ib_dev.iwcm);
		vrnic_dev->ib_dev.iwcm = NULL;
	}
	ib_dealloc_device(&vrnic_dev->ib_dev);

EXIT
	return;
}
