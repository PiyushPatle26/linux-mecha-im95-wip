/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef __DC_DRV_H__
#define __DC_DRV_H__

#include <linux/clk.h>
#include <linux/container_of.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <drm/drm_device.h>
#include <drm/drm_encoder.h>

#include "dc-de.h"
#include "dc-kms.h"
#include "dc-pe.h"

/**
 * enum dc_soc_type - SoC variant
 */
enum dc_soc_type {
	DC_SOC_IMX8QXP,
	DC_SOC_IMX95,
};

/**
 * struct dc_imx95 - i.MX95-specific sub-block pointers
 *
 * These are ioremap'd by the individual component drivers and stored here
 * so the KMS layer (dc-crtc, dc-plane) can program them directly.
 * Opaque void pointers are cast to the real type via dc-imx95-db.h etc.
 */
struct dc_imx95 {
	void *db[2];
	void *di[2];
	void __iomem *dm_regs;
};

/**
 * struct dc_drm_device - DC specific drm_device
 */
struct dc_drm_device {
	/** @base: base drm_device structure */
	struct drm_device base;
	/** @dc_crtc: DC specific CRTC list */
	struct dc_crtc dc_crtc[DC_DISPLAYS];
	/** @dc_primary: DC specific primary plane list */
	struct dc_plane dc_primary[DC_DISPLAYS];
	/** @encoder: encoder list */
	struct drm_encoder encoder[DC_DISPLAYS];
	/** @cf_safe: constframe list(safety stream) */
	struct dc_cf *cf_safe[DC_DISPLAYS];
	/** @cf_cont: constframe list(content stream) */
	struct dc_cf *cf_cont[DC_DISPLAYS];
	/** @de: display engine list */
	struct dc_de *de[DC_DISPLAYS];
	/** @ed_safe: extdst list(safety stream) */
	struct dc_ed *ed_safe[DC_DISPLAYS];
	/** @ed_cont: extdst list(content stream) */
	struct dc_ed *ed_cont[DC_DISPLAYS];
	/** @fg: framegen list */
	struct dc_fg *fg[DC_DISPLAYS];
	/** @fu_disp: fetchunit list(used by display engine) */
	struct dc_fu *fu_disp[DC_DISP_FU_CNT];
	/** @lb: layerblend list */
	struct dc_lb *lb[DC_LB_CNT];
	/** @pe: pixel engine */
	struct dc_pe *pe;
	/** @tc: tcon list */
	struct dc_tc *tc[DC_DISPLAYS];
	/** @pe_clk_axi_disabled: disablement flag at boot */
	bool pe_clk_axi_disabled;
	/** @clk_pix: pixel clock (i.MX95 only) */
	struct clk *clk_pix;
	/** @clk_ldb: LDB clock (i.MX95 only) */
	struct clk *clk_ldb;
	/** @clk_ldb_vco: LDB VCO clock (i.MX95 only) */
	struct clk *clk_ldb_vco;
	/** @dispmix_csr: display-mix blk-ctrl syscon regmap (i.MX95 only) */
	struct regmap *dispmix_csr;
	/** @soc: SoC variant */
	enum dc_soc_type soc;
	/** @imx95: i.MX95-specific sub-block pointers (NULL on 8QXP) */
	struct dc_imx95 *imx95;
};

struct dc_subdev_info {
	resource_size_t reg_start;
	int id;
};

static inline struct dc_drm_device *to_dc_drm_device(struct drm_device *drm)
{
	return container_of(drm, struct dc_drm_device, base);
}

int dc_crtc_init(struct dc_drm_device *dc_drm, int crtc_index);
int dc_crtc_post_init(struct dc_drm_device *dc_drm, int crtc_index);

int dc_kms_init(struct dc_drm_device *dc_drm);
void dc_kms_uninit(struct dc_drm_device *dc_drm);

int dc_plane_init(struct dc_drm_device *dc_drm, struct dc_plane *dc_plane,
		  unsigned int possible_crtcs);

static inline int dc_subdev_get_id(const struct dc_subdev_info *info,
				   int info_cnt, struct resource *res)
{
	int i;

	if (!res)
		return -EINVAL;

	for (i = 0; i < info_cnt; i++)
		if (info[i].reg_start == res->start)
			return info[i].id;

	return -EINVAL;
}

void dc_crtc_disable_at_boot(struct drm_crtc *crtc);
void dc_de_post_bind(struct dc_drm_device *dc_drm);
void dc_pe_post_bind(struct dc_drm_device *dc_drm);

extern struct platform_driver dc_cf_driver;
extern struct platform_driver dc_de_driver;
extern struct platform_driver dc_dprc_driver;
extern struct platform_driver dc_ed_driver;
extern struct platform_driver dc_fg_driver;
extern struct platform_driver dc_fl_driver;
extern struct platform_driver dc_fw_driver;
extern struct platform_driver dc_ic_driver;
extern struct platform_driver dc_lb_driver;
extern struct platform_driver dc_pe_driver;
extern struct platform_driver dc_prg_driver;
extern struct platform_driver dc_tc_driver;

/* i.MX95-only platform drivers */
extern struct platform_driver dc_imx95_db_driver;
extern struct platform_driver dc_imx95_dither_driver;

#endif /* __DC_DRV_H__ */
