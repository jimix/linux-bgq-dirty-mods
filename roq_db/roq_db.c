#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/aio_abi.h>
#include <linux/capability.h>
#include <linux/list.h>
#include <linux/sem.h>
#include <asm/siginfo.h>
#include <asm/signal.h>
#include <linux/unistd.h>
#include <linux/quota.h>
#include <linux/key.h>
#include <trace/syscall.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/slab.h>
#include <asm/div64.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/cpumask.h>

long (*doorbell_call)(int, u32, u32) = NULL;

EXPORT_SYMBOL(doorbell_call);

asmlinkage long sys_roq_db(int db_type, uint32_t db_id, uint32_t num_wqe)
{
	if (*doorbell_call)
		return (*doorbell_call)(db_type, db_id, num_wqe);

	pr_info("roq_db: call unassigned. type %d, id %u, # wqe %u\n",
		db_type, db_id, num_wqe);

	return -ENOSYS;
}
