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

#include <drm/drm_blend.h>

#define PIXENGCFG_DYNAMIC_SEC_SEL_GET(x)	\
		FIELD_GET(GENMASK(13, 8), (x))

#define PIXENGCFG_DYNAMIC_SEC_SEL_MASK		GENMASK(13, 8)
#define PIXENGCFG_DYNAMIC_SEC_SEL(x)		FIELD_PREP(PIXENGCFG_DYNAMIC_SEC_SEL_MASK, (x))
#define PIXENGCFG_DYNAMIC_PRIM_SEL_MASK	GENMASK(5, 0)
#define PIXENGCFG_DYNAMIC_PRIM_SEL(x)		FIELD_PREP(PIXENGCFG_DYNAMIC_PRIM_SEL_MASK, (x))

#define SHDTOKSEL_MASK				GENMASK(4, 3)
#define SHDTOKSEL(x)				FIELD_PREP(SHDTOKSEL_MASK, (x))
#define SHDLDSEL_MASK				GENMASK(2, 1)
#define SHDLDSEL(x)				FIELD_PREP(SHDLDSEL_MASK, (x))

#define CTRL_MODE_MASK				BIT(0)
#define CTRL_MODE(x)				FIELD_PREP(CTRL_MODE_MASK, (x))

#define ALPHA_MASK				GENMASK(23, 16)
#define ALPHA(x)				FIELD_PREP(ALPHA_MASK, (x))
#define SEC_A_BLD_FUNC_MASK			GENMASK(14, 12)
#define SEC_A_BLD_FUNC(x)			FIELD_PREP(SEC_A_BLD_FUNC_MASK, (x))
#define PRIM_A_BLD_FUNC_MASK			GENMASK(10, 8)
#define PRIM_A_BLD_FUNC(x)			FIELD_PREP(PRIM_A_BLD_FUNC_MASK, (x))
#define SEC_C_BLD_FUNC_MASK			GENMASK(6, 4)
#define SEC_C_BLD_FUNC(x)			FIELD_PREP(SEC_C_BLD_FUNC_MASK, (x))
#define PRIM_C_BLD_FUNC_MASK			GENMASK(2, 0)
#define PRIM_C_BLD_FUNC(x)			FIELD_PREP(PRIM_C_BLD_FUNC_MASK, (x))

#define YPOS_MASK				GENMASK(31, 16)
#define YPOS(x)					FIELD_PREP(YPOS_MASK, (x))
#define XPOS_MASK				GENMASK(15, 0)
#define XPOS(x)					FIELD_PREP(XPOS_MASK, (x))

enum dc_lib_lb_blend_func {
	LB_BLEND_ZERO,
	LB_BLEND_ONE,
	LB_BLEND_PRIM_ALPHA,
	LB_BLEND_ONE_MINUS_PRIM_ALPHA,
	LB_BLEND_SEC_ALPHA,
	LB_BLEND_ONE_MINUS_SEC_ALPHA,
	LB_BLEND_CONST_ALPHA,
	LB_BLEND_ONE_MINUS_CONST_ALPHA,
};

/* i.MX8QXP instance layout (ids 0-3) */
static const struct dc_subdev_info dc_lb_info[] = {
	{ .reg_start = 0x56180ba0, .id = 0, },
	{ .reg_start = 0x56180bc0, .id = 1, },
	{ .reg_start = 0x56180be0, .id = 2, },
	{ .reg_start = 0x56180c00, .id = 3, },
};

static const u32 dc_lb_link_ids[] = {
	LINK_ID_LAYERBLEND0,
	LINK_ID_LAYERBLEND1,
	LINK_ID_LAYERBLEND2,
	LINK_ID_LAYERBLEND3,
};

static const u32 dc_lb_prim_sels[] = {
	LINK_ID_NONE,
	LINK_ID_CONSTFRAME0,
	LINK_ID_CONSTFRAME1,
	LINK_ID_CONSTFRAME4,
	LINK_ID_CONSTFRAME5,
	LINK_ID_LAYERBLEND0,
	LINK_ID_LAYERBLEND1,
	LINK_ID_LAYERBLEND2,
	LINK_ID_LAYERBLEND3,
};

static const u32 dc_lb_sec_sels[] = {
	LINK_ID_NONE,
	LINK_ID_FETCHWARP2,
	LINK_ID_FETCHLAYER0,
};

static const struct dc_lib_lb_match_data dc_lb_imx8qxp_data = {
	.subdev_info         = dc_lb_info,
	.subdev_info_cnt     = ARRAY_SIZE(dc_lb_info),
	.link_ids            = dc_lb_link_ids,
	.prim_sels           = dc_lb_prim_sels,
	.prim_sels_cnt       = ARRAY_SIZE(dc_lb_prim_sels),
	.sec_sels            = dc_lb_sec_sels,
	.sec_sels_cnt        = ARRAY_SIZE(dc_lb_sec_sels),
	.fixed_prim_sels_num = 6,
};

/* i.MX95 instance layout: PEC addresses are base 0x4b400000 + aux_ofs from dpu95-core */
static const struct dc_subdev_info dc_lb_imx95_info[] = {
	{ .reg_start = 0x4b571000, .id = 1, },
	{ .reg_start = 0x4b581000, .id = 2, },
	{ .reg_start = 0x4b591000, .id = 3, },
	{ .reg_start = 0x4b5a1000, .id = 4, },
	{ .reg_start = 0x4b5b1000, .id = 5, },
	{ .reg_start = 0x4b5c1000, .id = 6, },
};

static const u32 dc_lb_imx95_link_ids[] = {
	IMX95_LINK_ID_LAYERBLEND1,
	IMX95_LINK_ID_LAYERBLEND2,
	IMX95_LINK_ID_LAYERBLEND3,
	IMX95_LINK_ID_LAYERBLEND4,
	IMX95_LINK_ID_LAYERBLEND5,
	IMX95_LINK_ID_LAYERBLEND6,
};

/*
 * PRIM selects the "stage" below this LayerBlend: a ConstFrame (background) or
 * a lower LayerBlend's output. SEC selects the plane source (a fetch unit or a
 * scaler). The DomainBlend/blendcontrol emits the SEC input, so the plane's
 * FetchLayer must land on SEC and the ConstFrame background on PRIM. These
 * lists must mirror the vendor dpu95 driver, otherwise prim_sel(ConstFrame) /
 * sec_sel(FetchLayer) silently no-op and no content reaches the FrameGen.
 */
static const u32 dc_lb_imx95_prim_sels[] = {
	IMX95_LINK_ID_NONE,
	IMX95_LINK_ID_CONSTFRAME0,
	IMX95_LINK_ID_CONSTFRAME1,
	IMX95_LINK_ID_CONSTFRAME4,
	IMX95_LINK_ID_CONSTFRAME5,
	IMX95_LINK_ID_LAYERBLEND1,
	IMX95_LINK_ID_LAYERBLEND2,
	IMX95_LINK_ID_LAYERBLEND3,
	IMX95_LINK_ID_LAYERBLEND4,
	IMX95_LINK_ID_LAYERBLEND5,
	IMX95_LINK_ID_LAYERBLEND6,
};

static const u32 dc_lb_imx95_sec_sels[] = {
	IMX95_LINK_ID_NONE,
	IMX95_LINK_ID_FETCHLAYER0,
	IMX95_LINK_ID_FETCHLAYER1,
	IMX95_LINK_ID_FETCHYUV0,
	IMX95_LINK_ID_FETCHYUV1,
	IMX95_LINK_ID_FETCHYUV2,
	IMX95_LINK_ID_FETCHYUV3,
	IMX95_LINK_ID_HSCALER4,
	IMX95_LINK_ID_VSCALER4,
	IMX95_LINK_ID_MATRIX4,
};

static const struct dc_lib_lb_match_data dc_lb_imx95_data = {
	.subdev_info         = dc_lb_imx95_info,
	.subdev_info_cnt     = ARRAY_SIZE(dc_lb_imx95_info),
	.link_ids            = dc_lb_imx95_link_ids,
	.prim_sels           = dc_lb_imx95_prim_sels,
	.prim_sels_cnt       = ARRAY_SIZE(dc_lb_imx95_prim_sels),
	.sec_sels            = dc_lb_imx95_sec_sels,
	.sec_sels_cnt        = ARRAY_SIZE(dc_lb_imx95_sec_sels),
	/*
	 * Fixed (non-LayerBlend) prim options = NONE + 4 ConstFrames. The lib
	 * loops i < fixed + lb->id, so with ids 1..6 every LB can always reach
	 * the ConstFrames and LB(id) can also reach the lower LayerBlends
	 * (mirrors the vendor "ARRAY_SIZE - 6 + id - 1" window).
	 */
	.fixed_prim_sels_num = 4,
};

static const struct regmap_range dc_lb_pec_regmap_access_ranges[] = {
	regmap_reg_range(LB_PIXENGCFG_DYNAMIC, LB_PIXENGCFG_DYNAMIC),
};

static const struct regmap_access_table dc_lb_pec_regmap_access_table = {
	.yes_ranges = dc_lb_pec_regmap_access_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_lb_pec_regmap_access_ranges),
};

static const struct regmap_config dc_lb_pec_regmap_config = {
	.name = "pec",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_lb_pec_regmap_access_table,
	.rd_table = &dc_lb_pec_regmap_access_table,
	.max_register = LB_PIXENGCFG_DYNAMIC,
};

static const struct regmap_range dc_lb_regmap_ranges[] = {
	regmap_reg_range(LB_STATICCONTROL, LB_POSITION),
};

static const struct regmap_access_table dc_lb_regmap_access_table = {
	.yes_ranges = dc_lb_regmap_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_lb_regmap_ranges),
};

static const struct regmap_config dc_lb_cfg_regmap_config = {
	.name = "cfg",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_lb_regmap_access_table,
	.rd_table = &dc_lb_regmap_access_table,
	.max_register = LB_POSITION,
};

enum dc_link_id dc_lb_get_link_id(struct dc_lb *lb)
{
	return lb->link;
}
EXPORT_SYMBOL_GPL(dc_lb_get_link_id);

void dc_lb_pec_dynamic_prim_sel(struct dc_lb *lb, enum dc_link_id prim)
{
	const u32 *prim_sels = lb->match_data->prim_sels;
	int cnt = lb->match_data->prim_sels_cnt;
	int i;

	for (i = 0; i < lb->match_data->fixed_prim_sels_num + lb->id; i++) {
		if (i >= cnt)
			break;
		if (prim_sels[i] == prim) {
			regmap_write_bits(lb->reg_pec, LB_PIXENGCFG_DYNAMIC,
					  PIXENGCFG_DYNAMIC_PRIM_SEL_MASK,
					  PIXENGCFG_DYNAMIC_PRIM_SEL(prim));
			return;
		}
	}
}
EXPORT_SYMBOL_GPL(dc_lb_pec_dynamic_prim_sel);

void dc_lb_pec_dynamic_sec_sel(struct dc_lb *lb, enum dc_link_id sec)
{
	const u32 *sec_sels = lb->match_data->sec_sels;
	int i;

	for (i = 0; i < lb->match_data->sec_sels_cnt; i++) {
		if (sec_sels[i] == sec) {
			regmap_write_bits(lb->reg_pec, LB_PIXENGCFG_DYNAMIC,
					  PIXENGCFG_DYNAMIC_SEC_SEL_MASK,
					  PIXENGCFG_DYNAMIC_SEC_SEL(sec));
			return;
		}
	}
}
EXPORT_SYMBOL_GPL(dc_lb_pec_dynamic_sec_sel);

int dc_lb_pec_dynamic_sec_sel_get(struct dc_lb *lb, enum dc_link_id *sec)
{
	const u32 *sels = lb->match_data->sec_sels;
	int cnt = lb->match_data->sec_sels_cnt;
	u32 val;
	int i;

	regmap_read(lb->reg_pec, LB_PIXENGCFG_DYNAMIC, &val);
	val = PIXENGCFG_DYNAMIC_SEC_SEL_GET(val);

	for (i = 0; i < cnt; i++) {
		if (sels[i] == val) {
			*sec = val;
			dev_dbg(lb->dev, "get secondary input selection: 0x%02x\n", val);
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(dc_lb_pec_dynamic_sec_sel_get);

void dc_lb_pec_clken(struct dc_lb *lb, enum dc_pec_clken clken)
{
	regmap_write_bits(lb->reg_pec, LB_PIXENGCFG_DYNAMIC, CLKEN_MASK,
			  CLKEN(clken));
}
EXPORT_SYMBOL_GPL(dc_lb_pec_clken);

void dc_lb_mode(struct dc_lb *lb, enum dc_lb_mode mode)
{
	regmap_write_bits(lb->reg_cfg, LB_CONTROL, CTRL_MODE_MASK,
			  CTRL_MODE(mode));
}
EXPORT_SYMBOL_GPL(dc_lb_mode);

void dc_lb_position(struct dc_lb *lb, int x, int y)
{
	regmap_write(lb->reg_cfg, LB_POSITION, XPOS(x) | YPOS(y));
}
EXPORT_SYMBOL_GPL(dc_lb_position);

int dc_lb_get_id(struct dc_lb *lb)
{
	return lb->id;
}
EXPORT_SYMBOL_GPL(dc_lb_get_id);

static void dc_lb_shdtoksel(struct dc_lb *lb, u32 sel)
{
	regmap_write_bits(lb->reg_cfg, LB_STATICCONTROL, SHDTOKSEL_MASK,
			  SHDTOKSEL(sel));
}

static void dc_lb_shdldsel(struct dc_lb *lb, u32 sel)
{
	regmap_write_bits(lb->reg_cfg, LB_STATICCONTROL, SHDLDSEL_MASK,
			  SHDLDSEL(sel));
}

static void dc_lb_blendcontrol(struct dc_lb *lb)
{
	u32 val = PRIM_A_BLD_FUNC(LB_BLEND_ZERO) |
		  SEC_A_BLD_FUNC(LB_BLEND_ZERO) |
		  PRIM_C_BLD_FUNC(LB_BLEND_ZERO) |
		  SEC_C_BLD_FUNC(LB_BLEND_CONST_ALPHA) |
		  ALPHA(DRM_BLEND_ALPHA_OPAQUE >> 8);

	regmap_write(lb->reg_cfg, LB_BLENDCONTROL, val);
}

void dc_lb_init(struct dc_lb *lb)
{
	regmap_write_bits(lb->reg_pec, LB_PIXENGCFG_DYNAMIC,
			  CLKEN_MASK |
			  PIXENGCFG_DYNAMIC_PRIM_SEL_MASK |
			  PIXENGCFG_DYNAMIC_SEC_SEL_MASK,
			  CLKEN(0));
	dc_lb_shdldsel(lb, 0x2);
	dc_lb_shdtoksel(lb, 0x2);
	dc_lb_blendcontrol(lb);
	regmap_write_bits(lb->reg_cfg, LB_STATICCONTROL, SHDEN, SHDEN);
}
EXPORT_SYMBOL_GPL(dc_lb_init);

static int dc_lb_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct dc_lib_lb_match_data *match_data;
	struct dc_drm_device *dc_drm = data;
	struct resource *res_pec;
	void __iomem *base_pec;
	void __iomem *base_cfg;
	struct dc_lb *lb;
	int i;

	match_data = of_match_node(dev->driver->of_match_table,
				   dev->of_node)->data;
	if (!match_data)
		return -EINVAL;

	lb = devm_kzalloc(dev, sizeof(*lb), GFP_KERNEL);
	if (!lb)
		return -ENOMEM;

	base_pec = devm_platform_get_and_ioremap_resource(pdev, 0, &res_pec);
	if (IS_ERR(base_pec))
		return PTR_ERR(base_pec);

	base_cfg = devm_platform_ioremap_resource_byname(pdev, "cfg");
	if (IS_ERR(base_cfg))
		return PTR_ERR(base_cfg);

	lb->reg_pec = devm_regmap_init_mmio(dev, base_pec,
					    &dc_lb_pec_regmap_config);
	if (IS_ERR(lb->reg_pec))
		return PTR_ERR(lb->reg_pec);

	lb->reg_cfg = devm_regmap_init_mmio(dev, base_cfg,
					    &dc_lb_cfg_regmap_config);
	if (IS_ERR(lb->reg_cfg))
		return PTR_ERR(lb->reg_cfg);

	lb->id = dc_subdev_get_id(match_data->subdev_info,
				  match_data->subdev_info_cnt, res_pec);
	if (lb->id < 0) {
		dev_err(dev, "failed to get instance number: %d\n", lb->id);
		return lb->id;
	}

	for (i = 0; i < match_data->subdev_info_cnt; i++) {
		if (match_data->subdev_info[i].id == lb->id) {
			lb->link = match_data->link_ids[i];
			break;
		}
	}

	lb->dev = dev;
	lb->match_data = match_data;

	if (lb->id < DC_LB_CNT)
		dc_drm->lb[lb->id] = lb;

	return 0;
}

static const struct component_ops dc_lb_ops = {
	.bind = dc_lb_bind,
};

static int dc_lb_probe(struct platform_device *pdev)
{
	int ret;

	ret = component_add(&pdev->dev, &dc_lb_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to add component\n");

	return 0;
}

static void dc_lb_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dc_lb_ops);
}

static const struct of_device_id dc_lb_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-dc-layerblend",
	  .data = &dc_lb_imx8qxp_data },
	{ .compatible = "fsl,imx95-dc-layerblend",
	  .data = &dc_lb_imx95_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_lb_dt_ids);

struct platform_driver dc_lb_driver = {
	.probe = dc_lb_probe,
	.remove = dc_lb_remove,
	.driver = {
		.name = "imx8-dc-layerblend",
		.suppress_bind_attrs = true,
		.of_match_table = dc_lb_dt_ids,
	},
};
