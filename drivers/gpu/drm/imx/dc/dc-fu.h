/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef __DC_FETCHUNIT_H__
#define __DC_FETCHUNIT_H__

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <drm/drm_fourcc.h>

#include "dc-dprc.h"
#include "dc-lib.h"
#include "dc-pe.h"

#define FRAC_OFFSET			0x28

#define STATICCONTROL			0x8

/*
 * Per-fraction register base offsets (without REG_OFFSET1).
 * i.MX95 combines these with fu->reg_offset1 and the downstream
 * fraction stride (0x38) at bind time.  i.MX8QXP uses separate
 * legacy macros with FRAC_OFFSET (0x28) stride.
 */
#define _BASEADDRESS			0x00
#define _SOURCEBUFFERATTRIBUTES		0x10
#define _SOURCEBUFFERDIMENSION		0x14
#define _COLORCOMPONENTBITS		0x18
#define _COLORCOMPONENTSHIFT		0x1c
#define _LAYEROFFSET			0x20
#define _CLIPWINDOWOFFSET		0x24
#define _CLIPWINDOWDIMENSIONS		0x28
#define _CONSTANTCOLOR			0x2c
#define _LAYERPROPERTY			0x30

/* COLORCOMPONENTBITS */
#define R_BITS(x)			FIELD_PREP_CONST(GENMASK(27, 24), (x))
#define G_BITS(x)			FIELD_PREP_CONST(GENMASK(19, 16), (x))
#define B_BITS(x)			FIELD_PREP_CONST(GENMASK(11, 8), (x))
#define A_BITS(x)			FIELD_PREP_CONST(GENMASK(3, 0), (x))
#define Y_BITS(x)			R_BITS(x)
#define U_BITS(x)			G_BITS(x)
#define V_BITS(x)			B_BITS(x)

/* COLORCOMPONENTSHIFT */
#define R_SHIFT(x)			FIELD_PREP_CONST(GENMASK(28, 24), (x))
#define G_SHIFT(x)			FIELD_PREP_CONST(GENMASK(20, 16), (x))
#define B_SHIFT(x)			FIELD_PREP_CONST(GENMASK(12, 8), (x))
#define A_SHIFT(x)			FIELD_PREP_CONST(GENMASK(4, 0), (x))
#define Y_SHIFT(x)			R_SHIFT(x)
#define U_SHIFT(x)			G_SHIFT(x)
#define V_SHIFT(x)			B_SHIFT(x)

/* LAYERPROPERTY */
#define SOURCEBUFFERENABLE		BIT(31)
#define YUVCONVERSIONMODE_MASK		GENMASK(18, 17)
#define YUVCONVERSIONMODE(x)		FIELD_PREP(YUVCONVERSIONMODE_MASK, (x))

/* FRAMEDIMENSIONS */
#define FRAMEHEIGHT(x)			FIELD_PREP(GENMASK(29, 16), (x) - 1)
#define FRAMEWIDTH(x)			FIELD_PREP(GENMASK(13, 0), (x) - 1)

/* CONTROL */
#define INPUTSELECT_MASK		GENMASK(4, 3)
#define INPUTSELECT(x)			FIELD_PREP(INPUTSELECT_MASK, (x))
#define RASTERMODE_MASK			GENMASK(2, 0)
#define RASTERMODE(x)			FIELD_PREP(RASTERMODE_MASK, (x))

/*
 * struct dc_lib_fu_pixel_format - pixel format descriptor for FetchUnit
 */
struct dc_lib_fu_pixel_format {
	u32 pixel_format;
	u32 bits;
	u32 shifts;
};

/*
 * struct dc_lib_fu_match_data - per-SoC FetchUnit parameters
 */
struct dc_lib_fu_match_data {
	const struct dc_lib_fu_pixel_format *pixel_formats;
	int pixel_formats_cnt;
};

enum dc_yuvconversionmode {
	YUVCONVERSIONMODE_OFF,
};

enum dc_inputselect {
	INPUTSELECT_INACTIVE,
};

enum dc_rastermode {
	RASTERMODE_NORMAL,
};

enum {
	DC_FETCHUNIT_FL0,
	DC_FETCHUNIT_FW2,
};

enum dc_fu_frac {
	DC_FETCHUNIT_FRAC0,
	DC_FETCHUNIT_FRAC1,
	DC_FETCHUNIT_FRAC2,
	DC_FETCHUNIT_FRAC3,
	DC_FETCHUNIT_FRAC4,
	DC_FETCHUNIT_FRAC5,
	DC_FETCHUNIT_FRAC6,
	DC_FETCHUNIT_FRAC7,
	DC_FETCHUNIT_FRAC_NUM
};

struct dc_fu;
struct dc_lb;

struct dc_fu_ops {
	void (*init)(struct dc_fu *fu);
	void (*set_burstlength)(struct dc_fu *fu, dma_addr_t baddr);
	void (*set_baseaddress)(struct dc_fu *fu, enum dc_fu_frac frac,
				dma_addr_t baddr);
	void (*set_src_stride)(struct dc_fu *fu, enum dc_fu_frac frac,
			       unsigned int width, int bpp, dma_addr_t baddr);
	void (*set_src_buf_dimensions)(struct dc_fu *fu, enum dc_fu_frac frac,
				       int w, int h);
	void (*set_fmt)(struct dc_fu *fu, enum dc_fu_frac frac,
			const struct drm_format_info *format);
	void (*enable_src_buf)(struct dc_fu *fu, enum dc_fu_frac frac);
	void (*disable_src_buf)(struct dc_fu *fu, enum dc_fu_frac frac);
	void (*set_framedimensions)(struct dc_fu *fu, int w, int h);
	void (*set_layerblend)(struct dc_fu *fu, struct dc_lb *lb);
	enum dc_link_id (*get_link_id)(struct dc_fu *fu);
	const char *(*get_name)(struct dc_fu *fu);
};

struct dc_fu {
	struct device *dev;
	void __iomem *base_cfg;
	struct regmap *reg_pec;
	struct regmap *reg_cfg;
	const struct dc_lib_fu_match_data *match_data;
	char name[21];
	u32 reg_baseaddr[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_sourcebufferattributes[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_sourcebufferdimension[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_layeroffset[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_clipwindowoffset[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_clipwindowdimensions[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_constantcolor[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_layerproperty[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_colorcomponentbits[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_colorcomponentshift[DC_FETCHUNIT_FRAC_NUM];
	u32 reg_framedimensions;
	u32 reg_burstbuffermanagement;
	unsigned int reg_offset1;
	unsigned int reg_offset2;
	unsigned int id;
	enum dc_link_id link_id;
	struct dc_fu_ops ops;
	struct dc_lb *lb;
	struct dc_dprc *dprc;
};

extern const struct dc_fu_ops dc_fu_common_ops;
extern const struct dc_lib_fu_match_data dc_fu_imx8qxp_data;
extern const struct dc_lib_fu_match_data dc_fu_imx95_data;

void dc_fu_get_pixel_format_bits(struct dc_fu *fu, u32 format, u32 *bits);
void dc_fu_get_pixel_format_shifts(struct dc_fu *fu, u32 format, u32 *shifts);
void dc_fu_shdldreq_sticky(struct dc_fu *fu, u8 layer_mask);
void dc_fu_set_src_bpp(struct dc_fu *fu, enum dc_fu_frac frac, unsigned int bpp);
void dc_fu_common_hw_init(struct dc_fu *fu);
int dc_fu_get_dprc(struct dc_fu *fu);

const struct dc_fu_ops *dc_fu_get_ops(struct dc_fu *fu);

#endif /* __DC_FETCHUNIT_H__ */
