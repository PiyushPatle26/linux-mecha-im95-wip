// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/component.h>
#include <linux/ioport.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <drm/drm_fourcc.h>

#include "dc-drv.h"
#include "dc-fu.h"

/* i.MX8QXP legacy macros */
#define BASEADDRESS(x)			(0x10 + FRAC_OFFSET * (x))
#define SOURCEBUFFERATTRIBUTES(x)	(0x14 + FRAC_OFFSET * (x))
#define SOURCEBUFFERDIMENSION(x)	(0x18 + FRAC_OFFSET * (x))
#define COLORCOMPONENTBITS(x)		(0x1c + FRAC_OFFSET * (x))
#define COLORCOMPONENTSHIFT(x)		(0x20 + FRAC_OFFSET * (x))
#define LAYEROFFSET(x)			(0x24 + FRAC_OFFSET * (x))
#define CLIPWINDOWOFFSET(x)		(0x28 + FRAC_OFFSET * (x))
#define CLIPWINDOWDIMENSIONS(x)		(0x2c + FRAC_OFFSET * (x))
#define CONSTANTCOLOR(x)		(0x30 + FRAC_OFFSET * (x))
#define LAYERPROPERTY(x)		(0x34 + FRAC_OFFSET * (x))

#define FRAMEDIMENSIONS_8QXP		0x150
#define FRAMEDIMENSIONS_95		0x1d8

struct dc_fl {
	struct dc_fu fu;
};

static const struct dc_subdev_info dc_fl_info[] = {
	{ .reg_start = 0x56180ac0, .id = 0, },
};

static const u32 dc_fl_link_ids[] = {
	LINK_ID_FETCHLAYER0,
};

static const struct dc_subdev_info dc_fl_imx95_info[] = {
	{ .reg_start = 0x4b5d1000, .id = 0, },
	{ .reg_start = 0x4b5e1000, .id = 1, },
};

static const u32 dc_fl_imx95_link_ids[] = {
	IMX95_LINK_ID_FETCHLAYER0,
	IMX95_LINK_ID_FETCHLAYER1,
};

static const struct regmap_range dc_fl_regmap_ranges[] = {
	regmap_reg_range(STATICCONTROL, FRAMEDIMENSIONS_95),
};

static const struct regmap_access_table dc_fl_regmap_access_table = {
	.yes_ranges = dc_fl_regmap_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_fl_regmap_ranges),
};

static const struct regmap_config dc_fl_cfg_regmap_config = {
	.name = "cfg",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_fl_regmap_access_table,
	.rd_table = &dc_fl_regmap_access_table,
	.max_register = FRAMEDIMENSIONS_95,
	/*
	 * Per-fraction registers return SLVERR on AXI reads until SHDEN is
	 * written to STATICCONTROL.  Use a flat regmap cache so that
	 * regmap_update_bits() reads from the cache (initially 0) rather than
	 * hitting hardware and triggering a synchronous external abort.
	 * dc_fu_common_hw_init() writes SHDEN via direct readl/writel to
	 * preserve hardware-default bits, then syncs the cache so subsequent
	 * regmap operations work correctly.
	 */
	.cache_type = REGCACHE_FLAT,
};

static void dc_fl_set_fmt(struct dc_fu *fu, enum dc_fu_frac frac,
			  const struct drm_format_info *format)
{
	u32 bits = 0, shifts = 0;

	dc_fu_set_src_bpp(fu, frac, format->cpp[0] * 8);

	regmap_write_bits(fu->reg_cfg, fu->reg_layerproperty[frac],
			  YUVCONVERSIONMODE_MASK,
			  YUVCONVERSIONMODE(YUVCONVERSIONMODE_OFF));

	dc_fu_get_pixel_format_bits(fu, format->format, &bits);
	dc_fu_get_pixel_format_shifts(fu, format->format, &shifts);

	regmap_write(fu->reg_cfg, fu->reg_colorcomponentbits[frac], bits);
	regmap_write(fu->reg_cfg, fu->reg_colorcomponentshift[frac], shifts);
}

static void dc_fl_set_framedimensions(struct dc_fu *fu, int w, int h)
{
	regmap_write(fu->reg_cfg, fu->reg_framedimensions,
		     FRAMEWIDTH(w) | FRAMEHEIGHT(h));
}

static void dc_fl_init(struct dc_fu *fu)
{
	dc_fu_common_hw_init(fu);
	dc_fu_shdldreq_sticky(fu, 0xff);
}

/*
 * i.MX95 FetchLayer STATICCONTROL holds only SHDEN; the sticky shadow-load
 * request lives in its own register (SHDLDREQCONTROL). Without this the fetch
 * unit never re-latches its shadow, so a configured plane produces no pixels.
 */
#define DC_FL_IMX95_SHDLDREQCONTROL	0xc
#define DC_FL_IMX95_SHDLDREQSTICKY(lm)	((lm) & 0xff)

static void dc_fl_imx95_init(struct dc_fu *fu)
{
	dc_fu_common_hw_init(fu);
	regmap_write(fu->reg_cfg, DC_FL_IMX95_SHDLDREQCONTROL,
		     DC_FL_IMX95_SHDLDREQSTICKY(0xff));
}

static void dc_fl_set_ops(struct dc_fu *fu)
{
	memcpy(&fu->ops, &dc_fu_common_ops, sizeof(dc_fu_common_ops));
	fu->ops.init = dc_fl_init;
	fu->ops.set_fmt = dc_fl_set_fmt;
	fu->ops.set_framedimensions = dc_fl_set_framedimensions;
}

struct dc_fl_platform {
	const struct dc_subdev_info *subdev_info;
	int subdev_info_cnt;
	const u32 *link_ids;
	int disp_fu_base;
	const struct dc_lib_fu_match_data *fu_mdata;
};

static const struct dc_fl_platform dc_fl_imx8qxp_plat = {
	.subdev_info     = dc_fl_info,
	.subdev_info_cnt = ARRAY_SIZE(dc_fl_info),
	.link_ids        = dc_fl_link_ids,
	.disp_fu_base    = DC_FETCHUNIT_FL0,
	.fu_mdata        = &dc_fu_imx8qxp_data,
};

static const struct dc_fl_platform dc_fl_imx95_plat = {
	.subdev_info     = dc_fl_imx95_info,
	.subdev_info_cnt = ARRAY_SIZE(dc_fl_imx95_info),
	.link_ids        = dc_fl_imx95_link_ids,
	.disp_fu_base    = 0,
	.fu_mdata        = &dc_fu_imx95_data,
};

static int dc_fl_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dc_drm_device *dc_drm = data;
	struct resource *res_pec;
	void __iomem *base_cfg;
	const struct dc_fl_platform *plat;
	struct dc_fl *fl;
	struct dc_fu *fu;
	int i, id;

	fl = devm_kzalloc(dev, sizeof(*fl), GFP_KERNEL);
	if (!fl)
		return -ENOMEM;

	fu = &fl->fu;
	fu->dev = dev;

	plat = of_match_node(dev->driver->of_match_table,
			     dev->of_node)->data;
	if (!plat)
		return -EINVAL;
	fu->match_data = plat->fu_mdata;

	fu->dprc = NULL;

	res_pec = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	base_cfg = devm_platform_ioremap_resource_byname(pdev, "cfg");
	if (IS_ERR(base_cfg))
		return PTR_ERR(base_cfg);

	fu->base_cfg = base_cfg;
	fu->reg_cfg = devm_regmap_init_mmio(dev, base_cfg,
					    &dc_fl_cfg_regmap_config);
	if (IS_ERR(fu->reg_cfg))
		return PTR_ERR(fu->reg_cfg);

	id = dc_subdev_get_id(plat->subdev_info, plat->subdev_info_cnt, res_pec);
	if (id < 0) {
		dev_err(dev, "failed to get instance number: %d\n", id);
		return id;
	}

	fu->id = id;
	for (i = 0; i < plat->subdev_info_cnt; i++) {
		if (plat->subdev_info[i].id == id) {
			fu->link_id = plat->link_ids[i];
			break;
		}
	}

	if (plat == &dc_fl_imx95_plat) {
		fu->reg_offset1 = 0x18;
		fu->reg_offset2 = 0x1d8;
		fu->reg_framedimensions = FRAMEDIMENSIONS_95;
		fu->reg_burstbuffermanagement = 0x10;
		for (i = 0; i < DC_FETCHUNIT_FRAC_NUM; i++) {
			unsigned int ro = fu->reg_offset1 + i * 0x38;
			fu->reg_baseaddr[i]		= _BASEADDRESS + ro;
			fu->reg_sourcebufferattributes[i] = _SOURCEBUFFERATTRIBUTES + ro;
			fu->reg_sourcebufferdimension[i]  = _SOURCEBUFFERDIMENSION + ro;
			fu->reg_colorcomponentbits[i]     = _COLORCOMPONENTBITS + ro;
			fu->reg_colorcomponentshift[i]    = _COLORCOMPONENTSHIFT + ro;
			fu->reg_layeroffset[i]		= _LAYEROFFSET + ro;
			fu->reg_clipwindowoffset[i]	= _CLIPWINDOWOFFSET + ro;
			fu->reg_clipwindowdimensions[i]	= _CLIPWINDOWDIMENSIONS + ro;
			fu->reg_constantcolor[i]	= _CONSTANTCOLOR + ro;
			fu->reg_layerproperty[i]	= _LAYERPROPERTY + ro;
		}
	} else {
		fu->reg_offset1 = 0;
		fu->reg_offset2 = 0;
		fu->reg_framedimensions = FRAMEDIMENSIONS_8QXP;
		fu->reg_burstbuffermanagement = 0xc;
		for (i = 0; i < DC_FETCHUNIT_FRAC_NUM; i++) {
			fu->reg_baseaddr[i]		= BASEADDRESS(i);
			fu->reg_sourcebufferattributes[i] = SOURCEBUFFERATTRIBUTES(i);
			fu->reg_sourcebufferdimension[i]  = SOURCEBUFFERDIMENSION(i);
			fu->reg_colorcomponentbits[i]     = COLORCOMPONENTBITS(i);
			fu->reg_colorcomponentshift[i]    = COLORCOMPONENTSHIFT(i);
			fu->reg_layeroffset[i]		= LAYEROFFSET(i);
			fu->reg_clipwindowoffset[i]	= CLIPWINDOWOFFSET(i);
			fu->reg_clipwindowdimensions[i]	= CLIPWINDOWDIMENSIONS(i);
			fu->reg_constantcolor[i]	= CONSTANTCOLOR(i);
			fu->reg_layerproperty[i]	= LAYERPROPERTY(i);
		}
	}
	snprintf(fu->name, sizeof(fu->name), "FetchLayer%d", id);

	dc_fl_set_ops(fu);

	/* i.MX95 uses a dedicated SHDLDREQCONTROL register for the sticky req. */
	if (plat == &dc_fl_imx95_plat)
		fu->ops.init = dc_fl_imx95_init;

	if (plat->disp_fu_base + id < DC_DISP_FU_CNT)
		dc_drm->fu_disp[plat->disp_fu_base + id] = fu;

	return 0;
}

static const struct component_ops dc_fl_ops = {
	.bind = dc_fl_bind,
};

static int dc_fl_probe(struct platform_device *pdev)
{
	int ret;

	ret = component_add(&pdev->dev, &dc_fl_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to add component\n");

	return 0;
}

static void dc_fl_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dc_fl_ops);
}

static const struct of_device_id dc_fl_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-dc-fetchlayer",
	  .data = &dc_fl_imx8qxp_plat },
	{ .compatible = "fsl,imx95-dc-fetchlayer",
	  .data = &dc_fl_imx95_plat },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_fl_dt_ids);

struct platform_driver dc_fl_driver = {
	.probe = dc_fl_probe,
	.remove = dc_fl_remove,
	.driver = {
		.name = "imx8-dc-fetchlayer",
		.suppress_bind_attrs = true,
		.of_match_table = dc_fl_dt_ids,
	},
};
