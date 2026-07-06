/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef __DC_KMS_H__
#define __DC_KMS_H__

#include <linux/completion.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_vblank.h>

#include "dc-de.h"
#include "dc-fu.h"
#include "dc-pe.h"

#define DC_CRTC_IRQS	6

struct dc_crtc_irq {
	struct dc_crtc *dc_crtc;
	unsigned int irq;
};

/**
 * struct dc_crtc - DC specific drm_crtc
 *
 * Each display controller contains one content stream and one safety stream.
 * In general, the two streams have the same functionality. One stream is
 * overlaid on the other by @fg. This driver chooses to generate black constant
 * color from the content stream as background color, build plane(s) on the
 * content stream by using layerblend(s) and always generate a constant color
 * from the safety stream. Note that due to the decoupled timing, the safety
 * stream still works to show the constant color properly even when the content
 * stream has completely hung up due to mal-function of this driver.
 */
struct dc_crtc {
	/** @base: base drm_crtc structure */
	struct drm_crtc base;
	/** @de: display engine */
	struct dc_de *de;
	/** @cf_cont: content stream constframe */
	struct dc_cf *cf_cont;
	/** @cf_safe: safety stream constframe */
	struct dc_cf *cf_safe;
	/** @ed_cont: content stream extdst */
	struct dc_ed *ed_cont;
	/** @ed_safe: safety stream extdst */
	struct dc_ed *ed_safe;
	/** @fg: framegen */
	struct dc_fg *fg;
	/** @tc: tcon */
	struct dc_tc *tc;
	/** @db: i.MX95 DomainBlend instance (NULL on 8QXP) */
	void *db;
	/** @di: i.MX95 Dither instance (NULL on 8QXP) */
	void *di;
	/**
	 * @output_is_dsi:
	 *
	 * i.MX95: the output pipeline drives a MIPI-DSI encoder rather than
	 * LVDS/LDB.  Selects the FrameGen pixel-clock source (DSI PLL vs LVDS
	 * PLL) in the enable/disable paths.
	 */
	bool output_is_dsi;
	/**
	 * @irq_dec_dev:
	 *
	 * device owning the display-engine-config interrupts (the display
	 * engine on i.MX8QXP, the Dither/DISENGCFG block on i.MX95)
	 */
	struct device *irq_dec_dev;
	/**
	 * @irq_dec_framecomplete:
	 *
	 * display engine configuration frame complete interrupt
	 */
	unsigned int irq_dec_framecomplete;
	/**
	 * @irq_dec_seqcomplete:
	 *
	 * display engine configuration sequence complete interrupt
	 */
	unsigned int irq_dec_seqcomplete;
	/**
	 * @irq_dec_shdload:
	 *
	 * display engine configuration shadow load interrupt
	 */
	unsigned int irq_dec_shdload;
	/**
	 * @irq_ed_cont_shdload:
	 *
	 * content stream extdst shadow load interrupt
	 */
	unsigned int irq_ed_cont_shdload;
	/**
	 * @irq_ed_safe_shdload:
	 *
	 * safety stream extdst shadow load interrupt
	 */
	unsigned int irq_ed_safe_shdload;
	/**
	 * @irq_db_shdload:
	 *
	 * i.MX95 DomainBlend shadow load interrupt (0 on i.MX8QXP)
	 */
	unsigned int irq_db_shdload;
	/** @db_dev: device owning the DomainBlend interrupt (i.MX95 only) */
	struct device *db_dev;
	/**
	 * @dec_seqcomplete_done:
	 *
	 * display engine configuration sequence completion
	 */
	struct completion dec_seqcomplete_done;
	/**
	 * @dec_shdload_done:
	 *
	 * display engine configuration shadow load completion
	 */
	struct completion dec_shdload_done;
	/**
	 * @ed_cont_shdload_done:
	 *
	 * content stream extdst shadow load completion
	 */
	struct completion ed_cont_shdload_done;
	/**
	 * @ed_safe_shdload_done:
	 *
	 * safety stream extdst shadow load completion
	 */
	struct completion ed_safe_shdload_done;
	/**
	 * @db_shdload_done:
	 *
	 * i.MX95 DomainBlend shadow load completion
	 */
	struct completion db_shdload_done;
	/** @event: cached pending vblank event */
	struct drm_pending_vblank_event *event;
	/** @vblank_hrtimer: software vblank timer when no framecomplete IRQ */
	struct hrtimer vblank_hrtimer;
	/** @vblank_period: hrtimer period derived from the active display mode */
	ktime_t vblank_period;
	/** @irqs: interrupt list */
	struct dc_crtc_irq irqs[DC_CRTC_IRQS];
};

/**
 * struct dc_plane - DC specific drm_plane
 *
 * Build a plane on content stream with a fetchunit and a layerblend.
 */
struct dc_plane {
	/** @base: base drm_plane structure */
	struct drm_plane base;
	/** @fu: fetchunit */
	struct dc_fu *fu;
	/** @cf: content stream constframe */
	struct dc_cf *cf;
	/** @lb: layerblend */
	struct dc_lb *lb;
	/** @ed: content stream extdst */
	struct dc_ed *ed;
};

static inline struct dc_plane *to_dc_plane(struct drm_plane *plane)
{
	return container_of(plane, struct dc_plane, base);
}

struct dc_drm_device;

extern const struct drm_mode_config_funcs dc_drm_mode_config_funcs;
int dc_kms_init_encoder_per_crtc(struct dc_drm_device *dc_drm, int crtc_index);

#endif /* __DC_KMS_H__ */
