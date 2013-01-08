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


#ifndef BGVRNIC_H
#define BGVRNIC_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/kthread.h>

#include <rdma/ib_verbs.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_umem.h>

#include <firmware/include/personality.h>
#include <spi/include/kernel/trace.h>
#include <mudm/include/mudm.h> 


#ifdef DEBUG
#define ENTER printk(KERN_DEBUG "->%s():%d\n", __FUNCTION__, __LINE__);
#define EXIT printk(KERN_DEBUG "<-%s():%d\n", __FUNCTION__, __LINE__);
#else
#define ENTER
#define EXIT
#endif


#define PHYSICAL_MEMORY_SIZE (num_physpages * PAGE_SIZE)


struct bgvrnic_id_pool {
	struct idr	idr;
	spinlock_t	lock;
	int 		prev_id;
};


struct bgvrnic_device {
	struct ib_device 	ib_dev;
	spinlock_t 		lock;
	struct net_device*	net_dev;
	struct ib_device_attr	ib_dev_attrs;
#define BGVRNIC_IB_PORT_MAX 1
	struct ib_port_attr	ib_port_attrs[BGVRNIC_IB_PORT_MAX];
	struct bgvrnic_id_pool  qp_pool;
	struct bgvrnic_id_pool	pd_pool;
	struct bgvrnic_id_pool	cq_pool;
	struct bgvrnic_id_pool	mr_pool;
	struct mudm_cfg_info	mu_cfg_info;
	void*			mu_context;
	struct task_struct*     mu_poll_thread[2];
	struct list_head	ceps; 
	spinlock_t		ceps_lock;
	struct list_head	listeners;
	spinlock_t		listeners_lock;
};


struct bgvrnic_cqe {
	struct ib_wc	 ib_wc;
	struct list_head list;
};


struct bgvrnic_cq {
	struct ib_cq		ib_cq;
	spinlock_t 		lock;
	atomic_t		references;
	int		   	cqn;
	wait_queue_head_t	waitq;
	struct list_head	entries;
	enum ib_cq_notify_flags flags;	
};

				
struct bgvrnic_pd {
        struct ib_pd 	ib_pd;
        u32 		pdn;
};


struct bgvrnic_mr {
	struct ib_mr 		ib_mr;
	u64			start;
	u64			length;
	struct bgvrnic_pd* 	pd;
	struct ib_umem* 	umem;
	u32			sge_num;
	struct mudm_sgl*	sge;
};


enum bgvrnic_wqe_state {
	BGVRNIC_WQES_INIT = 0xcafebabe,
	BGVRNIC_WQES_WAIT,
	BGVRNIC_WQES_RUN,
	BGVRNIC_WQES_DONE,
	BGVRNIC_WQES_INVALID
};

enum bgvrnic_wqe_type {
	BGVRNIC_WQET_SEND = 0xdeadbeef,
	BGVRNIC_WQET_RECV,
	BGVRNIC_WQET_RDMA
};


struct bgvrnic_wqe {
	struct bgvrnic_qp*      vrnic_qp;
	struct list_head        list;
	enum bgvrnic_wqe_state  state;
	u32                     length;
	int                     status;
	enum bgvrnic_wqe_type	type;
	union {
		struct ib_send_wr ib_send;
		struct ib_recv_wr ib_recv;
	} wr;
	int 			imm_data_valid;
	u32 			imm_data;
};

enum bgvrnic_cep_state {
	BGVRNIC_CEP_INIT,
	BGVRNIC_CEP_CONNECTING,
	BGVRNIC_CEP_CONNECTED,
	BGVRNIC_CEP_DISCONNECTED
};

struct bgvrnic_cep {
	struct list_head        list;
	struct bgvrnic_qp*      vrnic_qp;
	struct bgvrnic_device*	vrnic_dev;
	void*                   mu_context;
	u16			remote_qpn;
	u32			remote_addr;	
	u64			remote_request_id;
	u16			source_qpn;
	enum bgvrnic_cep_state	state;
};

struct bgvrnic_listener {
	struct list_head	list;
	struct iw_cm_id* 	cm_id;
};

struct bgvrnic_qp {
	struct ib_qp 		ib_qp;
	struct ib_qp_attr	ib_qp_attrs;
	struct ib_qp_init_attr	ib_qp_init_attrs;
	struct bgvrnic_device*  vrnic_dev;
	struct iw_cm_id*	cm_id;
	struct bgvrnic_cep*	cep;
	void*			mu_context;
	spinlock_t		lock;
	atomic_t		references;
	wait_queue_head_t	waitq;
	struct bgvrnic_pd*	pd;
	struct bgvrnic_cq*	scq;
	struct bgvrnic_cq*	rcq;
	struct list_head	sq;
	spinlock_t		sq_lock;
	struct list_head 	rq;
	spinlock_t		rq_lock;
};


	

extern struct bgvrnic_device* __devinit bgvrnic_alloc_device(void);
extern void  bgvrnic_dealloc_device(struct bgvrnic_device*);
extern struct bgvrnic_mr* bgvrnic_get_mr(struct bgvrnic_device*, u32);
extern struct net_device* __devinit bgvrnic_alloc_netdev(struct bgvrnic_device*, const char*, const char*);
extern void bgvrnic_free_netdev(struct net_device*);
extern int bgvrnic_rx(struct net_device* net_dev, u8* data, u32 data_len);
extern int bgvrnic_process_recv_wqes(struct bgvrnic_qp* vrnic_qp);

/* MU callbacks */
extern int mu_recv_cb(char* data, void* cb_context);
extern int mu_connect_cb(void* mu_context, struct ionet_header* msg, void* cb_context); 
extern int mu_status_cb(void* requestID[], u32 status[], void* cb_context, u32 error_return[], u32 num_request_ids);
extern int mu_allocate_cb(struct mudm_memory_region* memory, size_t length, void* cb_context);
extern int mu_free_cb(struct mudm_memory_region* memory, void* cb_context);

/* iWarp CM callbacks */
extern void bgvrnic_add_ref(struct ib_qp* qp);
extern void bgvrnic_rem_ref(struct ib_qp* qp);
extern struct ib_qp* bgvrnic_get_qp(struct ib_device* ib_dev, int qpn);
extern int bgvrnic_connect(struct iw_cm_id* cm_id, struct iw_cm_conn_param* conn_param);
extern int bgvrnic_accept(struct iw_cm_id* cm_id, struct iw_cm_conn_param* conn_param);
extern int bgvrnic_reject(struct iw_cm_id* cm_id, const void* data, u8 data_len);
extern int bgvrnic_create_listen(struct iw_cm_id* cm_id, int backlog);
extern int bgvrnic_destroy_listen(struct iw_cm_id* cm_id);


extern int vrnic_debug;
extern int eth_debug;

extern BG_FlightRecorderRegistry_t vrnic_log;
extern BG_FlightRecorderLog_t vrnic_log_entry[];
extern u64 vrnic_log_lock;
#define FLIGHTPRINT(name, format) { #name, FLIGHTRECORDER_PRINTF, format, NULL, NULL},  
#define FLIGHTFUNCT(name, function) { #name, FLIGHTRECORDER_FUNC, NULL, function, NULL},  
#define FLIGHTLOG &vrnic_log_lock, vrnic_log_entry, 1024 

enum bgvrnic_flight_recorder {  /* want FL_9_characters */
	FL_INVALID,
	FL_INIT_DONE,
        FL_CONN_REQT, 
        FL_CONN_RPLY,
        FL_CONN_DISC,
        FL_CMCONNECT,
        FL_CM_ACCEPT,
        FL_CM_REJECT,
        FL_CRT_LISTN,
        FL_DST_LISTN, 
        FL_STAT_SEND, 
        FL_STAT_RECV,
        FL_STAT_RDMA,
	FL_BUG_VERBS,
	FL_LINK_RSET,
	FL_MODIFY_QP,
	FL_POST_RECV,
	FL_TAKE_BUFF,
	FL_NO_BUFFER,
	FL_ADD_CQE,
};


static inline int bgvrnic_add_cqe(struct bgvrnic_cq* vrnic_cq,
				  struct bgvrnic_qp* vrnic_qp,
				  struct ib_send_wr* send_wr,
				  struct ib_recv_wr* recv_wr,
				  u32 length,
				  int status,
				  int imm_data_valid,
				  u32 imm_data)
{
	int rc = 0;
	int generate_event = 1;
	struct ib_cq* ib_cq = &vrnic_cq->ib_cq;
	struct bgvrnic_cqe* cqe;
ENTER

	cqe = kmalloc(sizeof(*cqe), GFP_KERNEL);
	if (cqe) {
		memset(&cqe->ib_wc, 0, sizeof(cqe->ib_wc));
		if (likely(status == 0)) {
			cqe->ib_wc.status = IB_WC_SUCCESS;
			cqe->ib_wc.vendor_err = 0;
		} else if (status < 0) {
			cqe->ib_wc.status = IB_WC_GENERAL_ERR;
			cqe->ib_wc.vendor_err = status;
		} else {
			cqe->ib_wc.status = status;
			cqe->ib_wc.vendor_err = 0; 
		}
		cqe->ib_wc.byte_len = length;
		cqe->ib_wc.qp = &vrnic_qp->ib_qp;
		cqe->ib_wc.src_qp = vrnic_qp->ib_qp.qp_num;
		//cqe->ib_wc.csum_ok = 1;	
		if (imm_data) {
			cqe->ib_wc.wc_flags = IB_WC_WITH_IMM;
			cqe->ib_wc.ex.imm_data = imm_data;
		}
			
		if (send_wr) {
			switch (send_wr->opcode) {
				case IB_WR_RDMA_WRITE:
				case IB_WR_RDMA_WRITE_WITH_IMM:
					cqe->ib_wc.opcode = IB_WC_RDMA_WRITE;
					break;

				case IB_WR_SEND:
				case IB_WR_SEND_WITH_IMM:
				case IB_WR_SEND_WITH_INV:
					cqe->ib_wc.opcode = IB_WC_SEND;
					break;

				case IB_WR_RDMA_READ:
				case IB_WR_RDMA_READ_WITH_INV:
					cqe->ib_wc.opcode = IB_WC_RDMA_READ;
					break;

				default:
					BUG();
			}
			cqe->ib_wc.wr_id = send_wr->wr_id;

			generate_event = (cqe->ib_wc.status != IB_WC_SUCCESS) || (vrnic_cq->flags & IB_CQ_NEXT_COMP);
		} else if (recv_wr) {
			cqe->ib_wc.opcode = IB_WC_RECV;
			cqe->ib_wc.wr_id = recv_wr->wr_id;
		}
		else
			BUG(); 

		/* Add CQE to the CQ */
		spin_lock(&vrnic_cq->lock);
		Kernel_WriteFlightLog(FLIGHTLOG, FL_ADD_CQE, (u64) vrnic_qp, (u64) cqe, cqe->ib_wc.opcode, cqe->ib_wc.status);
		list_add_tail(&cqe->list, &vrnic_cq->entries);
		vrnic_cq->flags = 0;
		spin_unlock(&vrnic_cq->lock);

		if (generate_event) {
			BUG_ON(!ib_cq->comp_handler);
			(ib_cq->comp_handler)(ib_cq, ib_cq->cq_context);
		}
        } else {
		printk(KERN_EMERG "%s:%d - Failure allocating CQE.\n", __func__, __LINE__);
		rc = -ENOMEM;
        }

EXIT
	return rc;
}
 

static inline int bgvrnic_xlate_sgl(struct bgvrnic_device* vrnic_dev,
					struct ib_sge* usge,
					int num_usge,
					struct mudm_sgl* psge,
					int* num_psge)
{
	int ui = 0;
	int pi = 0;
	int total_length = 0;

ENTER
	for (; ui < num_usge; ui++) {
		u32 mi = 0;
		u32 bytes_to_map = usge[ui].length;
		struct bgvrnic_mr* vrnic_mr = bgvrnic_get_mr(vrnic_dev, usge[ui].lkey); 
		u64 mr_offset;

		if (!vrnic_mr)
			return -ENOMEM;

		mr_offset = (u64) usge[ui].addr - vrnic_mr->start;
		total_length += usge[ui].length;

		// Find the first physical SGE that specifies the start of the user memory.
		while (mr_offset && mi < vrnic_mr->sge_num) {
			if (mr_offset >= vrnic_mr->sge[mi].memlength) {
				mr_offset -= vrnic_mr->sge[mi].memlength;
				mi++;
			} else
				break;
		}
		BUG_ON(mi >= vrnic_mr->sge_num);

		psge[pi].memlength = vrnic_mr->sge[mi].memlength - mr_offset; 
		psge[pi].physicalAddr = vrnic_mr->sge[mi++].physicalAddr + mr_offset;
		if (psge[pi].memlength > bytes_to_map) {
			psge[pi].memlength = bytes_to_map;
			pi++;
			bytes_to_map = 0;
		} else {
			bytes_to_map -= psge[pi++].memlength;

			while (bytes_to_map && pi < *num_psge && mi < vrnic_mr->sge_num) {
				psge[pi].physicalAddr = vrnic_mr->sge[mi].physicalAddr; 
				if (vrnic_mr->sge[mi].memlength > bytes_to_map) {
					psge[pi].memlength = bytes_to_map;
					bytes_to_map = 0;
				} else {
					psge[pi].memlength = vrnic_mr->sge[mi].memlength;
					bytes_to_map -= vrnic_mr->sge[mi].memlength;
				}

				pi++;
				mi++;
			}
			BUG_ON(mi > vrnic_mr->sge_num);
		}

		if (pi >= *num_psge) {
			total_length = -E2BIG;
			goto out;
		}
	}

	*num_psge = pi;

out:

EXIT		
	return total_length;
}	
		

static inline struct bgvrnic_wqe* bgvrnic_alloc_wqe(enum bgvrnic_wqe_type type,
						    void* wr,
						    struct bgvrnic_qp* vrnic_qp)
{
	struct bgvrnic_wqe* wqe = (struct bgvrnic_wqe*) kmalloc(sizeof(*wqe), GFP_KERNEL);

	if (wqe) {
		wqe->state = BGVRNIC_WQES_INIT;
		wqe->type = type;
		wqe->vrnic_qp = vrnic_qp;
		wqe->status = 0;
		wqe->imm_data_valid = wqe->imm_data = 0;

		if (type == BGVRNIC_WQET_SEND) {
			struct ib_send_wr* ib_wr = (struct ib_send_wr*) wr;

			wqe->wr.ib_send = *ib_wr;
			wqe->wr.ib_send.sg_list = (struct ib_sge*) kmalloc(sizeof(struct ib_sge) * ib_wr->num_sge, GFP_KERNEL);
			if (wqe->wr.ib_send.sg_list)
				memcpy(wqe->wr.ib_send.sg_list, ib_wr->sg_list, sizeof(struct ib_sge) * ib_wr->num_sge);
			else {
				kfree(wqe);
				wqe = NULL;
			}
			wqe->length = 0;
		} else if (type == BGVRNIC_WQET_RECV) {
			struct ib_recv_wr* ib_wr = (struct ib_recv_wr*) wr;

			wqe->wr.ib_recv = *ib_wr;
			wqe->wr.ib_recv.sg_list = (struct ib_sge*) kmalloc(sizeof(struct ib_sge) * ib_wr->num_sge, GFP_KERNEL);
			if (wqe->wr.ib_recv.sg_list) {
				memcpy(wqe->wr.ib_recv.sg_list, ib_wr->sg_list, sizeof(struct ib_sge) * ib_wr->num_sge);
			} else {
				kfree(wqe);
				wqe = NULL;
			}
                	wqe->length = -1;
		} else if (type == BGVRNIC_WQET_RDMA)
			wqe->length = 0;
		else
			BUG();
	}

	return wqe;
}


static inline void bgvrnic_free_wqe(struct bgvrnic_wqe* wqe)
{
	if (wqe->type == BGVRNIC_WQET_SEND)
		kfree(wqe->wr.ib_send.sg_list);
	else if (wqe->type == BGVRNIC_WQET_RECV)
		kfree(wqe->wr.ib_recv.sg_list);
	wqe->state = BGVRNIC_WQES_INVALID;
	kfree(wqe);

	return;
}

#endif /* BGVRNIC_H */
