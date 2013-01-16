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
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include "common.h"


MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("IBM Blue Gene/Q Message Unit module");

/* Make the bg_mu_dev symbol available to the kernel */
struct device* bg_mu_dev;
EXPORT_SYMBOL_GPL(bg_mu_dev);


static int __init bgmudm_probe(struct platform_device *dev)
{
        /* Set bg_mu_dev to the device created in the platform_device */
        bg_mu_dev = &dev->dev;

        return 0;
}

static int bgmudm_remove( struct platform_device *dev)
{
        bg_mu_dev = NULL;

        return 0;
}

/* Platform structures */
struct platform_device *bgmudm;

struct platform_driver bgmudm_driver = {

        .probe = bgmudm_probe,
        .remove = __exit_p(bgmudm_remove),
        .driver = {
                .name = "bgmudm",
        },
};

unsigned long long __bgq_base_dma;
unsigned long long _bgq_mu_dcr_mem_mapped_start_offset;

static int __init bgmudm_module_init(void)
{
	void __iomem *dma;

	dma = ioremap(__BGQ_BASE_DMA, 0x08000000);
	if (!dma) {
		pr_warn("%s: could not map base dma\n", __func__);
		return -ENODEV;
	}
	__bgq_base_dma = (ulong)dma;

	dma = ioremap( _BGQ_MU_DCR_MEM_MAPPED_START_OFFSET(), 0x08000000);
	if (!dma) {
		pr_warn("%s: could not map DCR mem\n", __func__);
		iounmap((void *)__bgq_base_dma);
		return -ENODEV;
	}
	_bgq_mu_dcr_mem_mapped_start_offset = (ulong)dma;

        /* Register the device */
        bgmudm = platform_device_register_simple("bgmudm", 0, NULL, 0);
        if(IS_ERR(bgmudm))
                return PTR_ERR(bgmudm);

        if(platform_driver_register(&bgmudm_driver))
        {
                printk(KERN_EMERG "%s -> Error registering bgmudm. \n", __FUNCTION__);
                platform_device_unregister(bgmudm);
        }

        return 0;
}

static  void __exit bgmudm_module_exit(void)
{
        platform_driver_unregister(&bgmudm_driver);
        platform_device_unregister(bgmudm);
	iounmap((void *)(ulong)__bgq_base_dma);
	iounmap((void *)(ulong)_bgq_mu_dcr_mem_mapped_start_offset);

        return;

}

module_init(bgmudm_module_init);
module_exit(bgmudm_module_exit);
