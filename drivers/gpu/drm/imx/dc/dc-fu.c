// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/io.h>
#include <linux/math.h>
#include <linux/module.h>

#include "dc-fu.h"
#include "dc-pe.h"
#include "dc-prg.h"

/* STATICCONTROL */
#define SHDLDREQSTICKY_MASK		GENMASK(31, 24)
#define SHDLDREQSTICKY(x)		FIELD_PREP(SHDLDREQSTICKY_MASK, (x))
#define BASEADDRESSAUTOUPDATE_MASK	GENMASK(23, 16)
#define BASEADDRESSAUTOUPDATE(x)	FIELD_PREP(BASEADDRESSAUTOUPDATE_MASK, (x))

/* BURSTBUFFERMANAGEMENT */
#define LINEMODE_MASK			BIT(31)
#define COMBINERLINEFLUSH_ENABLE	BIT(30)
#define COMBINERTIMEOUT_ENABLE		BIT(29)
#define SETMAXBURSTLENGTH_MASK		GENMASK(20, 13)
#define SETMAXBURSTLENGTH(x)		FIELD_PREP(SETMAXBURSTLENGTH_MASK, (x))
#define SETBURSTLENGTH_MASK		GENMASK(12, 8)
#define SETBURSTLENGTH(x)		FIELD_PREP(SETBURSTLENGTH_MASK, (x))
#define SETNUMBUFFERS_MASK		GENMASK(7, 0)
#define SETNUMBUFFERS(x)		FIELD_PREP(SETNUMBUFFERS_MASK, (x))

/* SOURCEBUFFERATTRIBUTES */
#define BITSPERPIXEL_MASK		GENMASK(21, 16)
#define BITSPERPIXEL(x)			FIELD_PREP(BITSPERPIXEL_MASK, (x))
#define STRIDE_MASK			GENMASK(15, 0)
#define STRIDE(x)			FIELD_PREP(STRIDE_MASK, (x) - 1)

/* SOURCEBUFFERDIMENSION */
#define LINECOUNT(x)			FIELD_PREP(GENMASK(29, 16), (x) - 1)
#define LINEWIDTH(x)			FIELD_PREP(GENMASK(13, 0), (x) - 1)

/* LAYEROFFSET */
#define LAYERYOFFSET(x)			FIELD_PREP(GENMASK(30, 16), (x))
#define LAYERXOFFSET(x)			FIELD_PREP(GENMASK(14, 0), (x))

/* CLIPWINDOWOFFSET */
#define CLIPWINDOWYOFFSET(x)		FIELD_PREP(GENMASK(30, 16), (x))
#define CLIPWINDOWXOFFSET(x)		FIELD_PREP(GENMASK(14, 0), (x))

/* CLIPWINDOWDIMENSIONS */
#define CLIPWINDOWHEIGHT(x)		FIELD_PREP(GENMASK(29, 16), (x) - 1)
#define CLIPWINDOWWIDTH(x)		FIELD_PREP(GENMASK(13, 0), (x) - 1)

static const struct dc_lib_fu_pixel_format dc_fu_imx8qxp_formats[] = {
	{
		DRM_FORMAT_XRGB8888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(0),
		R_SHIFT(16) | G_SHIFT(8)  | B_SHIFT(0)  | A_SHIFT(0),
	},
};

const struct dc_lib_fu_match_data dc_fu_imx8qxp_data = {
	.pixel_formats = dc_fu_imx8qxp_formats,
	.pixel_formats_cnt = ARRAY_SIZE(dc_fu_imx8qxp_formats),
};

static const struct dc_lib_fu_pixel_format dc_fu_imx95_formats[] = {
	{
		DRM_FORMAT_ARGB8888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(8),
		R_SHIFT(16) | G_SHIFT(8)  | B_SHIFT(0)  | A_SHIFT(24),
	}, {
		DRM_FORMAT_XRGB8888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(0),
		R_SHIFT(16) | G_SHIFT(8)  | B_SHIFT(0)  | A_SHIFT(0),
	}, {
		DRM_FORMAT_ABGR8888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(8),
		R_SHIFT(0)  | G_SHIFT(8)  | B_SHIFT(16) | A_SHIFT(24),
	}, {
		DRM_FORMAT_XBGR8888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(0),
		R_SHIFT(0)  | G_SHIFT(8)  | B_SHIFT(16) | A_SHIFT(0),
	}, {
		DRM_FORMAT_RGBA8888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(8),
		R_SHIFT(24) | G_SHIFT(16) | B_SHIFT(8)  | A_SHIFT(0),
	}, {
		DRM_FORMAT_RGBX8888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(0),
		R_SHIFT(24) | G_SHIFT(16) | B_SHIFT(8)  | A_SHIFT(0),
	}, {
		DRM_FORMAT_BGRA8888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(8),
		R_SHIFT(8)  | G_SHIFT(16) | B_SHIFT(24) | A_SHIFT(0),
	}, {
		DRM_FORMAT_BGRX8888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(0),
		R_SHIFT(8)  | G_SHIFT(16) | B_SHIFT(24) | A_SHIFT(0),
	}, {
		DRM_FORMAT_RGB888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(0),
		R_SHIFT(16) | G_SHIFT(8)  | B_SHIFT(0)  | A_SHIFT(0),
	}, {
		DRM_FORMAT_BGR888,
		R_BITS(8)   | G_BITS(8)   | B_BITS(8)   | A_BITS(0),
		R_SHIFT(0)  | G_SHIFT(8)  | B_SHIFT(16) | A_SHIFT(0),
	}, {
		DRM_FORMAT_RGB565,
		R_BITS(5)   | G_BITS(6)   | B_BITS(5)   | A_BITS(0),
		R_SHIFT(11) | G_SHIFT(5)  | B_SHIFT(0)  | A_SHIFT(0),
	}, {
		DRM_FORMAT_YUYV,
		Y_BITS(8)   | U_BITS(8)   | V_BITS(8)   | A_BITS(0),
		Y_SHIFT(0)  | U_SHIFT(8)  | V_SHIFT(8)  | A_SHIFT(0),
	}, {
		DRM_FORMAT_UYVY,
		Y_BITS(8)   | U_BITS(8)   | V_BITS(8)   | A_BITS(0),
		Y_SHIFT(8)  | U_SHIFT(0)  | V_SHIFT(0)  | A_SHIFT(0),
	}, {
		DRM_FORMAT_NV12,
		Y_BITS(8)   | U_BITS(8)   | V_BITS(8)   | A_BITS(0),
		Y_SHIFT(0)  | U_SHIFT(0)  | V_SHIFT(8)  | A_SHIFT(0),
	}, {
		DRM_FORMAT_NV21,
		Y_BITS(8)   | U_BITS(8)   | V_BITS(8)   | A_BITS(0),
		Y_SHIFT(0)  | U_SHIFT(8)  | V_SHIFT(0)  | A_SHIFT(0),
	},
};

const struct dc_lib_fu_match_data dc_fu_imx95_data = {
	.pixel_formats = dc_fu_imx95_formats,
	.pixel_formats_cnt = ARRAY_SIZE(dc_fu_imx95_formats),
};

enum dc_linemode {
	LINEMODE_DISPLAY = 0,
};

void dc_fu_get_pixel_format_bits(struct dc_fu *fu, u32 format, u32 *bits)
{
	int i;

	for (i = 0; i < fu->match_data->pixel_formats_cnt; i++) {
		if (fu->match_data->pixel_formats[i].pixel_format == format) {
			*bits = fu->match_data->pixel_formats[i].bits;
			return;
		}
	}
}

void
dc_fu_get_pixel_format_shifts(struct dc_fu *fu, u32 format, u32 *shifts)
{
	int i;

	for (i = 0; i < fu->match_data->pixel_formats_cnt; i++) {
		if (fu->match_data->pixel_formats[i].pixel_format == format) {
			*shifts = fu->match_data->pixel_formats[i].shifts;
			return;
		}
	}
}

void dc_fu_shdldreq_sticky(struct dc_fu *fu, u8 layer_mask)
{
	regmap_write_bits(fu->reg_cfg, STATICCONTROL, SHDLDREQSTICKY_MASK,
			  SHDLDREQSTICKY(layer_mask));
}

static unsigned int dc_fu_burst_size_fixup(dma_addr_t baddr)
{
	unsigned int burst_size;

	burst_size = 1 << __ffs(baddr);
	burst_size = round_up(burst_size, 8);
	burst_size = min(burst_size, 128U);

	return burst_size;
}

static unsigned int
dc_fu_stride_fixup(unsigned int stride, unsigned int burst_size)
{
	return round_up(stride, burst_size);
}

static void dc_fu_set_burstlength(struct dc_fu *fu, dma_addr_t baddr)
{
	unsigned int burst_size, burst_length;

	burst_size = dc_fu_burst_size_fixup(baddr);
	burst_length = burst_size / 8;

	regmap_write_bits(fu->reg_cfg, fu->reg_burstbuffermanagement,
			  SETBURSTLENGTH_MASK, SETBURSTLENGTH(burst_length));
}

static void dc_fu_set_baseaddress(struct dc_fu *fu, enum dc_fu_frac frac,
				  dma_addr_t baddr)
{
	regmap_write(fu->reg_cfg, fu->reg_baseaddr[frac], baddr);
}

void dc_fu_set_src_bpp(struct dc_fu *fu, enum dc_fu_frac frac, unsigned int bpp)
{
	regmap_write_bits(fu->reg_cfg, fu->reg_sourcebufferattributes[frac],
			  BITSPERPIXEL_MASK, BITSPERPIXEL(bpp));
}

static void dc_fu_set_src_stride(struct dc_fu *fu, enum dc_fu_frac frac,
				 unsigned int width, int bpp, dma_addr_t baddr)
{
	unsigned int burst_size = dc_fu_burst_size_fixup(baddr);
	unsigned int stride;

	stride = width * (bpp / 8);
	stride = dc_fu_stride_fixup(stride, burst_size);

	regmap_write_bits(fu->reg_cfg, fu->reg_sourcebufferattributes[frac],
			  STRIDE_MASK, STRIDE(stride));
}

static void dc_fu_set_src_buf_dimensions(struct dc_fu *fu, enum dc_fu_frac frac,
					 int w, int h)
{
	regmap_write(fu->reg_cfg, fu->reg_sourcebufferdimension[frac],
		     LINEWIDTH(w) | LINECOUNT(h));
}

static inline void dc_fu_layeroffset(struct dc_fu *fu, enum dc_fu_frac frac,
				     unsigned int x, unsigned int y)
{
	regmap_write(fu->reg_cfg, fu->reg_layeroffset[frac],
		     LAYERXOFFSET(x) | LAYERYOFFSET(y));
}

static inline void dc_fu_clipoffset(struct dc_fu *fu, enum dc_fu_frac frac,
				    unsigned int x, unsigned int y)
{
	regmap_write(fu->reg_cfg, fu->reg_clipwindowoffset[frac],
		     CLIPWINDOWXOFFSET(x) | CLIPWINDOWYOFFSET(y));
}

static inline void dc_fu_clipdimensions(struct dc_fu *fu, enum dc_fu_frac frac,
					unsigned int w, unsigned int h)
{
	regmap_write(fu->reg_cfg, fu->reg_clipwindowdimensions[frac],
		     CLIPWINDOWWIDTH(w) | CLIPWINDOWHEIGHT(h));
}

static inline void
dc_fu_set_pixel_blend_mode(struct dc_fu *fu, enum dc_fu_frac frac)
{
	regmap_write(fu->reg_cfg, fu->reg_layerproperty[frac], 0);
	regmap_write(fu->reg_cfg, fu->reg_constantcolor[frac], 0);
}

static void dc_fu_enable_src_buf(struct dc_fu *fu, enum dc_fu_frac frac)
{
	regmap_write_bits(fu->reg_cfg, fu->reg_layerproperty[frac],
			  SOURCEBUFFERENABLE, SOURCEBUFFERENABLE);
}

static void dc_fu_disable_src_buf(struct dc_fu *fu, enum dc_fu_frac frac)
{
	regmap_write_bits(fu->reg_cfg, fu->reg_layerproperty[frac],
			  SOURCEBUFFERENABLE, 0);

	if (fu->lb) {
		dc_lb_pec_clken(fu->lb, CLKEN_DISABLE);
		dc_lb_mode(fu->lb, LB_NEUTRAL);
	}
}

static void dc_fu_set_layerblend(struct dc_fu *fu, struct dc_lb *lb)
{
	fu->lb = lb;
}

static enum dc_link_id dc_fu_get_link_id(struct dc_fu *fu)
{
	return fu->link_id;
}

static const char *dc_fu_get_name(struct dc_fu *fu)
{
	return fu->name;
}

const struct dc_fu_ops dc_fu_common_ops = {
	.set_burstlength	= dc_fu_set_burstlength,
	.set_baseaddress	= dc_fu_set_baseaddress,
	.set_src_stride		= dc_fu_set_src_stride,
	.set_src_buf_dimensions = dc_fu_set_src_buf_dimensions,
	.enable_src_buf		= dc_fu_enable_src_buf,
	.disable_src_buf	= dc_fu_disable_src_buf,
	.set_layerblend		= dc_fu_set_layerblend,
	.get_link_id		= dc_fu_get_link_id,
	.get_name		= dc_fu_get_name,
};

const struct dc_fu_ops *dc_fu_get_ops(struct dc_fu *fu)
{
	return &fu->ops;
}
EXPORT_SYMBOL_GPL(dc_fu_get_ops);

void dc_fu_common_hw_init(struct dc_fu *fu)
{
	/*
	 * STATICCONTROL and BURSTBUFFERMANAGEMENT must be read from hardware
	 * to preserve hardware-default bits.  REGCACHE_FLAT always returns
	 * the cached value without consulting the volatile table, so
	 * regmap_write_bits on uncached registers reads 0 and clears
	 * default bits.  Use direct readl/writel for these two registers
	 * during init, then sync the cache via regmap_write.
	 */
	if (fu->base_cfg) {
		u32 val;

		val = readl(fu->base_cfg + STATICCONTROL);
		val &= ~(BASEADDRESSAUTOUPDATE_MASK | SHDLDREQSTICKY_MASK);
		val |= SHDEN;
		writel(val, fu->base_cfg + STATICCONTROL);
		regmap_write(fu->reg_cfg, STATICCONTROL, val);

		val = readl(fu->base_cfg + fu->reg_burstbuffermanagement);
		val &= ~(LINEMODE_MASK |
			 COMBINERLINEFLUSH_ENABLE |
			 COMBINERTIMEOUT_ENABLE |
			 SETMAXBURSTLENGTH_MASK |
			 SETBURSTLENGTH_MASK |
			 SETNUMBUFFERS_MASK);
		val |= SETNUMBUFFERS(16) |
		       SETBURSTLENGTH(16) |
		       SETMAXBURSTLENGTH(64);
		writel(val, fu->base_cfg + fu->reg_burstbuffermanagement);
		regmap_write(fu->reg_cfg, fu->reg_burstbuffermanagement, val);
	} else {
		regmap_write_bits(fu->reg_cfg, STATICCONTROL, SHDEN, SHDEN);
		regmap_write_bits(fu->reg_cfg, fu->reg_burstbuffermanagement,
				  SETNUMBUFFERS_MASK, SETNUMBUFFERS(16));
		regmap_write_bits(fu->reg_cfg, fu->reg_burstbuffermanagement,
				  SETBURSTLENGTH_MASK, SETBURSTLENGTH(16));
		regmap_write_bits(fu->reg_cfg, fu->reg_burstbuffermanagement,
				  SETMAXBURSTLENGTH_MASK,
				  SETMAXBURSTLENGTH(64));
		regmap_write_bits(fu->reg_cfg, fu->reg_burstbuffermanagement,
				  COMBINERTIMEOUT_ENABLE, 0);
		regmap_write_bits(fu->reg_cfg, fu->reg_burstbuffermanagement,
				  COMBINERLINEFLUSH_ENABLE, 0);
	}

	/*
	 * Only initialize the first fraction (FRAC0) during HW init.
	 * Each fetch unit on i.MX95 maps to a single plane, so additional
	 * fractions may not have valid register backing and accessing them
	 * can trigger bus errors.  Per-fraction registers for additional
	 * fractions are initialized at plane setup time.
	 */
	dc_fu_layeroffset(fu, DC_FETCHUNIT_FRAC0, 0, 0);
	dc_fu_clipoffset(fu, DC_FETCHUNIT_FRAC0, 0, 0);
	dc_fu_clipdimensions(fu, DC_FETCHUNIT_FRAC0, 1, 1);
	regmap_write(fu->reg_cfg, fu->reg_sourcebufferattributes[DC_FETCHUNIT_FRAC0], 0);
	regmap_write(fu->reg_cfg, fu->reg_layerproperty[DC_FETCHUNIT_FRAC0], 0);
	regmap_write(fu->reg_cfg, fu->reg_constantcolor[DC_FETCHUNIT_FRAC0], 0);
}

int dc_fu_get_dprc(struct dc_fu *fu)
{
	struct dc_prg *prg;

	prg = dc_prg_lookup_by_phandle(fu->dev, "fsl,prg", 0);
	if (!prg)
		return dev_err_probe(fu->dev, -EPROBE_DEFER,
				     "failed to lookup PRG\n");

	fu->dprc = dc_prg_get_dprc(prg);
	if (!fu->dprc)
		return dev_err_probe(fu->dev, -EPROBE_DEFER,
				     "failed to get DPR channel\n");

	return 0;
}
