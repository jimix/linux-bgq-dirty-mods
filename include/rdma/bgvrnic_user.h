#ifndef _BGVRNIC_USER_H
#define _BGVRNIC_USER_H

#define BGVRNIC_NAME "bgvrnic_0"

/*
 * user commands/command responses must correlate with the bgvrnic_abi
 */

struct bgvrnic_ureq_create_cq {
        __u32   dummy;
};

struct bgvrnic_uresp_create_cq {
        __u32   cq_id;
};

struct bgvrnic_ureq_create_qp {

};

struct bgvrnic_uresp_create_qp {
        __u32   qp_id;
        __u32   sq_size;
        __u32   rq_size;
};

struct bgvrnic_uresp_reg_mr {
	__u32	num_frags;
};

struct bgvrnic_ureq_reg_mr {
};

#endif

