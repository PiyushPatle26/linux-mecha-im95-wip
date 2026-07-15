// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 *
 * Registers the hardware sub-block component drivers shared by both Display
 * Controller SoC drivers (imx8-dc-drm, imx95-dc-drm).
 */

#include <linux/array_size.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "dc-drv.h"

static struct platform_driver * const dc_subblock_drivers[] = {
	&dc_cf_driver,
	&dc_de_driver,
	&dc_dprc_driver,
	&dc_ed_driver,
	&dc_fg_driver,
	&dc_fl_driver,
	&dc_fw_driver,
	&dc_ic_driver,
	&dc_lb_driver,
	&dc_pe_driver,
	&dc_prg_driver,
	&dc_tc_driver,
	&dc_imx95_db_driver,
	&dc_imx95_dither_driver,
};

static int __init dc_lib_init(void)
{
	return platform_register_drivers(dc_subblock_drivers,
					 ARRAY_SIZE(dc_subblock_drivers));
}
module_init(dc_lib_init);

static void __exit dc_lib_exit(void)
{
	platform_unregister_drivers(dc_subblock_drivers,
				    ARRAY_SIZE(dc_subblock_drivers));
}
module_exit(dc_lib_exit);

MODULE_DESCRIPTION("i.MX Display Controller Helper Library");
MODULE_AUTHOR("Liu Ying <victor.liu@nxp.com>");
MODULE_LICENSE("GPL");
