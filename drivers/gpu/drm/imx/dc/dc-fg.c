// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/units.h>

#include <drm/drm_modes.h>

#include "dc-de.h"
#include "dc-drv.h"
#include "dc-lib.h"

#define FGSYNCMODE_MASK		GENMASK(2, 1)
#define FGSYNCMODE(x)		FIELD_PREP(FGSYNCMODE_MASK, (x))

#define FGSHDTOKGENSYNCMODE_MASK	BIT(4)
#define FGSHDTOKGENSYNCMODE(x)		((x) << 4)
#define FG_SHDTOKGENSYNCMODE_LOCAL	0

#define FGDM_MASK		GENMASK(2, 0)

#define PRIMSYNCSTAT		BIT(8)
#define SECSYNCSTAT		BIT(24)
#define SFIFOEMPTY		BIT(16)
#define CLRPRIMSTAT		BIT(0)
#define CLRSECSTAT		BIT(16)
#define EN			BIT(31)

#define HTOTAL(x)		FIELD_PREP(GENMASK(29, 16), ((x) - 1))
#define HACT(x)			FIELD_PREP(GENMASK(13, 0), (x))
#define HSEN			BIT(31)
#define HSBP(x)			FIELD_PREP(GENMASK(29, 16), ((x) - 1))
#define HSYNC(x)		FIELD_PREP(GENMASK(13, 0), ((x) - 1))
#define VTOTAL(x)		FIELD_PREP(GENMASK(29, 16), ((x) - 1))
#define VACT(x)			FIELD_PREP(GENMASK(13, 0), (x))
#define VSEN			BIT(31)
#define VSBP(x)			FIELD_PREP(GENMASK(29, 16), ((x) - 1))
#define VSYNC(x)		FIELD_PREP(GENMASK(13, 0), ((x) - 1))

#define ROW(x)			FIELD_PREP(GENMASK(29, 16), (x))
#define COL(x)			FIELD_PREP(GENMASK(13, 0), (x))

#define STARTY(x)		FIELD_PREP(GENMASK(29, 16), ((x) + 1))
#define STARTX(x)		FIELD_PREP(GENMASK(13, 0), ((x) + 1))

#define ENSECALPHA		BIT(4)
#define ENPRIMALPHA		BIT(3)

#define CCGREEN(x)		FIELD_PREP(GENMASK(19, 10), (x))

#define FGEN			BIT(0)
#define SHDTOKGEN		BIT(0)

#define FRAMEINDEX(x)		FIELD_GET(GENMASK(31, 14), (x))
#define LINEINDEX(x)		FIELD_GET(GENMASK(13, 0), (x))

enum dc_fg_syncmode {
	FG_SYNCMODE_OFF,
};

enum dc_fg_dm {
	FG_DM_OFF,
	FG_DM_CONSTCOL = 0x1,
	FG_DM_PRIM = 0x2,
	FG_DM_SEC_ON_TOP = 0x5,
};

/* i.MX8QXP FrameGen non-timing register offsets */
static const struct dc_lib_fg_offsets dc_fg_imx8qxp_offsets = {
	.timing_base	= 0x8,
	.pkickconfig	= 0x2c,
	.skickconfig	= 0x30,
	.pacfg		= 0x54,
	.sacfg		= 0x58,
	.fginctrl	= 0x5c,
	.fginctrlpanic	= 0x60,
	.fgccr		= 0x64,
	.fgenable	= 0x68,
	.fgslr		= 0x6c,
	.fgtimestamp	= 0x74,
	.fgchstat	= 0x78,
	.fgchstatclr	= 0x7c,
	.pfifotres	= 0xb4,
};

/* i.MX95 FrameGen non-timing register offsets */
static const struct dc_lib_fg_offsets dc_fg_imx95_offsets = {
	.timing_base	= 0x8,
	.pkickconfig	= 0x2c,
	.skickconfig	= 0x30,
	.pacfg		= 0x78,
	.sacfg		= 0x7c,
	.fginctrl	= 0x80,
	.fginctrlpanic	= 0x84,
	.fgccr		= 0x88,
	.fgenable	= 0x8c,
	.fgslr		= 0x90,
	.fgtimestamp	= 0x98,
	.fgchstat	= 0x9c,
	.fgchstatclr	= 0xa0,
	.pfifotres	= 0xb4,
};

static const struct dc_subdev_info dc_fg_info[] = {
	{ .reg_start = 0x5618b800, .id = 0, },
	{ .reg_start = 0x5618d400, .id = 1, },
};

/* i.MX95: base 0x4b400000 + fg_ofss from dpu95-core (no PEC for FG) */
static const struct dc_subdev_info dc_fg_imx95_info[] = {
	{ .reg_start = 0x4b6b0000, .id = 0, },
	{ .reg_start = 0x4b730000, .id = 1, },
};

/* i.MX8QXP regmap access ranges */
static const struct regmap_range dc_fg_regmap_write_ranges[] = {
	regmap_reg_range(FG_FGSTCTRL, FG_VTCFG2),
	regmap_reg_range(0x2c, 0x30),
	regmap_reg_range(0x54, 0x6c),
	regmap_reg_range(0x7c, 0x7c),
	regmap_reg_range(0xb4, 0xb4),
};

static const struct regmap_range dc_fg_regmap_read_ranges[] = {
	regmap_reg_range(FG_FGSTCTRL, FG_VTCFG2),
	regmap_reg_range(0x2c, 0x30),
	regmap_reg_range(0x54, 0x68),
	regmap_reg_range(0x74, 0x78),
};

static const struct regmap_access_table dc_fg_regmap_write_table = {
	.yes_ranges = dc_fg_regmap_write_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_fg_regmap_write_ranges),
};

static const struct regmap_access_table dc_fg_regmap_read_table = {
	.yes_ranges = dc_fg_regmap_read_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_fg_regmap_read_ranges),
};

static const struct regmap_config dc_fg_imx8qxp_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_fg_regmap_write_table,
	.rd_table = &dc_fg_regmap_read_table,
	.max_register = 0xb4,
};

/* i.MX95 regmap access ranges (registers extend to 0xa0) */
static const struct regmap_range dc_fg_imx95_regmap_write_ranges[] = {
	regmap_reg_range(0x0, 0x18),
	regmap_reg_range(0x2c, 0x30),
	regmap_reg_range(0x78, 0x90),
	regmap_reg_range(0xa0, 0xa0),
	regmap_reg_range(0xb4, 0xb4),
};

static const struct regmap_range dc_fg_imx95_regmap_read_ranges[] = {
	regmap_reg_range(0x0, 0x18),
	regmap_reg_range(0x2c, 0x30),
	regmap_reg_range(0x78, 0x8c),
	regmap_reg_range(0x98, 0x9c),
};

static const struct regmap_access_table dc_fg_imx95_regmap_write_table = {
	.yes_ranges = dc_fg_imx95_regmap_write_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_fg_imx95_regmap_write_ranges),
};

static const struct regmap_access_table dc_fg_imx95_regmap_read_table = {
	.yes_ranges = dc_fg_imx95_regmap_read_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_fg_imx95_regmap_read_ranges),
};

static const struct regmap_config dc_fg_imx95_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &dc_fg_imx95_regmap_write_table,
	.rd_table = &dc_fg_imx95_regmap_read_table,
	.max_register = 0xb4,
};

static const struct dc_lib_fg_match_data dc_fg_imx8qxp_data = {
	.subdev_info     = dc_fg_info,
	.subdev_info_cnt = ARRAY_SIZE(dc_fg_info),
	.offsets         = &dc_fg_imx8qxp_offsets,
	.regmap_cfg      = &dc_fg_imx8qxp_regmap_config,
};

static const struct dc_lib_fg_match_data dc_fg_imx95_data = {
	.subdev_info     = dc_fg_imx95_info,
	.subdev_info_cnt = ARRAY_SIZE(dc_fg_imx95_info),
	.offsets         = &dc_fg_imx95_offsets,
	.regmap_cfg      = &dc_fg_imx95_regmap_config,
};

void dc_fg_cfg_videomode(struct dc_fg *fg, struct drm_display_mode *m)
{
	const struct dc_lib_fg_offsets *offsets = fg->offsets;
	u32 hact, htotal, hsync, hsbp;
	u32 vact, vtotal, vsync, vsbp;
	u32 kick_row, kick_col;
	int ret;

	hact = m->crtc_hdisplay;
	htotal = m->crtc_htotal;
	hsync = m->crtc_hsync_end - m->crtc_hsync_start;
	hsbp = m->crtc_htotal - m->crtc_hsync_start;

	vact = m->crtc_vdisplay;
	vtotal = m->crtc_vtotal;
	vsync = m->crtc_vsync_end - m->crtc_vsync_start;
	vsbp = m->crtc_vtotal - m->crtc_vsync_start;

	regmap_write(fg->reg, offsets->timing_base + 0x4,
		     HACT(hact) | HTOTAL(htotal));
	regmap_write(fg->reg, offsets->timing_base + 0x8,
		     HSYNC(hsync) | HSBP(hsbp) | HSEN);
	regmap_write(fg->reg, offsets->timing_base + 0xc,
		     VACT(vact) | VTOTAL(vtotal));
	regmap_write(fg->reg, offsets->timing_base + 0x10,
		     VSYNC(vsync) | VSBP(vsbp) | VSEN);

	/*
	 * Disable the primary-FIFO panic mechanism.  FGPFIFOTRES resets to a
	 * non-zero fill threshold; leaving it armed makes the FrameGen assert
	 * panic as soon as the primary FIFO isn't full, which is immediately
	 * at startup when it's empty.  In panic mode the FG shows FGINCTRLPANIC
	 * (green constcolor) and stops kicking the pixel pipeline, so ExtDst is
	 * never kicked, no pixels arrive, the FIFO stays empty (PFIFOEMPTY=1),
	 * and the primary channel never syncs up (PRIMSYNCSTAT=0): a
	 * self-sustaining panic loop.  Writing 0 matches the downstream/vendor
	 * driver and breaks the loop.
	 */
	regmap_write(fg->reg, offsets->pfifotres, 0);

	kick_col = hact + 1;
	kick_row = vact;

	/*
	 * Only the primary channel is kicked/positioned here.  Enabling the
	 * secondary kick (SKICKCONFIG) makes the FrameGen wait for the safety
	 * stream to sync every frame, which blocks the primary channel from
	 * syncing up (FGCHSTAT.PRIMSYNCSTAT never sets) and the panic colour
	 * (green) is shown instead of content.  The secondary kick and area
	 * config are therefore left at their reset values, matching the
	 * downstream/vendor driver.
	 */
	regmap_write(fg->reg, offsets->pkickconfig,
		     COL(kick_col) | ROW(kick_row) | EN);

	regmap_write(fg->reg, offsets->pacfg, STARTX(0) | STARTY(0));

	regmap_write_bits(fg->reg, offsets->fginctrl,
			  ENPRIMALPHA | ENSECALPHA, 0);
	regmap_write_bits(fg->reg, offsets->fginctrlpanic,
			  ENPRIMALPHA | ENSECALPHA, 0);

	regmap_write(fg->reg, offsets->fgccr, CCGREEN(0x3ff));

	if (!fg->clk_disp)
		return;
	ret = clk_set_rate(fg->clk_disp, m->clock * HZ_PER_KHZ);
	if (ret < 0)
		dev_err(fg->dev, "failed to set display clock rate: %d\n", ret);
}
EXPORT_SYMBOL_GPL(dc_fg_cfg_videomode);

void dc_fg_enable(struct dc_fg *fg)
{
	regmap_write(fg->reg, fg->offsets->fgenable, FGEN);
}
EXPORT_SYMBOL_GPL(dc_fg_enable);

void dc_fg_disable(struct dc_fg *fg)
{
	regmap_write(fg->reg, fg->offsets->fgenable, 0);
}
EXPORT_SYMBOL_GPL(dc_fg_disable);

void dc_fg_shdtokgen(struct dc_fg *fg)
{
	regmap_write(fg->reg, fg->offsets->fgslr, SHDTOKGEN);
}
EXPORT_SYMBOL_GPL(dc_fg_shdtokgen);

u32 dc_fg_get_frame_index(struct dc_fg *fg)
{
	u32 val;

	regmap_read(fg->reg, fg->offsets->fgtimestamp, &val);

	return FRAMEINDEX(val);
}
EXPORT_SYMBOL_GPL(dc_fg_get_frame_index);

u32 dc_fg_get_line_index(struct dc_fg *fg)
{
	u32 val;

	regmap_read(fg->reg, fg->offsets->fgtimestamp, &val);

	return LINEINDEX(val);
}
EXPORT_SYMBOL_GPL(dc_fg_get_line_index);

bool dc_fg_wait_for_frame_index_moving(struct dc_fg *fg)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100);
	u32 frame_index, last_frame_index;

	frame_index = dc_fg_get_frame_index(fg);
	do {
		last_frame_index = frame_index;
		frame_index = dc_fg_get_frame_index(fg);
	} while (last_frame_index == frame_index &&
		 time_before(jiffies, timeout));

	return last_frame_index != frame_index;
}
EXPORT_SYMBOL_GPL(dc_fg_wait_for_frame_index_moving);

bool dc_fg_wait_for_primary_syncup(struct dc_fg *fg)
{
	unsigned int val;

	return regmap_read_poll_timeout(fg->reg, fg->offsets->fgchstat,
					val, val & PRIMSYNCSTAT, 5, 100000);
}
EXPORT_SYMBOL_GPL(dc_fg_wait_for_primary_syncup);

void dc_fg_primary_clear_channel_status(struct dc_fg *fg)
{
	regmap_write(fg->reg, fg->offsets->fgchstatclr, CLRPRIMSTAT);
}
EXPORT_SYMBOL_GPL(dc_fg_primary_clear_channel_status);

bool dc_fg_secondary_requests_to_read_empty_fifo(struct dc_fg *fg)
{
	u32 val;

	regmap_read(fg->reg, fg->offsets->fgchstat, &val);

	return !!(val & SFIFOEMPTY);
}
EXPORT_SYMBOL_GPL(dc_fg_secondary_requests_to_read_empty_fifo);

void dc_fg_secondary_clear_channel_status(struct dc_fg *fg)
{
	regmap_write(fg->reg, fg->offsets->fgchstatclr, CLRSECSTAT);
}
EXPORT_SYMBOL_GPL(dc_fg_secondary_clear_channel_status);

int dc_fg_wait_for_secondary_syncup(struct dc_fg *fg)
{
	unsigned int val;

	return regmap_read_poll_timeout(fg->reg, fg->offsets->fgchstat,
					val, val & SECSYNCSTAT, 5, 100000);
}
EXPORT_SYMBOL_GPL(dc_fg_wait_for_secondary_syncup);

void dc_fg_enable_clock(struct dc_fg *fg)
{
	int ret;

	if (!fg->clk_disp)
		return;
	ret = clk_prepare_enable(fg->clk_disp);
	if (ret)
		dev_err(fg->dev, "failed to enable display clock: %d\n", ret);
}
EXPORT_SYMBOL_GPL(dc_fg_enable_clock);

void dc_fg_disable_clock(struct dc_fg *fg)
{
	if (!fg->clk_disp)
		return;
	clk_disable_unprepare(fg->clk_disp);
}
EXPORT_SYMBOL_GPL(dc_fg_disable_clock);

enum drm_mode_status dc_fg_check_clock(struct dc_fg *fg, int clk_khz)
{
	unsigned long rounded_rate;

	if (!fg->clk_disp)
		return MODE_OK;
	rounded_rate = clk_round_rate(fg->clk_disp, clk_khz * HZ_PER_KHZ);

	if (rounded_rate != clk_khz * HZ_PER_KHZ)
		return MODE_NOCLOCK;

	return MODE_OK;
}
EXPORT_SYMBOL_GPL(dc_fg_check_clock);

void dc_fg_init(struct dc_fg *fg)
{
	regmap_write_bits(fg->reg, fg->offsets->timing_base + 0, SHDEN, SHDEN);
	regmap_write_bits(fg->reg, fg->offsets->timing_base + 0,
			  FGSYNCMODE_MASK | FGSHDTOKGENSYNCMODE_MASK,
			  FGSYNCMODE(FG_SYNCMODE_OFF) |
			  FGSHDTOKGENSYNCMODE(FG_SHDTOKGENSYNCMODE_LOCAL));
	regmap_write_bits(fg->reg, fg->offsets->fginctrl,
			  FGDM_MASK, FG_DM_PRIM);
	regmap_write_bits(fg->reg, fg->offsets->fginctrlpanic,
			  FGDM_MASK, FG_DM_CONSTCOL);
}
EXPORT_SYMBOL_GPL(dc_fg_init);

static int dc_fg_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct dc_lib_fg_match_data *match_data;
	struct dc_drm_device *dc_drm = data;
	struct resource *res;
	void __iomem *base;
	struct dc_fg *fg;
	int id;

	match_data = of_match_node(dev->driver->of_match_table,
				   dev->of_node)->data;
	if (!match_data)
		return -EINVAL;

	fg = devm_kzalloc(dev, sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return -ENOMEM;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	fg->reg = devm_regmap_init_mmio(dev, base, match_data->regmap_cfg);
	if (IS_ERR(fg->reg))
		return PTR_ERR(fg->reg);

	fg->clk_disp = devm_clk_get(dev, NULL);
	if (IS_ERR(fg->clk_disp)) {
		if (of_device_is_compatible(dev->of_node, "fsl,imx95-dc-framegen")) {
			fg->clk_disp = NULL;
		} else {
			return dev_err_probe(dev, PTR_ERR(fg->clk_disp),
					     "failed to get display clock\n");
		}
	}

	id = dc_subdev_get_id(match_data->subdev_info,
			      match_data->subdev_info_cnt, res);
	if (id < 0) {
		dev_err(dev, "failed to get instance number: %d\n", id);
		return id;
	}

	fg->dev = dev;
	fg->id = id;
	fg->offsets = match_data->offsets;
	dc_drm->fg[id] = fg;

	return 0;
}

static const struct component_ops dc_fg_ops = {
	.bind = dc_fg_bind,
};

static int dc_fg_probe(struct platform_device *pdev)
{
	int ret;

	ret = component_add(&pdev->dev, &dc_fg_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to add component\n");

	return 0;
}

static void dc_fg_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dc_fg_ops);
}

static const struct of_device_id dc_fg_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-dc-framegen",
	  .data = &dc_fg_imx8qxp_data },
	{ .compatible = "fsl,imx95-dc-framegen",
	  .data = &dc_fg_imx95_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_fg_dt_ids);

struct platform_driver dc_fg_driver = {
	.probe = dc_fg_probe,
	.remove = dc_fg_remove,
	.driver = {
		.name = "imx8-dc-framegen",
		.suppress_bind_attrs = true,
		.of_match_table = dc_fg_dt_ids,
	},
};
