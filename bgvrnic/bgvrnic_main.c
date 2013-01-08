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


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>

#include <asm/bluegene.h>

#include <firmware/include/personality.h>
#include <firmware/include/mailbox.h>

#include <mudm/include/mudm.h>

#include "bgvrnic.h"


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Andrew Tauferner <ataufer@us.ibm.com>");
MODULE_DESCRIPTION("IBM Blue Gene/Q OFED driver");
MODULE_VERSION("1");

int vrnic_debug = 0;
module_param(vrnic_debug, int, 0);
MODULE_PARM_DESC(vrnic_debug, "Debug level (0=off, 1=some, 2=more, ...).");

int eth_debug = 0;
module_param(eth_debug, int, 0);
MODULE_PARM_DESC(eth_debug, "Debug level (0=off, 1=some, 2=more, ...).");

char* tor0_name = "tor0";
module_param(tor0_name, charp, S_IRUGO);
MODULE_PARM_DESC(tor0_name, "Name of torus device (tor0).");

char* tor0_id  = "00:11:22:33:44:55";
module_param(tor0_id, charp, S_IRUGO);
MODULE_PARM_DESC(tor0_id, "MAC address of torus device (66:00:00:00:00:00).");

BG_FlightRecorderRegistry_t vrnic_log;
u64 vrnic_log_lock;
BG_FlightRecorderLog_t vrnic_log_entry[1024];
BG_FlightRecorderFormatter_t vrnic_log_formatter[] = {
	{ "FL_INVALD", FLIGHTRECORDER_PRINTF, "Invalid flight recorder entry", NULL },  
	{ "FL_INIT_DONE", FLIGHTRECORDER_PRINTF, "Initialization complete rc=%d", NULL},
	{ "FL_CONN_REQT", FLIGHTRECORDER_PRINTF, "mu_connect_cb - MUDM_CONN_REQUEST : msg=%p, address=%08x", NULL},
	{ "FL_CONN_RPLY", FLIGHTRECORDER_PRINTF, "mu_connect_cb - MUDM_CONN_REPLY : msg=%p, status=%d, sQPN=%u, dQPN=%u", NULL},
	{ "FL_CONN_DISC", FLIGHTRECORDER_PRINTF, "mu_connect_cb - MUDM_CONN_DISCONNECTED : msg=%p, sQPN=%u, dQPN=%u, QP state=%u", NULL},
	{ "FL_CMCONNECT", FLIGHTRECORDER_PRINTF, "bgvrnic_connect : cm_id=%p, vrnic_qp=%p, QPN=%u, private_data_len=%d", NULL},
	{ "FL_CM_ACCEPT", FLIGHTRECORDER_PRINTF, "bgvrnic_accept : cm_id=%p, vrnic_qp=%p, sQPN=%u, dQPN=%u", NULL},
	{ "FL_CM_REJECT", FLIGHTRECORDER_PRINTF, "bgvrnic_reject : cm_id=%p, sQPN=%u, dQPN=%u, private_data_len=%d", NULL},
	{ "FL_CRT_LISTN", FLIGHTRECORDER_PRINTF, "bgvrnic_create_listener : cm_id=%p, backlog=%d", NULL}, 
	{ "FL_DST_LISTN", FLIGHTRECORDER_PRINTF, "bgvrnic_destroy_listener : cm_id=%p", NULL},
	{ "FL_STAT_SEND", FLIGHTRECORDER_PRINTF, "mu_status_cb - SEND : wqe=%p, QPN=%u, status=%d", NULL},
	{ "FL_STAT_RECV", FLIGHTRECORDER_PRINTF, "mu_status_cb - RECV : wqe=%p, QPN=%u, status=%d", NULL},
	{ "FL_STAT_RDMA", FLIGHTRECORDER_PRINTF, "mu_status_cb - RDMA : wqe=%p, QPN=%u, status=%d", NULL}, 
	{ "FL_BUG_VERBS", FLIGHTRECORDER_PRINTF, "bgvrnic_verbs.c:%d 0x%x 0x%x 0x%x", NULL},
	{ "FL_LINK_RSET", FLIGHTRECORDER_PRINTF, "I/O link reset : block_id=%u line=%d", NULL},
	{ "FL_MODIFY_QP", FLIGHTRECORDER_PRINTF, "modify_qp : line=%u data1=0x%x data2=0x%x data3=0x%x", NULL},
	{ "FL_POST_RECV", FLIGHTRECORDER_PRINTF, "bgvrnic_post_recv : QP=%p, WR=%p, QPS=%u, WQE=%p", NULL},
	{ "FL_TAKE_BUFF", FLIGHTRECORDER_PRINTF, "mu_recv_cb : QP=%p, type=0x%llx, mu_context=0x%llx, WQE=%p", NULL},
	{ "FL_NO_BUFFER", FLIGHTRECORDER_PRINTF, "mu_recv_cb : QP=%p, type=0x%llx, mu_context=0x%llx, empty?=%u", NULL},
	{ "FL_ADD_CQE__", FLIGHTRECORDER_PRINTF, "bgvrnic_add_cqe : QP=%p, CQE=%p, wc opcode=0x%llx status=0x%llx", NULL },
};

Personality_t _bgpers;

static struct bgvrnic_device* _vrnic;

extern int bglog_addFlightRecorder(BG_FlightRecorderRegistry_t*);

extern u32 bgq_io_reset_block_id;



static int vrnic_thread(void* data)
{
	u64 io_link = (u64) data;

	BUG_ON(io_link >= (sizeof(_vrnic->mu_poll_thread) / sizeof(_vrnic->mu_poll_thread[0])));
	set_current_state(TASK_INTERRUPTIBLE);

	while (!kthread_should_stop()) {
		int rc = mudm_poll_iolink(_vrnic->mu_context, (u32) io_link);
		if (rc) {
			printk(KERN_EMERG "mudm_poll_iolink() returns %d\n", rc);
			break;
		} 

		/* If I/O link reset is indicated then handle that now. */
		if (bgq_io_reset_block_id && io_link == 0) {
			u64 link[1] = { bgq_io_reset_block_id };

			Kernel_WriteFlightLog(FLIGHTLOG, FL_LINK_RSET, (u64) link[0], __LINE__, 0, 0);
			bgq_io_reset_block_id = 0;
			rc = mudm_resetIOlink(_vrnic->mu_context, bgq_io_reset_block_id);
			if (rc)
				printk(KERN_WARNING "%s:%d - Unable to reset I/O link. rc=%d\n", __func__, __LINE__, rc);
			Kernel_WriteFlightLog(FLIGHTLOG, FL_LINK_RSET, (u64) link[0], __LINE__, 0, 0);
			rc = bluegene_sendBlockStatus(JMB_BLOCKSTATE_IO_LINK_CLOSED, 1, link);
			if (rc)
				printk(KERN_WARNING "%s:%d - Unable to send link closed notification. rc=%d\n", __func__, __LINE__, rc);
		}

		yield();
	}

	printk(KERN_INFO "Ending MU polling.\n");
	__set_current_state(TASK_RUNNING);

	return 0;
}


static int bgvrnic_recv_immed(struct bgvrnic_qp* vrnic_qp,
			      struct bgvrnic_wqe* wqe,
			      u8*                data,
			      u32                length)
{
        int rc;
	int i = 0;
	struct ib_recv_wr* ib_wr = &wqe->wr.ib_recv;
	struct mudm_sgl psge[MUDM_MAX_SGE];
	int num_psge = MUDM_MAX_SGE;
ENTER

	rc = bgvrnic_xlate_sgl(vrnic_qp->vrnic_dev, ib_wr->sg_list, ib_wr->num_sge, psge, &num_psge);
	BUG_ON(rc < 0); 
	if (rc < length) {
		/* More data inbound than this RQE can handle.  Truncate and indicate local length error. */
		length = rc;
		wqe->status = IB_WC_LOC_LEN_ERR;
		rc = -E2BIG;
	} else {
		wqe->status = IB_WC_SUCCESS;
		rc = 0;
	}
	wqe->length = length;

	while (length) {
		u32 chunk_size = min_t(u32, length, psge[i].memlength);

		memcpy((void*) phys_to_virt(psge[i].physicalAddr), data, chunk_size);

		length -= chunk_size;
		data += chunk_size;

		if (chunk_size >= psge[i].memlength) {
			i++;
			BUG_ON(length && (i >= num_psge));
		}
	}

	wqe->state = BGVRNIC_WQES_DONE;

EXIT
	return rc;
}


static int bgvrnic_recv_remote(struct bgvrnic_qp *vrnic_qp,
			       struct bgvrnic_wqe *wqe,
			       struct ionet_send_remote *pkt)
{
	int rc;
	struct ib_recv_wr *ib_wr = &wqe->wr.ib_recv;
	struct mudm_sgl psge[MUDM_MAX_SGE];
	int num_psge = MUDM_MAX_SGE;

	/* Translate the user memory into physical memory. */
	rc = bgvrnic_xlate_sgl(vrnic_qp->vrnic_dev, ib_wr->sg_list, ib_wr->num_sge, psge, &num_psge);
	BUG_ON(rc < 0);
	if (rc < pkt->payload.data_length) {
		/* More data inbound than this RQE can handle.  Truncate and indicate local length error. */
		pkt->payload.data_length = rc;
		wqe->status = IB_WC_LOC_LEN_ERR;
	}

	/* Initialize the work queue entry. */
	wqe->length = rc; 
	if (pkt->payload.flags & MUDM_IMMEDIATE_DATA) {
		wqe->imm_data_valid = 1;
		wqe->imm_data = pkt->payload.immediate_data;
	}

	/* Start the RDMA. */
	while ((rc = mudm_rdma_read(vrnic_qp->mu_context, wqe, (void*) pkt->payload.RequestID,
				    pkt->payload.rdma_object, pkt->payload.data_length, 
				    psge, pkt->payload.SGE)) == -EBUSY);
	switch (rc) {
		case -ECANCELED:
			wqe->status = IB_WC_REM_ABORT_ERR;
			/* Yes.  Fall through here. */
		case 0:
			wqe->state = BGVRNIC_WQES_DONE;
			break;

		case -EINPROGRESS:
			wqe->state = BGVRNIC_WQES_RUN;
                	rc = 0;
			break;

		default:
			printk(KERN_EMERG "%s:%d - mudm_rdma_read failed with rc = %d\n", __func__, __LINE__, rc);
			BUG();
	}

	return rc;
}
		       

int mu_recv_cb(char* data,
                void* cb_context)
{
        int rc = 0;
	int psn_mismatch = 0;
        struct ionet_header* header = (struct ionet_header*) data;
        struct bgvrnic_device* vrnic_dev = (struct bgvrnic_device*) cb_context;
	struct ib_qp* ib_qp;
	struct bgvrnic_qp* vrnic_qp;

ENTER
	if (header->type == MUDM_IP_IMMED) {
                struct ionet_frame* frame = (struct ionet_frame*) data;

                rc = bgvrnic_rx(vrnic_dev->net_dev, frame->payload, frame->ionet_pkt_hdr.payload_length);
		goto out;
        }

	ib_qp = bgvrnic_get_qp(&vrnic_dev->ib_dev, header->dest_qpn);
	vrnic_qp = container_of(ib_qp, struct bgvrnic_qp, ib_qp);

	/* Validate the packet sequence number. */
	if (header->sequence_number != vrnic_qp->ib_qp_attrs.rq_psn) {
		printk(KERN_EMERG "PSN mismatch.  Expecting %u but received %u on QPN %d.  Synchronizing...\n", vrnic_qp->ib_qp_attrs.rq_psn, header->sequence_number, header->dest_qpn);
		psn_mismatch = 1;
		vrnic_qp->ib_qp_attrs.rq_psn = header->sequence_number;
	}
	vrnic_qp->ib_qp_attrs.rq_psn = (vrnic_qp->ib_qp_attrs.rq_psn + 1) & 0xffff;

       	switch (header->type) {
               	case MUDM_PKT_DATA_IMM: 
		case MUDM_PKT_DATA_REMOTE: {
			int recvd = 0;
			struct bgvrnic_wqe* wqe = NULL;

			/* Find an unused WQE for the incoming data. */
			spin_lock(&vrnic_qp->rq_lock);
			list_for_each_entry(wqe, &vrnic_qp->rq, list) { 
				if (wqe->state == BGVRNIC_WQES_INIT) {
					Kernel_WriteFlightLog(FLIGHTLOG, FL_TAKE_BUFF, (u64) vrnic_qp, (u64) header->type,
                                                        (u64) vrnic_qp->mu_context, (u64) wqe);
					if (header->type == MUDM_PKT_DATA_IMM) {
                        			struct ionet_send_imm* pkt = (struct ionet_send_imm*) data;

                        			rc = bgvrnic_recv_immed(vrnic_qp, wqe, pkt->payload, header->payload_length);                   
					} else if (header->type == MUDM_PKT_DATA_REMOTE) {
						struct ionet_send_remote *pkt = (struct ionet_send_remote*) data;

						rc = bgvrnic_recv_remote(vrnic_qp, wqe, pkt); 
					} else
						BUG();

					recvd = 1;
					break;
				}
			}	

			if (recvd) {
				bgvrnic_process_recv_wqes(vrnic_qp);
			} else {
				struct iw_cm_event e;

				Kernel_WriteFlightLog(FLIGHTLOG, FL_NO_BUFFER, (u64) vrnic_qp, (u64) header->type, 
							(u64) vrnic_qp->mu_context, list_empty(&vrnic_qp->rq));

				/* No WQE available to receive incoming data.  Disconnect is our only choice. */
                                if (vrnic_qp->cm_id) {
                                        e.event = IW_CM_EVENT_DISCONNECT;
                                        e.status = -ECONNRESET; 
                                        vrnic_qp->ib_qp_attrs.qp_state = IB_QPS_SQE;
                                        e.remote_addr = vrnic_qp->cm_id->remote_addr;
                                        e.local_addr = vrnic_qp->cm_id->local_addr;
                                        e.private_data = e.provider_data = NULL;
                                        e.private_data_len = 0;
                                        rc = vrnic_qp->cm_id->event_handler(vrnic_qp->cm_id, &e);
                                        if (rc)
                                                printk(KERN_EMERG "%s:%d - CM event type %d returned %d\n", __func__, __LINE__, e.event, rc);
                                }
                                rc = mudm_disconnect(vrnic_qp->mu_context);
                                if (rc)
                                        printk(KERN_EMERG "%s:%d - mudm_disconnect returned %d\n", __func__, __LINE__, rc);
                        }
			
			spin_unlock(&vrnic_qp->rq_lock);
		}
		break;	

                case MUDM_RDMA_READ: {
			struct bgvrnic_wqe* wqe = bgvrnic_alloc_wqe(BGVRNIC_WQET_RDMA, NULL, vrnic_qp);
                        struct ionet_send_remote* pkt = (struct ionet_send_remote*) data;
       	                struct ib_sge usge = { .addr = pkt->payload.remote_vaddr, .length = pkt->payload.data_length, .lkey = pkt->payload.rkey };
			struct mudm_sgl psge[MUDM_MAX_SGE];
			int num_psge = MUDM_MAX_SGE;
			int length = bgvrnic_xlate_sgl(vrnic_dev, &usge, 1, psge, &num_psge);

			BUG_ON(!wqe);	
			BUG_ON(length != usge.length);
			while ((rc = mudm_rdma_read(vrnic_qp->mu_context, wqe, (void*) pkt->payload.RequestID, pkt->payload.rdma_object, 
				length, psge, pkt->payload.SGE)) == -EBUSY);

			switch (rc) {
				case 0:
				case -ECANCELED:
					bgvrnic_free_wqe(wqe);
				break;

				case -EINPROGRESS:
					rc = 0;
					wqe->state = BGVRNIC_WQES_RUN;
				break;
					
				default:
					printk(KERN_EMERG "%s:%d - mudm_rdma_read failed with rc = %d\n", __func__, __LINE__, rc);
			}
		}
        	break;

		case MUDM_RDMA_WRITE: {
			struct ionet_send_remote* pkt = (struct ionet_send_remote*) data;
			struct ib_sge usge = { .addr = pkt->payload.remote_vaddr, .length = pkt->payload.data_length, .lkey = pkt->payload.rkey };
			struct mudm_sgl psge[MUDM_MAX_SGE];
			int num_psge = MUDM_MAX_SGE;
			int length = bgvrnic_xlate_sgl(vrnic_dev, &usge, 1, psge, &num_psge);

			BUG_ON(length != usge.length);
			while ((rc = mudm_rdma_write(vrnic_qp->mu_context, (void*) pkt->payload.RequestID, pkt->payload.rdma_object, 
				length, psge, pkt->payload.SGE)) == -EBUSY);

			switch (rc) {
				case 0:
				case -ECANCELED:
				break;

				default:
					BUG_ON(rc); // We have no way to track the success or failure of this operation!
			}
                }
                break;

                default:
                       	printk(KERN_EMERG "%s:%d - We don't handle type %04x yet.\n", __func__, __LINE__, header->type);
	}

out:

EXIT

	if (psn_mismatch && rc == 0)
		rc = -EPROTO;

        return rc;
}



static int __init bgvrnic_module_init(void)
{
	int rc = 0;

ENTER
	if (bluegene_getPersonality((void*) &_bgpers, sizeof(_bgpers)))
		goto out;
	BUG_ON(_bgpers.Version != PERSONALITY_VERSION);

	_vrnic = bgvrnic_alloc_device();
	if (_vrnic) {
		u64 l;
		u8 tor0_mac[8];
		u32 tmp_mac[8];
		struct mudm_init_info init_info;

		/* Initialize the MU code */
		init_info.callers_version = MUDM_VERSION;
		init_info.req_inbound_connections = 2048;
		init_info.req_inj_fifos = 1;
		init_info.req_rec_fifos = 1;
		init_info.recv = mu_recv_cb;
		init_info.recv_conn = mu_connect_cb;
		init_info.status = mu_status_cb;
		init_info.allocate = mu_allocate_cb;
		init_info.free = mu_free_cb;	
		init_info.personality =  &_bgpers;  
		init_info.callback_context = (void*) _vrnic;
		rc = mudm_init(&init_info, &_vrnic->mu_cfg_info, &_vrnic->mu_context);
		if (rc) {
			printk(KERN_EMERG "mudm_init failed with rc=%d\n", rc);
			goto out1;
		}
                bglog_addFlightRecorder(&_vrnic->mu_cfg_info.mudm_hi_wrap_flight_recorder);
                bglog_addFlightRecorder(&_vrnic->mu_cfg_info.mudm_lo_wrap_flight_recorder);
                
		/* Initialize flight recorder. */
		vrnic_log.flightlog = vrnic_log_entry; 
		BUG_ON(!vrnic_log.flightlog);
		vrnic_log.flightsize = 1024;
		vrnic_log.flightlock = (u64*) &vrnic_log_lock;	
		vrnic_log.flightformatter = vrnic_log_formatter;
		vrnic_log.num_ids = sizeof(vrnic_log_formatter) / sizeof(*vrnic_log.flightformatter);
		vrnic_log.lastStateSet = vrnic_log.lastState = vrnic_log.lastStateTotal = vrnic_log.lastOffset = 0;	
		vrnic_log.registryName = "vRNIC";
		bglog_addFlightRecorder(&vrnic_log);

		BUG_ON(_vrnic->mu_cfg_info.num_io_links > (sizeof(_vrnic->mu_poll_thread) / sizeof(_vrnic->mu_poll_thread[0])));

		sscanf(tor0_id, "%2x:%2x:%2x:%2x:%2x:%2x", &tmp_mac[0], &tmp_mac[1], &tmp_mac[2], &tmp_mac[3], &tmp_mac[4], &tmp_mac[5]);
		tor0_mac[0] = (u8) tmp_mac[0];
		tor0_mac[1] = (u8) tmp_mac[1];		
		tor0_mac[2] = (u8) tmp_mac[2];
		tor0_mac[3] = (u8) tmp_mac[3];
		tor0_mac[4] = (u8) tmp_mac[4];
		tor0_mac[5] = (u8) tmp_mac[5];
		tor0_mac[6] = tor0_mac[7] = 0;

		_vrnic->net_dev = bgvrnic_alloc_netdev(_vrnic, tor0_name, tor0_mac);
		if (!_vrnic->net_dev) {
			rc = -ENOMEM;
			goto out2;
		}

		/* The GUID must match the MAC address */
		memset(&_vrnic->ib_dev.node_guid, 0, sizeof(_vrnic->ib_dev.node_guid));
		memcpy(&_vrnic->ib_dev.node_guid, _vrnic->net_dev->dev_addr, 6); 
		memcpy(&_vrnic->ib_dev_attrs.sys_image_guid, &_vrnic->ib_dev.node_guid, sizeof(_vrnic->ib_dev.node_guid));

		rc = ib_register_device(&_vrnic->ib_dev, NULL);
		if (rc) {
			printk(KERN_EMERG "IB device registration failed!\n");
			goto out2;
		}

		/* Start a kernel thread for each I/O link. */
		for (l = 0; l < _vrnic->mu_cfg_info.num_io_links; l++) {
			int cpu;

			/* Determine which CPU to use.  Each MU link should be on its own A2 thread if possible. */
			if (num_online_cpus() == 1)
				cpu = 0;
			else
				cpu = num_online_cpus() - l - 1;

			_vrnic->mu_poll_thread[l] = kthread_create(vrnic_thread, (void*) l, "bgvrnic/%u", cpu);
			if (!IS_ERR(_vrnic->mu_poll_thread[l])) {
				kthread_bind(_vrnic->mu_poll_thread[l], cpu);
				wake_up_process(_vrnic->mu_poll_thread[l]); 
			} else {
				printk(KERN_EMERG "Failure starting poll thread! rc=%p\n", _vrnic->mu_poll_thread[l]);
				_vrnic->mu_poll_thread[l] = NULL;
				goto out3;
			}
		}
	}

	goto out;

out3:
	ib_unregister_device(&_vrnic->ib_dev);	
out2:
	mudm_terminate(_vrnic->mu_context);
	_vrnic->mu_context = NULL;
out1:
	bgvrnic_dealloc_device(_vrnic);
	_vrnic = NULL;

out:
	Kernel_WriteFlightLog(FLIGHTLOG, FL_INIT_DONE, rc, 0, 0, 0);
EXIT

	return rc;
}



static void __exit bgvrnic_module_exit(void)
{
ENTER
	if (_vrnic) {
		u64 l;

		for (l = 0; l < (sizeof(_vrnic->mu_poll_thread) / sizeof(_vrnic->mu_poll_thread[0])); l++)
			if (_vrnic->mu_poll_thread[l]) {
				kthread_stop(_vrnic->mu_poll_thread[l]);
				_vrnic->mu_poll_thread[l] = NULL;
			}
		mudm_terminate(_vrnic->mu_context);
		ib_unregister_device(&_vrnic->ib_dev);
		bgvrnic_dealloc_device(_vrnic);
		_vrnic = NULL;
	}	
EXIT
	return;
}



int mu_allocate_cb(struct mudm_memory_region* memory,
		   size_t length,
		   void* cb_context)
{
	int rc = 0;
	void* va;
	dma_addr_t pa; 
	struct bgvrnic_device* vrnic_dev = (struct bgvrnic_device*) cb_context;
	
	va = dma_alloc_coherent(vrnic_dev->ib_dev.dma_device, length, &pa, GFP_KERNEL);
	BUG_ON(!va);

	memory->base_vaddr = va;
	memory->base_paddr = (void*) pa;
	memory->length = length;
		
	return rc;
}


int mu_free_cb(struct mudm_memory_region* memory,
		void* cb_context)
{
	struct bgvrnic_device* vrnic_dev = (struct bgvrnic_device*) cb_context;
	
	dma_free_coherent(vrnic_dev->ib_dev.dma_device, memory->length, memory->base_vaddr, (dma_addr_t) memory->base_paddr);

	return 0;
}


module_init(bgvrnic_module_init);
module_exit(bgvrnic_module_exit);

