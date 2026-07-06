// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/component.h>
#include <linux/ioport.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "dc-drv.h"
#include "dc-lib.h"
#include "dc-pe.h"

#define HEIGHT(x)	FIELD_PREP(GENMASK(29, 16), (x) - 1)
#define WIDTH(x)	FIELD_PREP(GENMASK(13, 0), (x) - 1)
#define BLUE(x)		FIELD_PREP(GENMASK(15, 8), (x))
#define RED(x)		FIELD_PREP(GENMASK(31, 24), (x))

/* i.MX8QXP instance layout */
static const struct dc_subdev_info dc_cf_info[] = {
	{ .reg_start = 0x56180960, .id = 0, },
	{ .reg_start = 0x561809e0, .id = 1, },
	{ .reg_start = 0x561809a0, .id = 4, },
	{ .reg_start = 0x56180a20, .id = 5, },
};

static const u32 dc_cf_link_ids[] = {
	LINK_ID_CONSTFRAME0,
	LINK_ID_CONSTFRAME1,
	LINK_ID_CONSTFRAME4,
	LINK_ID_CONSTFRAME5,
};

static const struct dc_lib_cf_match_data dc_cf_imx8qxp_data = {
	.subdev_info     = dc_cf_info,
	.subdev_info_cnt = ARRAY_SIZE(dc_cf_info),
	.link_ids        = dc_cf_link_ids,
	.link_ids_cnt    = ARRAY_SIZE(dc_cf_link_ids),
};

/* i.MX95 instance layout: PEC addresses are base 0x4b400000 + aux_ofs from dpu95-core */
static const struct dc_subdev_info dc_cf_imx95_info[] = {
	{ .reg_start = 0x4b4f1000, .id = 0, },
	{ .reg_start = 0x4b531000, .id = 1, },
	{ .reg_start = 0x4b501000, .id = 4, },
	{ .reg_start = 0x4b541000, .id = 5, },
};

static const u32 dc_cf_imx95_link_ids[] = {
	IMX95_LINK_ID_CONSTFRAME0,
	IMX95_LINK_ID_CONSTFRAME1,
	IMX95_LINK_ID_CONSTFRAME4,
	IMX95_LINK_ID_CONSTFRAME5,
};

static const struct dc_lib_cf_match_data dc_cf_imx95_data = {
	.subdev_info     = dc_cf_imx95_info,
	.subdev_info_cnt = ARRAY_SIZE(dc_cf_imx95_info),
	.link_ids        = dc_cf_imx95_link_ids,
	.link_ids_cnt    = ARRAY_SIZE(dc_cf_imx95_link_ids),
};

static const struct regmap_range dc_cf_regmap_ranges[] = {
	regmap_reg_range(CF_STATICCONTROL, CF_CONSTANTCOLOR),
};

static const struct regmap_access_table dc_cf_regmap_access_table = {
	.yes_ranges = dc_cf_regmap_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_cf_regmap_ranges),
};

static const struct regmap_config dc_cf_cfg_regmap_config = {
	.name = "cfg",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_cf_regmap_access_table,
	.rd_table = &dc_cf_regmap_access_table,
	.max_register = CF_CONSTANTCOLOR,
};

enum dc_link_id dc_cf_get_link_id(struct dc_cf *cf)
{
	return cf->link;
}
EXPORT_SYMBOL_GPL(dc_cf_get_link_id);

void dc_cf_framedimensions(struct dc_cf *cf, unsigned int w,
			   unsigned int h)
{
	regmap_write(cf->reg_cfg, CF_FRAMEDIMENSIONS, WIDTH(w) | HEIGHT(h));
}
EXPORT_SYMBOL_GPL(dc_cf_framedimensions);

void dc_cf_constantcolor_black(struct dc_cf *cf)
{
	regmap_write(cf->reg_cfg, CF_CONSTANTCOLOR, 0);
}
EXPORT_SYMBOL_GPL(dc_cf_constantcolor_black);

void dc_cf_constantcolor_blue(struct dc_cf *cf)
{
	regmap_write(cf->reg_cfg, CF_CONSTANTCOLOR, BLUE(0xff));
}
EXPORT_SYMBOL_GPL(dc_cf_constantcolor_blue);

void dc_cf_init(struct dc_cf *cf)
{
	regmap_write(cf->reg_cfg, CF_STATICCONTROL, SHDEN);
}
EXPORT_SYMBOL_GPL(dc_cf_init);

static int dc_cf_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct dc_lib_cf_match_data *match_data;
	struct dc_drm_device *dc_drm = data;
	struct resource *res_pec;
	void __iomem *base_cfg;
	struct dc_cf *cf;
	int id, i;

	match_data = of_match_node(dev->driver->of_match_table,
				   dev->of_node)->data;
	if (!match_data)
		return -EINVAL;

	cf = devm_kzalloc(dev, sizeof(*cf), GFP_KERNEL);
	if (!cf)
		return -ENOMEM;

	res_pec = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	base_cfg = devm_platform_ioremap_resource_byname(pdev, "cfg");
	if (IS_ERR(base_cfg))
		return PTR_ERR(base_cfg);

	cf->reg_cfg = devm_regmap_init_mmio(dev, base_cfg,
					    &dc_cf_cfg_regmap_config);
	if (IS_ERR(cf->reg_cfg))
		return PTR_ERR(cf->reg_cfg);

	id = dc_subdev_get_id(match_data->subdev_info,
			      match_data->subdev_info_cnt, res_pec);
	if (id < 0) {
		dev_err(dev, "failed to get instance number: %d\n", id);
		return id;
	}

	/* look up link_id by position, then assign cont/safe slot by id */
	for (i = 0; i < match_data->subdev_info_cnt; i++) {
		if (match_data->subdev_info[i].id == id) {
			cf->link = match_data->link_ids[i];
			break;
		}
	}

	switch (id) {
	case 0:
		dc_drm->cf_cont[0] = cf;
		break;
	case 1:
		dc_drm->cf_cont[1] = cf;
		break;
	case 4:
		dc_drm->cf_safe[0] = cf;
		break;
	case 5:
		dc_drm->cf_safe[1] = cf;
		break;
	}

	return 0;
}

static const struct component_ops dc_cf_ops = {
	.bind = dc_cf_bind,
};

static int dc_cf_probe(struct platform_device *pdev)
{
	int ret;

	ret = component_add(&pdev->dev, &dc_cf_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to add component\n");

	return 0;
}

static void dc_cf_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dc_cf_ops);
}

static const struct of_device_id dc_cf_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-dc-constframe",
	  .data = &dc_cf_imx8qxp_data },
	{ .compatible = "fsl,imx95-dc-constframe",
	  .data = &dc_cf_imx95_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_cf_dt_ids);

struct platform_driver dc_cf_driver = {
	.probe = dc_cf_probe,
	.remove = dc_cf_remove,
	.driver = {
		.name = "imx8-dc-constframe",
		.suppress_bind_attrs = true,
		.of_match_table = dc_cf_dt_ids,
	},
};
