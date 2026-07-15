/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef __DC_LIB_H__
#define __DC_LIB_H__

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/regmap.h>
#include <linux/types.h>

struct dc_subdev_info;

/* Hardware constants shared across all Display Controller blocks. */
#define SHDEN			BIT(0)

#define CLKEN_MASK_SHIFT	24
#define CLKEN_MASK		(0x3 << CLKEN_MASK_SHIFT)
#define CLKEN(n)		((n) << CLKEN_MASK_SHIFT)

/*
 * i.MX95-specific link ID values (differ from i.MX8QXP for some blocks).
 * These are Display Controller hardware interconnect IDs.
 */
#define IMX95_LINK_ID_NONE		0x00
#define IMX95_LINK_ID_CONSTFRAME0	0x0c
#define IMX95_LINK_ID_CONSTFRAME4	0x0d
#define IMX95_LINK_ID_CONSTFRAME1	0x10
#define IMX95_LINK_ID_CONSTFRAME5	0x11
#define IMX95_LINK_ID_LAYERBLEND1	0x14
#define IMX95_LINK_ID_LAYERBLEND2	0x15
#define IMX95_LINK_ID_LAYERBLEND3	0x16
#define IMX95_LINK_ID_LAYERBLEND4	0x17
#define IMX95_LINK_ID_LAYERBLEND5	0x18
#define IMX95_LINK_ID_LAYERBLEND6	0x19
#define IMX95_LINK_ID_FETCHLAYER0	0x1a
#define IMX95_LINK_ID_FETCHLAYER1	0x1b
#define IMX95_LINK_ID_FETCHYUV0		0x1d
#define IMX95_LINK_ID_FETCHYUV1		0x1f
#define IMX95_LINK_ID_FETCHYUV2		0x21
#define IMX95_LINK_ID_FETCHYUV3		0x1c
#define IMX95_LINK_ID_FETCHECO0		0x1e
#define IMX95_LINK_ID_FETCHECO1		0x20
#define IMX95_LINK_ID_FETCHECO2		0x22
#define IMX95_LINK_ID_STORE9		0x01
#define IMX95_LINK_ID_EXTDST0		0x02
#define IMX95_LINK_ID_EXTDST4		0x03
#define IMX95_LINK_ID_EXTDST1		0x04
#define IMX95_LINK_ID_EXTDST5		0x05
#define IMX95_LINK_ID_FETCHECO9		0x07
#define IMX95_LINK_ID_HSCALER9		0x08
#define IMX95_LINK_ID_VSCALER9		0x09
#define IMX95_LINK_ID_FILTER9		0x0a
#define IMX95_LINK_ID_HSCALER4		0x24
#define IMX95_LINK_ID_VSCALER4		0x25
#define IMX95_LINK_ID_MATRIX4		0x23

/* ---- Domain Mask (DM) registers at DC base + 0x2000 ---- */
#define DM_EXTDST0_DM		0x0c
#define DM_EXTDST4_DM		0x10
#define DM_EXTDST1_DM		0x14
#define DM_EXTDST5_DM		0x18
#define DM_ALLOW_ALL		0xffffffff
#define DM_DISPLAY_STATIC	0x1c
#define DM_PIPELINE_SYNC	BIT(0)
#define DM_EXTDST0_STATIC	0x20
#define DM_EXTDST4_STATIC	0x24
#define DM_EXTDST1_STATIC	0x28
#define DM_EXTDST5_STATIC	0x2c
#define DM_MASTER		BIT(15)

/* ---- ConstFrame register offsets (identical across SoCs) ---- */
#define CF_STATICCONTROL	0x8
#define CF_FRAMEDIMENSIONS	0xc
#define CF_CONSTANTCOLOR	0x10

/* ---- ExtDst register offsets (identical across SoCs) ---- */
#define ED_PIXENGCFG_STATIC	0x8
#define ED_PIXENGCFG_DYNAMIC	0xc
#define ED_PIXENGCFG_TRIGGER	0x14
#define ED_PIXENGCFG_STATUS	0x18
#define ED_STATICCONTROL	0x8
#define ED_CONTROL		0xc

/* ---- LayerBlend register offsets (identical across SoCs) ---- */
#define LB_PIXENGCFG_DYNAMIC	0x8
#define LB_STATICCONTROL	0x8
#define LB_CONTROL		0xc
#define LB_BLENDCONTROL		0x10
#define LB_POSITION		0x14

/* ---- FrameGen timing register offsets (identical across SoCs) ---- */
#define FG_FGSTCTRL		0x8
#define FG_HTCFG1		0xc
#define FG_HTCFG2		0x10
#define FG_VTCFG1		0x14
#define FG_VTCFG2		0x18

/* CLKEN_DISABLE / CLKEN_AUTOMATIC defined in dc-pe.h */

/*
 * struct dc_lib_fg_offsets - per-SoC register offsets for FrameGen regs
 *
 * Timing registers (FGSTCTRL through VTCFG2) are at base+0x0 through
 * base+0x10.  timing_base is 0x8 on i.MX8QXP, 0x0 on i.MX95.
 * The remaining registers vary between i.MX8QXP and i.MX95.
 */
struct dc_lib_fg_offsets {
	u32 timing_base;
	u32 pkickconfig;
	u32 skickconfig;
	u32 pacfg;
	u32 sacfg;
	u32 fginctrl;
	u32 fginctrlpanic;
	u32 fgccr;
	u32 fgenable;
	u32 fgslr;
	u32 fgtimestamp;
	u32 fgchstat;
	u32 fgchstatclr;
	u32 pfifotres;
};

/*
 * struct dc_lib_cf_match_data - per-SoC ConstFrame parameters
 *
 * ConstFrame registers are at the same offsets across SoCs,
 * so the match data is minimal (future-proofing).
 */
struct dc_lib_cf_match_data {
	const struct dc_subdev_info *subdev_info;
	int subdev_info_cnt;
	const u32 *link_ids;
	int link_ids_cnt;
};

/*
 * struct dc_lib_ed_match_data - per-SoC ExtDst parameters
 */
struct dc_lib_ed_match_data {
	const struct dc_subdev_info *subdev_info;
	int subdev_info_cnt;
	const u32 *src_sels;
	int src_sels_cnt;
};

/*
 * struct dc_lib_lb_match_data - per-SoC LayerBlend parameters
 */
struct dc_lib_lb_match_data {
	const struct dc_subdev_info *subdev_info;
	int subdev_info_cnt;
	const u32 *link_ids;
	const u32 *prim_sels;
	int prim_sels_cnt;
	const u32 *sec_sels;
	int sec_sels_cnt;
	int fixed_prim_sels_num;
};

/*
 * struct dc_lib_fg_match_data - per-SoC FrameGen parameters
 */
struct dc_lib_fg_match_data {
	const struct dc_subdev_info *subdev_info;
	int subdev_info_cnt;
	const struct dc_lib_fg_offsets *offsets;
	const struct regmap_config *regmap_cfg;
};

#endif /* __DC_LIB_H__ */
