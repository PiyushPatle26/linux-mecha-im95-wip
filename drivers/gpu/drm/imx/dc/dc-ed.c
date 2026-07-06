// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/component.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "dc-drv.h"
#include "dc-lib.h"
#include "dc-pe.h"

#define DIV_MASK		GENMASK(23, 16)
#define DIV(x)			FIELD_PREP(DIV_MASK, (x))
#define DIV_RESET		0x80
#define SYNC_MODE		BIT(8)
#define SINGLE			0
#define POWERDOWN		BIT(4)
#define SYNC_TRIGGER		BIT(0)
#define SYNC_BUSY		BIT(8)	/* in ED_PIXENGCFG_STATUS */
#define PERFCOUNTMODE		BIT(12)
#define KICK_MODE		BIT(8)
#define EXTERNAL		BIT(8)
#define GAMMAAPPLYENABLE	BIT(0)

/* i.MX8QXP instance layout */
static const struct dc_subdev_info dc_ed_info[] = {
	{ .reg_start = 0x56180980, .id = 0, },
	{ .reg_start = 0x56180a00, .id = 1, },
	{ .reg_start = 0x561809c0, .id = 4, },
	{ .reg_start = 0x56180a40, .id = 5, },
};

static const u32 dc_ed_src_sels[] = {
	LINK_ID_NONE,
	LINK_ID_CONSTFRAME0,
	LINK_ID_CONSTFRAME1,
	LINK_ID_CONSTFRAME4,
	LINK_ID_CONSTFRAME5,
	LINK_ID_LAYERBLEND3,
	LINK_ID_LAYERBLEND2,
	LINK_ID_LAYERBLEND1,
	LINK_ID_LAYERBLEND0,
};

static const struct dc_lib_ed_match_data dc_ed_imx8qxp_data = {
	.subdev_info     = dc_ed_info,
	.subdev_info_cnt = ARRAY_SIZE(dc_ed_info),
	.src_sels        = dc_ed_src_sels,
	.src_sels_cnt    = ARRAY_SIZE(dc_ed_src_sels),
};

/* i.MX95 instance layout: PEC addresses are base 0x4b400000 + aux_ofs from dpu95-core */
static const struct dc_subdev_info dc_ed_imx95_info[] = {
	{ .reg_start = 0x4b511000, .id = 0, },
	{ .reg_start = 0x4b551000, .id = 1, },
	{ .reg_start = 0x4b521000, .id = 4, },
	{ .reg_start = 0x4b561000, .id = 5, },
};

static const u32 dc_ed_imx95_src_sels[] = {
	IMX95_LINK_ID_CONSTFRAME0,
	IMX95_LINK_ID_CONSTFRAME1,
	IMX95_LINK_ID_CONSTFRAME4,
	IMX95_LINK_ID_CONSTFRAME5,
	IMX95_LINK_ID_LAYERBLEND6,
	IMX95_LINK_ID_LAYERBLEND5,
	IMX95_LINK_ID_LAYERBLEND4,
	IMX95_LINK_ID_LAYERBLEND3,
	IMX95_LINK_ID_LAYERBLEND2,
	IMX95_LINK_ID_LAYERBLEND1,
};

static const struct dc_lib_ed_match_data dc_ed_imx95_data = {
	.subdev_info     = dc_ed_imx95_info,
	.subdev_info_cnt = ARRAY_SIZE(dc_ed_imx95_info),
	.src_sels        = dc_ed_imx95_src_sels,
	.src_sels_cnt    = ARRAY_SIZE(dc_ed_imx95_src_sels),
};

static const struct regmap_range dc_ed_pec_regmap_write_ranges[] = {
	regmap_reg_range(ED_PIXENGCFG_STATIC, ED_PIXENGCFG_STATIC),
	regmap_reg_range(ED_PIXENGCFG_DYNAMIC, ED_PIXENGCFG_DYNAMIC),
	regmap_reg_range(ED_PIXENGCFG_TRIGGER, ED_PIXENGCFG_TRIGGER),
};

static const struct regmap_access_table dc_ed_pec_regmap_write_table = {
	.yes_ranges = dc_ed_pec_regmap_write_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_ed_pec_regmap_write_ranges),
};

static const struct regmap_range dc_ed_pec_regmap_read_ranges[] = {
	regmap_reg_range(ED_PIXENGCFG_STATIC, ED_PIXENGCFG_STATIC),
	regmap_reg_range(ED_PIXENGCFG_DYNAMIC, ED_PIXENGCFG_DYNAMIC),
	regmap_reg_range(ED_PIXENGCFG_STATUS, ED_PIXENGCFG_STATUS),
};

static const struct regmap_access_table dc_ed_pec_regmap_read_table = {
	.yes_ranges = dc_ed_pec_regmap_read_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_ed_pec_regmap_read_ranges),
};

static const struct regmap_range dc_ed_pec_regmap_volatile_ranges[] = {
	regmap_reg_range(ED_PIXENGCFG_TRIGGER, ED_PIXENGCFG_TRIGGER),
	regmap_reg_range(ED_PIXENGCFG_STATUS, ED_PIXENGCFG_STATUS),
};

static const struct regmap_access_table dc_ed_pec_regmap_volatile_table = {
	.yes_ranges = dc_ed_pec_regmap_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_ed_pec_regmap_volatile_ranges),
};

static const struct regmap_config dc_ed_pec_regmap_config = {
	.name = "pec",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_ed_pec_regmap_write_table,
	.rd_table = &dc_ed_pec_regmap_read_table,
	.volatile_table = &dc_ed_pec_regmap_volatile_table,
	.max_register = ED_PIXENGCFG_STATUS,
};

static const struct regmap_range dc_ed_regmap_ranges[] = {
	regmap_reg_range(ED_STATICCONTROL, ED_STATICCONTROL),
	regmap_reg_range(ED_CONTROL, ED_CONTROL),
};

static const struct regmap_access_table dc_ed_regmap_access_table = {
	.yes_ranges = dc_ed_regmap_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_ed_regmap_ranges),
};

static const struct regmap_config dc_ed_cfg_regmap_config = {
	.name = "cfg",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_ed_regmap_access_table,
	.rd_table = &dc_ed_regmap_access_table,
	.max_register = ED_CONTROL,
};

void dc_ed_pec_clken(struct dc_ed *ed, u32 clken)
{
	regmap_write_bits(ed->reg_pec, ED_PIXENGCFG_DYNAMIC, CLKEN_MASK,
			  CLKEN(clken));
}
EXPORT_SYMBOL_GPL(dc_ed_pec_clken);

void dc_ed_pec_src_sel(struct dc_ed *ed, enum dc_link_id src)
{
	const u32 *src_sels = ed->match_data->src_sels;
	int i;

	for (i = 0; i < ed->match_data->src_sels_cnt; i++) {
		if (src_sels[i] == src) {
			regmap_write_bits(ed->reg_pec, ED_PIXENGCFG_DYNAMIC,
					  GENMASK(5, 0), src);
			return;
		}
	}
}
EXPORT_SYMBOL_GPL(dc_ed_pec_src_sel);

int dc_ed_pec_src_sel_get(struct dc_ed *ed, enum dc_link_id *src)
{
	const u32 *sels = ed->match_data->src_sels;
	int cnt = ed->match_data->src_sels_cnt;
	u32 val;
	int i;

	regmap_read(ed->reg_pec, ED_PIXENGCFG_DYNAMIC, &val);

	for (i = 0; i < cnt; i++) {
		if (sels[i] == val) {
			*src = val;
			dev_dbg(ed->dev, "get source selection: 0x%02x\n", val);
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(dc_ed_pec_src_sel_get);

void dc_ed_pec_sync_trigger(struct dc_ed *ed)
{
	regmap_write(ed->reg_pec, ED_PIXENGCFG_TRIGGER, SYNC_TRIGGER);
}
EXPORT_SYMBOL_GPL(dc_ed_pec_sync_trigger);

/*
 * Wait for a pixel-routing shadow load to complete.  ED_PIXENGCFG_STATUS
 * SYNC_BUSY is asserted from the sync trigger until the shadow load latches
 * on a FrameGen frame boundary.  Used on i.MX95 where the shadow-load
 * completion interrupt is not wired, so completion must be polled.
 */
int dc_ed_pec_sync_wait(struct dc_ed *ed)
{
	unsigned int val;

	return regmap_read_poll_timeout(ed->reg_pec, ED_PIXENGCFG_STATUS, val,
					!(val & SYNC_BUSY), 5, 100000);
}
EXPORT_SYMBOL_GPL(dc_ed_pec_sync_wait);

void dc_ed_init(struct dc_ed *ed)
{
	regmap_write_bits(ed->reg_pec, ED_PIXENGCFG_STATIC, POWERDOWN, 0);
	regmap_write_bits(ed->reg_pec, ED_PIXENGCFG_STATIC, SHDEN, SHDEN);
	regmap_write_bits(ed->reg_pec, ED_PIXENGCFG_STATIC, SYNC_MODE, SINGLE);
	regmap_write_bits(ed->reg_pec, ED_PIXENGCFG_STATIC, DIV_MASK,
			  DIV(DIV_RESET));
	regmap_write_bits(ed->reg_pec, ED_PIXENGCFG_DYNAMIC, CLKEN_MASK,
			  CLKEN(1) | 0);
	regmap_write_bits(ed->reg_cfg, ED_STATICCONTROL, SHDEN, SHDEN);
	regmap_write_bits(ed->reg_cfg, ED_STATICCONTROL, PERFCOUNTMODE, 0);
	regmap_write_bits(ed->reg_cfg, ED_STATICCONTROL, KICK_MODE, EXTERNAL);
	regmap_write_bits(ed->reg_cfg, ED_CONTROL, GAMMAAPPLYENABLE, 0);
}
EXPORT_SYMBOL_GPL(dc_ed_init);

static int dc_ed_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct dc_lib_ed_match_data *match_data;
	struct dc_drm_device *dc_drm = data;
	struct resource *res_pec;
	void __iomem *base_pec;
	void __iomem *base_cfg;
	struct dc_ed *ed;
	int id;

	match_data = of_match_node(dev->driver->of_match_table,
				   dev->of_node)->data;
	if (!match_data)
		return -EINVAL;

	ed = devm_kzalloc(dev, sizeof(*ed), GFP_KERNEL);
	if (!ed)
		return -ENOMEM;

	base_pec = devm_platform_get_and_ioremap_resource(pdev, 0, &res_pec);
	if (IS_ERR(base_pec))
		return PTR_ERR(base_pec);

	base_cfg = devm_platform_ioremap_resource_byname(pdev, "cfg");
	if (IS_ERR(base_cfg))
		return PTR_ERR(base_cfg);

	ed->reg_pec = devm_regmap_init_mmio(dev, base_pec,
					    &dc_ed_pec_regmap_config);
	if (IS_ERR(ed->reg_pec))
		return PTR_ERR(ed->reg_pec);

	ed->reg_cfg = devm_regmap_init_mmio(dev, base_cfg,
					    &dc_ed_cfg_regmap_config);
	if (IS_ERR(ed->reg_cfg))
		return PTR_ERR(ed->reg_cfg);

	ed->irq_shdload = platform_get_irq_byname(pdev, "shdload");
	if (ed->irq_shdload == -EPROBE_DEFER)
		return ed->irq_shdload;
	if (ed->irq_shdload < 0) {
		if (of_device_is_compatible(dev->of_node, "fsl,imx95-dc-extdst"))
			ed->irq_shdload = 0;
		else
			return ed->irq_shdload;
	}

	ed->dev = dev;
	ed->match_data = match_data;

	id = dc_subdev_get_id(match_data->subdev_info,
			      match_data->subdev_info_cnt, res_pec);
	if (id < 0) {
		dev_err(dev, "failed to get instance number: %d\n", id);
		return id;
	}

	switch (id) {
	case 0:
		dc_drm->ed_cont[0] = ed;
		break;
	case 1:
		dc_drm->ed_cont[1] = ed;
		break;
	case 4:
		dc_drm->ed_safe[0] = ed;
		break;
	case 5:
		dc_drm->ed_safe[1] = ed;
		break;
	}

	return 0;
}

static const struct component_ops dc_ed_ops = {
	.bind = dc_ed_bind,
};

static int dc_ed_probe(struct platform_device *pdev)
{
	int ret;

	ret = component_add(&pdev->dev, &dc_ed_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to add component\n");

	return 0;
}

static void dc_ed_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dc_ed_ops);
}

static const struct of_device_id dc_ed_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-dc-extdst",
	  .data = &dc_ed_imx8qxp_data },
	{ .compatible = "fsl,imx95-dc-extdst",
	  .data = &dc_ed_imx95_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_ed_dt_ids);

struct platform_driver dc_ed_driver = {
	.probe = dc_ed_probe,
	.remove = dc_ed_remove,
	.driver = {
		.name = "imx8-dc-extdst",
		.suppress_bind_attrs = true,
		.of_match_table = dc_ed_dt_ids,
	},
};
