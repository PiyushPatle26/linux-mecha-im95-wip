/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef __DC_PIXEL_ENGINE_H__
#define __DC_PIXEL_ENGINE_H__

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/regmap.h>

#include "dc-de.h"
#include "dc-lib.h"

#define DC_DISP_FU_CNT		2
/*
 * LayerBlend array is indexed by hardware id.  i.MX8QXP uses ids 0..3,
 * i.MX95 uses ids 1..6, so the array must hold id 6 (LB6), i.e. sized 7.
 * Undersizing this silently drops LB6, which is the
 * terminal LayerBlend of i.MX95 stream 1 and must feed ExtDst1.
 */
#define DC_LB_CNT		7	/* max LB id (6 on i.MX95) + 1 */

enum dc_link_id {
	LINK_ID_NONE		= 0x00,
	LINK_ID_CONSTFRAME0	= 0x0c,
	LINK_ID_CONSTFRAME4	= 0x0e,
	LINK_ID_CONSTFRAME1	= 0x10,
	LINK_ID_CONSTFRAME5	= 0x12,
	LINK_ID_FETCHWARP2	= 0x14,
	LINK_ID_FETCHLAYER0	= 0x1a,
	LINK_ID_LAYERBLEND0	= 0x21,
	LINK_ID_LAYERBLEND1	= 0x22,
	LINK_ID_LAYERBLEND2	= 0x23,
	LINK_ID_LAYERBLEND3	= 0x24,
};

enum dc_lb_mode {
	LB_NEUTRAL,	/* Output is same as primary input. */
	LB_BLEND,
};

enum dc_pec_clken {
	CLKEN_DISABLE,
	CLKEN_AUTOMATIC,
};

struct dc_cf {
	struct regmap *reg_cfg;
	enum dc_link_id link;
};

struct dc_ed {
	struct device *dev;
	struct regmap *reg_pec;
	struct regmap *reg_cfg;
	const struct dc_lib_ed_match_data *match_data;
	int irq_shdload;
};

struct dc_lb {
	struct device *dev;
	struct regmap *reg_pec;
	struct regmap *reg_cfg;
	const struct dc_lib_lb_match_data *match_data;
	int id;
	enum dc_link_id link;
};

struct dc_pe {
	struct device *dev;
	struct clk *clk_axi;
	struct dc_cf *cf_safe[DC_DISPLAYS];
	struct dc_cf *cf_cont[DC_DISPLAYS];
	struct dc_ed *ed_safe[DC_DISPLAYS];
	struct dc_ed *ed_cont[DC_DISPLAYS];
	struct dc_fu *fu_disp[DC_DISP_FU_CNT];
	struct dc_lb *lb[DC_LB_CNT];
};

/* Constant Frame Unit */
enum dc_link_id dc_cf_get_link_id(struct dc_cf *cf);
void dc_cf_framedimensions(struct dc_cf *cf, unsigned int w, unsigned int h);
void dc_cf_constantcolor_black(struct dc_cf *cf);
void dc_cf_constantcolor_blue(struct dc_cf *cf);
void dc_cf_init(struct dc_cf *cf);

/* External Destination Unit */
void dc_ed_pec_clken(struct dc_ed *ed, u32 clken);
void dc_ed_pec_src_sel(struct dc_ed *ed, enum dc_link_id src);
int dc_ed_pec_src_sel_get(struct dc_ed *ed, enum dc_link_id *src);
void dc_ed_pec_sync_trigger(struct dc_ed *ed);
int dc_ed_pec_sync_wait(struct dc_ed *ed);
void dc_ed_init(struct dc_ed *ed);

/* Layer Blend Unit */
enum dc_link_id dc_lb_get_link_id(struct dc_lb *lb);
void dc_lb_pec_dynamic_prim_sel(struct dc_lb *lb, enum dc_link_id prim);
void dc_lb_pec_dynamic_sec_sel(struct dc_lb *lb, enum dc_link_id sec);
int dc_lb_pec_dynamic_sec_sel_get(struct dc_lb *lb, enum dc_link_id *sec);
void dc_lb_pec_clken(struct dc_lb *lb, enum dc_pec_clken clken);
void dc_lb_mode(struct dc_lb *lb, enum dc_lb_mode mode);
void dc_lb_position(struct dc_lb *lb, int x, int y);
int dc_lb_get_id(struct dc_lb *lb);
void dc_lb_init(struct dc_lb *lb);

#endif /* __DC_PIXEL_ENGINE_H__ */
