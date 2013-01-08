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
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/in.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <asm/io.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>

#include <mudm/include/mudm.h>
#include <mudm/include/mudm_inlines.h>

#include "bgvrnic.h"


/*
 * Called when there is an incoming connection request or connection reply.
 */
int mu_connect_cb(void* mu_context,
		  struct ionet_header* msg, 
		  void* cb_context)
{
	int rc = 0;
	struct iw_cm_event e;
	struct bgvrnic_cep* cep = NULL;
	struct bgvrnic_device* vrnic_dev = (struct bgvrnic_device*) cb_context;

ENTER
	if (msg->type == MUDM_CONN_REQUEST) {
		int found = 0;
		struct bgvrnic_listener* listener;
		struct bgvrnic_listener* tmp;
		struct ionet_connect* req = (struct ionet_connect*) msg;
		struct iw_cm_id* cm_id = NULL;
		u32 address = *((u32*) req->dest_IP_addr);

		Kernel_WriteFlightLog(FLIGHTLOG, FL_CONN_REQT, (u64) msg, address, 0, 0);

		spin_lock(&vrnic_dev->listeners_lock);	
		list_for_each_entry_safe(listener, tmp, &vrnic_dev->listeners, list) {
			cm_id = listener->cm_id;
			if ((listener->cm_id->local_addr.sin_addr.s_addr == INADDR_ANY || 
			    address == listener->cm_id->local_addr.sin_addr.s_addr) && 
			    req->dest_port == listener->cm_id->local_addr.sin_port)   {
				cep = kmalloc(sizeof(*cep), GFP_KERNEL);
				if (!cep) {
					printk(KERN_EMERG "%s%d - Failure allocating new CEP.\n", __func__, __LINE__);
					rc = -ENOMEM;
					spin_unlock(&vrnic_dev->listeners_lock);
					goto out;
				}

				found = 1;
				cep->remote_request_id = req->source_request_id;
				cep->vrnic_qp = NULL; 
				cep->vrnic_dev = vrnic_dev;
				cep->mu_context = mu_context;
				cep->remote_addr = msg->torus_source_node;
				cep->remote_qpn = msg->dest_qpn; 
				cep->source_qpn = msg->source_qpn;
				cep->state = BGVRNIC_CEP_CONNECTING;
				spin_lock(&vrnic_dev->ceps_lock);
				list_add_tail(&cep->list, &vrnic_dev->ceps);
				spin_unlock(&vrnic_dev->ceps_lock);
	
				break;
			}
		}
		spin_unlock(&vrnic_dev->listeners_lock);

		/* If we have a CM ID then send CM event. */
		if (cm_id && found) {
			e.event = IW_CM_EVENT_CONNECT_REQUEST;
			e.status = 0; 
			if (cm_id->local_addr.sin_addr.s_addr == INADDR_ANY) {
				e.local_addr.sin_addr.s_addr = inet_select_addr(vrnic_dev->net_dev, 0, RT_SCOPE_UNIVERSE);
				e.local_addr.sin_port = req->dest_port;
			} else
				e.local_addr = cm_id->local_addr; 
			e.remote_addr.sin_addr.s_addr = *((u32*) req->source_IP_addr);
			e.remote_addr.sin_port = req->source_port;
			if (req->private_data_length) {
				e.private_data = req->private_data;
				e.private_data_len = req->private_data_length;
			} else {
				e.private_data = NULL; 
				e.private_data_len = 0;
			}
			e.provider_data = (void*) cep; 
			rc = cm_id->event_handler(cm_id, &e);
			if (rc) 
				printk(KERN_EMERG "%s:%d - CM event type %d failed.  rc=%d\n", __func__, __LINE__, e.event, rc);
		} else {
			/* No CM ID so echo this request back to the remote node. */
			int rc = 0;
			struct ionet_connect rsp;

			rsp.ionet_hdr.source_qpn = msg->dest_qpn;
			rsp.ionet_hdr.dest_qpn = msg->source_qpn;
			memset(rsp.source_IP_addr, 0, sizeof(rsp.source_IP_addr));
			*((u32*) rsp.source_IP_addr) = *((u32*) req->dest_IP_addr); 
			rsp.source_port = req->dest_port; 
			memset(rsp.dest_IP_addr, 0, sizeof(rsp.dest_IP_addr));
			*((u32*) rsp.dest_IP_addr) = *((u32*) req->source_IP_addr);
			rsp.dest_port = req->source_port; 
			rsp.source_request_id = req->source_request_id; 
			rsp.status = -ECONNREFUSED;
			rc = mudm_conn_reply(mu_context, &rsp, NULL, 0);
			if (rc)
				printk(KERN_EMERG "%s:%d - mudm_conn_reply failed rc=%d\n", __func__, __LINE__, rc);
		
			printk(KERN_EMERG "%s:%d - No listener found for connection event.\n", __func__, __LINE__);
		}
	} else if (msg->type == MUDM_CONN_REPLY) {
		int found = 0;
		struct bgvrnic_cep* cep = NULL;
		struct bgvrnic_cep* tmp;
		struct ionet_connect* req = (struct ionet_connect*) msg;

		Kernel_WriteFlightLog(FLIGHTLOG, FL_CONN_RPLY, (u64) msg, req->status, msg->source_qpn, msg->dest_qpn);

		spin_lock(&vrnic_dev->ceps_lock);
		list_for_each_entry_safe(cep, tmp, &vrnic_dev->ceps, list) {
			if ((u64) cep->remote_request_id == req->source_request_id) {
				found = 1;
				e.event = IW_CM_EVENT_CONNECT_REPLY;
				e.status = req->status;
				e.local_addr.sin_addr.s_addr = *((u32*) req->dest_IP_addr);
				e.local_addr.sin_port = req->dest_port;
				e.remote_addr.sin_addr.s_addr = *((u32*) req->source_IP_addr);
				e.remote_addr.sin_port = req->source_port;	
				if (req->private_data_length) {
					e.private_data = req->private_data;
					e.private_data_len = req->private_data_length;
				} else {
					e.private_data = NULL;
					e.private_data_len = 0;
				}
				e.provider_data = cep;
				BUG_ON(!cep->vrnic_qp);
				BUG_ON(!cep->vrnic_qp->cm_id);
				cep->vrnic_qp->ib_qp_attrs.dest_qp_num = msg->dest_qpn;
				rc = cep->vrnic_qp->cm_id->event_handler(cep->vrnic_qp->cm_id, &e);
				if (rc) {
					printk(KERN_EMERG "%s:%d - CM event type %d failed.  rc=%d\n", __func__, __LINE__, e.event, rc);
				} else {
					cep->state = BGVRNIC_CEP_CONNECTED;
					cep->vrnic_qp->ib_qp_attrs.qp_state = IB_QPS_RTS; 
				}

				/* Don't need this CEP any longer */
				cep->vrnic_qp->cm_id->provider_data = NULL;
				bgvrnic_rem_ref(&cep->vrnic_qp->ib_qp);
				cep->vrnic_qp = NULL;
				list_del(&cep->list);
				kfree(cep);

				break;
			}
		}
		spin_unlock(&vrnic_dev->ceps_lock);

		if (!found) {
			rc = -EINVAL;
			printk(KERN_EMERG "couldn't find CEP\n");
		}
	} else if (msg->type == MUDM_DISCONNECTED) {
		struct iw_cm_event e;
		struct ib_qp* ib_qp = bgvrnic_get_qp(&vrnic_dev->ib_dev, msg->dest_qpn);
		struct bgvrnic_qp* vrnic_qp;

		if (!ib_qp)
			goto out;

		vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);
		if (vrnic_qp && vrnic_qp->cm_id /* && vrnic_qp->ib_qp_attrs.qp_state >= IB_QPS_RTS*/) {

			Kernel_WriteFlightLog(FLIGHTLOG, FL_CONN_DISC, (u64) msg, msg->source_qpn, msg->dest_qpn, vrnic_qp->ib_qp_attrs.qp_state);

			/* Send disconnect and close events */
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
				vrnic_qp->cm_id->provider_data = NULL;
				vrnic_qp->cm_id->rem_ref(vrnic_qp->cm_id);
				vrnic_qp->cm_id = NULL;
			}
		}
	} else
		printk(KERN_EMERG "%s:%d - Unknown connect message type %d\n", __func__, __LINE__, msg->type);

out:

EXIT
	return rc;
}


void bgvrnic_add_ref(struct ib_qp* ib_qp)
{
	struct bgvrnic_qp* vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);
ENTER
	atomic_inc(&vrnic_qp->references);	
EXIT
	return;
}
	


void bgvrnic_rem_ref(struct ib_qp* ib_qp)
{
	struct bgvrnic_qp* vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);
ENTER
	if (atomic_dec_and_test(&vrnic_qp->references))
		wake_up(&vrnic_qp->waitq);

EXIT
	return;
}


struct ib_qp* bgvrnic_get_qp(struct ib_device* ib_dev,
			  int qpn)
{
	unsigned long flags;
	struct bgvrnic_qp* vrnic_qp;
	struct bgvrnic_device* vrnic_dev = container_of(ib_dev, struct bgvrnic_device, ib_dev);	
ENTER
	spin_lock_irqsave(&vrnic_dev->qp_pool.lock, flags);
	vrnic_qp = idr_find(&vrnic_dev->qp_pool.idr, qpn);
	spin_unlock_irqrestore(&vrnic_dev->qp_pool.lock, flags);
EXIT
	return vrnic_qp ? &vrnic_qp->ib_qp : NULL;
}


int bgvrnic_connect(struct iw_cm_id* cm_id,
		 struct iw_cm_conn_param* conn_param)
{
	int rc = 0;
	u8* addr;
	struct ib_qp* ib_qp;
	struct bgvrnic_qp* vrnic_qp;
	u32 dest_node;
	struct ionet_connect req;
	struct bgvrnic_cep* cep = kzalloc(sizeof(*cep), GFP_KERNEL);

ENTER

	if (!cep) {
		printk(KERN_EMERG "%s:%d - Connection endpoint allocation failed.\n", __func__, __LINE__);
		rc = -ENOMEM;
		goto out;
	}

	if (conn_param->private_data_len > MUDM_MAX_PRIVATE_DATA) {
                rc = -EINVAL;
		goto out1;
	}

	ib_qp = bgvrnic_get_qp(cm_id->device, conn_param->qpn);
	if (!ib_qp) {
		rc = -EINVAL;
		goto out1;
	}

	vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);
	Kernel_WriteFlightLog(FLIGHTLOG, FL_CMCONNECT, (u64) cm_id, (u64) vrnic_qp, conn_param->qpn, conn_param->private_data_len);
	cm_id->add_ref(cm_id);
	vrnic_qp->cm_id = cm_id;

	/* Compute destination node from IP address.  The format is 10.nnppaaaa.bbbbcccc.ddddeeee where 
           aaaa, bbbb, cccc, dddd, eeee are relative torus coordinates.  nn specifies the node type where
	   01 = ION and 10 = CN. pp specifies the port. */
	addr = (u8*) &cm_id->remote_addr.sin_addr.s_addr;
	dest_node = mudm_dest32(addr[1] & 0xf, addr[2] >> 4, addr[2] & 0xf, addr[3] >> 4, addr[3] & 0xf, 
				(addr[1] & 0xc0) == 0x80 ? MUDM_IS_CN : MUDM_IS_IONODE);

	/* Create the connection endpoint and add it to the list. */
	bgvrnic_add_ref(ib_qp);
	cep->vrnic_qp = vrnic_qp;
	cep->vrnic_dev = vrnic_qp->vrnic_dev;
	cep->remote_request_id = (u64) vrnic_qp;
	cep->source_qpn = conn_param->qpn;
	cep->remote_qpn = MUDM_UNKNOWN_QPN;
	cep->mu_context = NULL; 
	cep->remote_addr = dest_node;
	cep->state = BGVRNIC_CEP_CONNECTING;
	spin_lock(&vrnic_qp->vrnic_dev->ceps_lock);
	list_add_tail(&cep->list, &vrnic_qp->vrnic_dev->ceps);
	cm_id->provider_data = cep;
	spin_unlock(&vrnic_qp->vrnic_dev->ceps_lock);
	
	// Send connection request.
	req.ionet_hdr.source_qpn = conn_param->qpn; 
	req.ionet_hdr.dest_qpn = MUDM_UNKNOWN_QPN; 
	memset(req.source_IP_addr, 0, sizeof(req.source_IP_addr));
	*((u32*) req.source_IP_addr) = cm_id->local_addr.sin_addr.s_addr;
	req.source_port = cm_id->local_addr.sin_port; 
	memset(req.dest_IP_addr, 0, sizeof(req.dest_IP_addr));
	*((u32*) req.dest_IP_addr) = cm_id->remote_addr.sin_addr.s_addr;
	req.dest_port = cm_id->remote_addr.sin_port;
	req.source_request_id = (u64) vrnic_qp; 
	req.status = 0;
	rc = mudm_connect(vrnic_qp->vrnic_dev->mu_context, &vrnic_qp->mu_context, dest_node, &req, 
			  (char*) conn_param->private_data, conn_param->private_data_len);
	if (rc) {
		printk(KERN_EMERG "%s:%d - mudm_connect returned %d\n", __func__, __LINE__, rc);
		cm_id->provider_data = NULL;
		cm_id->rem_ref(cm_id);
		vrnic_qp->cm_id = NULL;
		bgvrnic_rem_ref(ib_qp);
		spin_lock(&vrnic_qp->vrnic_dev->ceps_lock);
		list_del(&cep->list);
		spin_unlock(&vrnic_qp->vrnic_dev->ceps_lock);
	} else
		goto out;
out1:
	kfree(cep);
out:

EXIT
	return rc;
}



int bgvrnic_accept(struct iw_cm_id* cm_id, 
		struct iw_cm_conn_param* conn_param)
{
	int rc = 0;
	struct ib_qp* ib_qp = bgvrnic_get_qp(cm_id->device, conn_param->qpn);
	struct bgvrnic_cep* cep = (struct bgvrnic_cep*) cm_id->provider_data;
	struct ionet_connect rsp;
ENTER

	if (conn_param->private_data_len > MUDM_MAX_PRIVATE_DATA) {
		rc = -EINVAL;
		goto out1;
	}

	if (!ib_qp) {
		rc = -EINVAL;
		goto out1;
	}

	BUG_ON(!cep);
	Kernel_WriteFlightLog(FLIGHTLOG, FL_CM_ACCEPT, (u64) cm_id, (u64) cep->vrnic_qp, conn_param->qpn, cep->remote_qpn);

	cep->vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);
	cep->vrnic_qp->cm_id = cm_id;
	cm_id->add_ref(cm_id);
	cep->vrnic_qp->ib_qp_attrs.qp_state = IB_QPS_RTS;	
	cep->vrnic_qp->ib_qp_attrs.dest_qp_num = cep->remote_qpn;
	cep->vrnic_qp->mu_context = cep->mu_context;
	cep->state = BGVRNIC_CEP_CONNECTED;
	rsp.ionet_hdr.source_qpn = conn_param->qpn;
	rsp.ionet_hdr.dest_qpn = cep->source_qpn;
	memset(rsp.source_IP_addr, 0, sizeof(rsp.source_IP_addr));
	*((u32*) rsp.source_IP_addr) = cm_id->local_addr.sin_addr.s_addr;
	rsp.source_port = cm_id->local_addr.sin_port;
	memset(rsp.dest_IP_addr, 0, sizeof(rsp.dest_IP_addr));
	*((u32*) rsp.dest_IP_addr) = cm_id->remote_addr.sin_addr.s_addr;
	rsp.dest_port = cm_id->remote_addr.sin_port;
	rsp.source_request_id = (u64) cep->remote_request_id;
	rsp.status = 0;
	rc = mudm_conn_reply(cep->mu_context, &rsp, 
			     (char*) conn_param->private_data, (uint16_t) conn_param->private_data_len);
	if (rc) {
		printk(KERN_EMERG "mudm_conn_reply failed! rc=%d\n", rc);
		goto out2;
	} else {
                struct iw_cm_event e;

                e.event = IW_CM_EVENT_ESTABLISHED;
                e.status = 0; 
                e.local_addr = cm_id->local_addr;
                e.remote_addr = cm_id->remote_addr;
                e.private_data = (void*) conn_param->private_data;
                e.private_data_len = conn_param->private_data_len;
                e.provider_data = NULL; //cep;
                rc = cm_id->event_handler(cm_id, &e);
		if (rc) {
			printk(KERN_EMERG "%s:%d - CM event type %d failed.  rc=%d\n", __func__, __LINE__, e.event, rc);
			goto out2;
		}
        }
 

	goto out;

out2:
	cep->vrnic_qp->cm_id = NULL;	
	cm_id->rem_ref(cm_id);
out1:
	if (cep) {
		spin_lock(&cep->vrnic_dev->ceps_lock);
		list_del(&cep->list);
		spin_unlock(&cep->vrnic_dev->ceps_lock);
		cm_id->provider_data = NULL;
		kfree(cep);
	}
out:
EXIT
	return rc;
}


int bgvrnic_reject(struct iw_cm_id* cm_id,
		const void* private_data, 
		u8 private_data_len)
{
	int rc = 0;
	struct ionet_connect rsp;
	struct bgvrnic_cep* cep = (struct bgvrnic_cep*) cm_id->provider_data;

ENTER
	if (private_data_len > MUDM_MAX_PRIVATE_DATA) {
		printk(KERN_EMERG "%s:%d - private_data_len of %u exceeds maximum of %u.\n", __func__, __LINE__, private_data_len, MUDM_MAX_PRIVATE_DATA);
		rc = -EINVAL;
		goto out;
	}

	BUG_ON(!cep);
	Kernel_WriteFlightLog(FLIGHTLOG, FL_CM_REJECT, (u64) cm_id, cep->source_qpn, cep->remote_qpn, private_data_len); 

	cep->state = BGVRNIC_CEP_DISCONNECTED;
	rsp.ionet_hdr.source_qpn = cep->source_qpn;
	rsp.ionet_hdr.dest_qpn = cep->remote_qpn;
	memset(rsp.source_IP_addr, 0, sizeof(rsp.source_IP_addr));
	*((u32*) rsp.source_IP_addr) = cm_id->local_addr.sin_addr.s_addr;
	rsp.source_port = cm_id->local_addr.sin_port;
	memset(rsp.dest_IP_addr, 0, sizeof(rsp.dest_IP_addr));
	*((u32*) rsp.dest_IP_addr) = cm_id->remote_addr.sin_addr.s_addr;
	rsp.dest_port = cm_id->remote_addr.sin_port;
	rsp.source_request_id = (u64) cep->remote_request_id;
	rsp.status = -ECONNREFUSED;
	rc = mudm_conn_reply(cep->mu_context, &rsp, (char*) private_data, (uint16_t) private_data_len);
	if (rc) 
		printk(KERN_EMERG "%s:%d - reject failed rc=%d\n", __func__, __LINE__, rc);

out:
	if (cep) {
		spin_lock(&cep->vrnic_dev->ceps_lock);
		list_del(&cep->list);
		spin_unlock(&cep->vrnic_dev->ceps_lock);
		kfree(cep);
		cm_id->provider_data = NULL;
	}
EXIT
	return rc;
}


int bgvrnic_create_listen(struct iw_cm_id* cm_id, 
		       int backlog)
{
	int rc = 0;
	struct bgvrnic_listener* listener = kmalloc(sizeof(*listener), GFP_KERNEL);	
	struct bgvrnic_device* vrnic_dev = container_of(cm_id->device, struct bgvrnic_device, ib_dev);
ENTER
	if (!listener) {
		rc = -ENOMEM;
		goto out;
	}

	Kernel_WriteFlightLog(FLIGHTLOG, FL_CRT_LISTN, (u64) cm_id, backlog, 0, 0); 

	cm_id->add_ref(cm_id);
	cm_id->provider_data = NULL;
	listener->cm_id = cm_id;
	spin_lock(&vrnic_dev->listeners_lock);
	list_add_tail(&listener->list, &vrnic_dev->listeners);	
	spin_unlock(&vrnic_dev->listeners_lock);

out:

EXIT
	return rc;
}


int bgvrnic_destroy_listen(struct iw_cm_id* cm_id)
{
	int rc = 0;
	struct bgvrnic_listener* listener;
	struct bgvrnic_listener* tmp;
	struct bgvrnic_device* vrnic_dev = container_of(cm_id->device, struct bgvrnic_device, ib_dev);
	
ENTER

	Kernel_WriteFlightLog(FLIGHTLOG, FL_DST_LISTN, (u64) cm_id, 0, 0, 0);

	spin_lock(&vrnic_dev->listeners_lock);
	list_for_each_entry_safe(listener, tmp, &vrnic_dev->listeners, list) {
		if (listener->cm_id == cm_id) {
			cm_id->provider_data = NULL;
			cm_id->rem_ref(cm_id);
			list_del(&listener->list);
		}
	}	
	spin_unlock(&vrnic_dev->listeners_lock);
EXIT
	return rc;
}



