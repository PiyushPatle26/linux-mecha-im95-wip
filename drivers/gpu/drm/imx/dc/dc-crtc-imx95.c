// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 *
 * i.MX95 CRTC.
 *
 * XXX: i.MX95 mode changes (re-tuning) are not supported yet.
 *
 * The first modeset after boot works at any resolution, but switching to a
 * different mode afterwards wedges the ExtDst content shadow load and the
 * FrameGen primary channel never syncs up, even though the content path is
 * programmed the same way as the working cold-boot case.  The difference is
 * that FrameGen is no longer in its power-on state.
 *
 * FrameGen has no software reset, and its timing counter free-runs off the
 * LVDS PLL, which the LDB bridge keeps enabled.  i.MX8QXP avoids this by
 * power-cycling the display and pixel-engine domains on every modeset, which
 * resets FrameGen through the runtime PM resume path.  i.MX95 can't do the
 * same: IMX95_PD_DISPLAY is a single domain pinned on by other consumers
 * (dispmix_csr, the interrupt steer/controller), and there's no reset
 * controller or SCMI reset for the display slice.
 *
 * Fixing this means making IMX95_PD_DISPLAY cycle-able again: drop the
 * driver's permanent PM reference, add a runtime_resume that reinitializes
 * the blocks, and stop the other consumers from pinning the domain.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/regmap.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>
#include <drm/drm_vblank.h>

#include "dc-crtc-common.h"
#include "dc-de.h"
#include "dc-drv.h"
#include "dc-fu.h"
#include "dc-kms.h"
#include "dc-pe.h"
#include "dc-imx95-db.h"
#include "dc-imx95-di.h"

/*
 * FrameGen pixel clock source select, in the display-mix blk-ctrl (dispmix_csr,
 * i.MX95 only).  Two bits per FrameGen: 0b01 selects the DSI PLL divider output
 * (MIPI-DSI output path), 0b10 the LVDS PLL /7 output (LVDS/LDB output path).
 */
#define DC_IMX95_CLOCK_CTRL			0x0
#define  DC_IMX95_PIX_CLK_SEL_MASK(id)		(0x3 << (2 * (id)))
#define  DC_IMX95_PIX_CLK_SEL_DSI_PLL(id)	(0x1 << (2 * (id)))
#define  DC_IMX95_PIX_CLK_SEL_LVDS_PLL(id)	(0x2 << (2 * (id)))

/*
 * i.MX95 CRTC mode set.
 *
 * Runs in drm_atomic_helper_commit_modeset_disables() -> crtc_set_mode(),
 * before the plane's ->atomic_update and ->atomic_enable.  It programs the
 * per-mode FrameGen timing and the default (black ConstFrame) content source;
 * the plane update then repoints the ExtDst to its LayerBlend.  The default
 * source must be set here and not in ->atomic_enable (which runs after the
 * plane update), or it would clobber the content path the plane just
 * programmed and leave the FrameGen FIFO empty (green panic colour).  Only
 * register configuration is done here; the pixel clock and FrameGen are
 * started in ->atomic_enable.
 */
static void dc_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_display_mode *adj = &crtc->state->adjusted_mode;
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	struct dc_drm_device *dc_drm = to_dc_drm_device(crtc->dev);

	dc_crtc_dbg(crtc, "i.MX95 mode " DRM_MODE_FMT "\n", DRM_MODE_ARG(adj));

	dc_imx95_dm_disable_pipeline_sync(dc_drm->imx95->dm_regs);

	/*
	 * The FrameGen pixel clock is not enabled here; it is started just before
	 * FGENABLE=1 in ->atomic_enable so the FrameGen starts on a clean frame
	 * rather than free-running through the mode set.  Only the DC register bus
	 * clock (up from probe) is needed for the register writes below.
	 *
	 * The content-path pixel-engine blocks (ConstFrame, ExtDst, LayerBlend,
	 * FetchUnit) and the DomainBlend routing are hardware-initialised once in
	 * dc_crtc_pe_hw_init(); the permanent runtime-PM reference keeps the
	 * display power domain on so that setup persists.  Only the FrameGen,
	 * ConstFrame contents and ExtDst source are reprogrammed per modeset,
	 * matching the dpu95 driver.
	 */
	dc_fg_init(dc_crtc->fg);
	dc_fg_cfg_videomode(dc_crtc->fg, adj);

	/*
	 * For the MIPI-DSI output the FrameGen pixel clock is the "pix" clock and
	 * its rate must be programmed for the mode.  The LVDS path instead derives
	 * its rate from the LDB clock tree, so nothing is set here.
	 */
	if (dc_crtc->output_is_dsi) {
		int ret = clk_set_rate(dc_drm->clk_pix, adj->clock * 1000);

		if (ret < 0)
			dc_crtc_err(crtc, "failed to set pix clock rate: %d\n",
				    ret);
	}

	if (dc_crtc->di) {
		dc_imx95_di_polen_active_high(dc_crtc->di);
		if (adj->flags & DRM_MODE_FLAG_PHSYNC)
			dc_imx95_di_polhs_active_high(dc_crtc->di);
		else
			dc_imx95_di_polhs_active_low(dc_crtc->di);
		if (adj->flags & DRM_MODE_FLAG_PVSYNC)
			dc_imx95_di_polvs_active_high(dc_crtc->di);
		else
			dc_imx95_di_polvs_active_low(dc_crtc->di);
	}

	dc_cf_framedimensions(dc_crtc->cf_cont,
			      adj->crtc_hdisplay, adj->crtc_vdisplay);
	dc_cf_constantcolor_black(dc_crtc->cf_cont);

	if (dc_crtc->ed_safe && dc_crtc->cf_safe) {
		dc_cf_framedimensions(dc_crtc->cf_safe,
				      adj->crtc_hdisplay, adj->crtc_vdisplay);
		dc_cf_constantcolor_blue(dc_crtc->cf_safe);
		dc_ed_pec_src_sel(dc_crtc->ed_safe,
				  dc_cf_get_link_id(dc_crtc->cf_safe));
	}

	/*
	 * Default content source is the black ConstFrame.  If a plane is active
	 * in this commit, its ->atomic_update (which runs after this) repoints
	 * ExtDst to its LayerBlend; if not, the black ConstFrame is shown.
	 */
	dc_ed_pec_src_sel(dc_crtc->ed_cont,
			  dc_cf_get_link_id(dc_crtc->cf_cont));
	dc_ed_pec_clken(dc_crtc->ed_cont, CLKEN_AUTOMATIC);
}

static void
dc_crtc_atomic_enable(struct drm_crtc *crtc, struct drm_atomic_commit *state)
{
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, crtc);
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	struct dc_drm_device *dc_drm = to_dc_drm_device(crtc->dev);
	int ret;

	drm_crtc_vblank_on(crtc);

	/*
	 * Select the FrameGen pixel-clock source and enable it here rather than
	 * in mode_set_nofb, so the FrameGen starts on a clean frame instead of
	 * free-running through the mode set.  The DSI PLL feeds the MIPI-DSI path,
	 * the LVDS PLL the LDB path.  Balanced in dc_crtc_atomic_disable().
	 */
	if (dc_drm->dispmix_csr)
		regmap_update_bits(dc_drm->dispmix_csr, DC_IMX95_CLOCK_CTRL,
				   DC_IMX95_PIX_CLK_SEL_MASK(dc_crtc->fg->id),
				   dc_crtc->output_is_dsi ?
				   DC_IMX95_PIX_CLK_SEL_DSI_PLL(dc_crtc->fg->id) :
				   DC_IMX95_PIX_CLK_SEL_LVDS_PLL(dc_crtc->fg->id));

	if (dc_crtc->output_is_dsi) {
		ret = clk_prepare_enable(dc_drm->clk_pix);
		if (ret)
			dc_crtc_err(crtc, "failed to enable pix clock: %d\n", ret);
	} else {
		ret = clk_prepare_enable(dc_drm->clk_ldb_vco);
		if (ret)
			dc_crtc_err(crtc, "failed to enable ldb_vco clock: %d\n",
				    ret);
		ret = clk_prepare_enable(dc_drm->clk_ldb);
		if (ret)
			dc_crtc_err(crtc, "failed to enable ldb clock: %d\n", ret);
	}

	if (dc_crtc->irq_db_shdload)
		enable_irq(dc_crtc->irq_db_shdload);

	dc_fg_enable_clock(dc_crtc->fg);

	/*
	 * The content path is already set up by dc_crtc_mode_set_nofb() and
	 * the plane update; here only start the output.  Arm the ExtDst
	 * external-kick content shadow load, latch the DomainBlend and FrameGen
	 * shadow tokens, then start the FrameGen, matching the dpu95 driver.
	 */
	dc_ed_pec_sync_trigger(dc_crtc->ed_cont);
	if (dc_crtc->ed_safe)
		dc_ed_pec_sync_trigger(dc_crtc->ed_safe);

	if (dc_crtc->db)
		dc_imx95_db_shdtokgen(dc_crtc->db);
	dc_fg_shdtokgen(dc_crtc->fg);

	dc_fg_primary_clear_channel_status(dc_crtc->fg);
	dc_fg_enable(dc_crtc->fg);

	if (dc_crtc->irq_dec_shdload) {
		DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(dec_shdload_done);
		disable_irq(dc_crtc->irq_dec_shdload);
	}
	if (dc_crtc->irq_db_shdload) {
		DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(db_shdload_done);
		disable_irq(dc_crtc->irq_db_shdload);
	}
	if (dc_crtc->irq_ed_safe_shdload) {
		DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(ed_safe_shdload_done);
		disable_irq(dc_crtc->irq_ed_safe_shdload);
	}
	if (dc_crtc->irq_ed_cont_shdload) {
		DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(ed_cont_shdload_done);
		disable_irq(dc_crtc->irq_ed_cont_shdload);
	}

	if (!dc_crtc->irq_ed_cont_shdload &&
	    !dc_crtc->irq_dec_shdload &&
	    dc_ed_pec_sync_wait(dc_crtc->ed_cont))
		dc_crtc_err(crtc, "ExtDst content shadow load timeout\n");

	if (dc_fg_wait_for_primary_syncup(dc_crtc->fg))
		dc_crtc_err(crtc, "FrameGen primary channel isn't syncup\n");
	dc_fg_primary_clear_channel_status(dc_crtc->fg);

	DC_CRTC_CHECK_FRAMEGEN_FIFO(dc_crtc->fg);

	dc_crtc_queue_state_event(new_crtc_state, false);
}

static void
dc_crtc_atomic_disable(struct drm_crtc *crtc, struct drm_atomic_commit *state)
{
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, crtc);
	struct dc_drm_device *dc_drm = to_dc_drm_device(crtc->dev);
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	int idx;

	if (!drm_dev_enter(crtc->dev, &idx))
		goto out;

	dc_fg_disable(dc_crtc->fg);
	dc_fg_shdtokgen(dc_crtc->fg);
	DC_CRTC_WAIT_FOR_FRAMEGEN_FRAME_INDEX_MOVING(dc_crtc->fg);

	dc_fg_disable_clock(dc_crtc->fg);

	if (dc_crtc->output_is_dsi) {
		clk_disable_unprepare(dc_drm->clk_pix);
	} else {
		clk_disable_unprepare(dc_drm->clk_ldb);
		clk_disable_unprepare(dc_drm->clk_ldb_vco);
	}
	if (dc_drm->dispmix_csr)
		regmap_update_bits(dc_drm->dispmix_csr, DC_IMX95_CLOCK_CTRL,
				   DC_IMX95_PIX_CLK_SEL_MASK(dc_crtc->fg->id), 0);

	drm_dev_exit(idx);

out:
	drm_crtc_vblank_off(crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (new_crtc_state->event && !new_crtc_state->active) {
		drm_crtc_send_vblank_event(crtc, new_crtc_state->event);
		new_crtc_state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

void dc_crtc_disable_at_boot(struct drm_crtc *crtc)
{
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);

	dc_crtc_dbg(crtc, "i.MX95: boot disable (simple)\n");
	if (dc_fg_wait_for_frame_index_moving(dc_crtc->fg)) {
		dc_fg_disable(dc_crtc->fg);
		dc_fg_shdtokgen(dc_crtc->fg);
	}
}

/*
 * One-time hardware init of the i.MX95 content-path pixel-engine blocks.
 * Runs once at CRTC init; the permanent runtime-PM reference keeps
 * IMX95_PD_DISPLAY powered so this static setup persists across modesets.
 * The per-modeset path (dc_crtc_mode_set_nofb) then only reprograms the
 * FrameGen, ConstFrame contents and ExtDst source selection - it must not
 * re-init these blocks, or the ExtDst PixEngCfg reset desyncs it from the
 * free-running FrameGen and the content shadow load never latches on a
 * re-tune.  Mirrors the monolithic dpu95 driver's hw-init model.
 */
static void dc_crtc_pe_hw_init(struct dc_crtc *dc_crtc)
{
	struct dc_drm_device *dc_drm = to_dc_drm_device(dc_crtc->base.dev);
	struct dc_plane *dc_primary = to_dc_plane(dc_crtc->base.primary);

	dc_imx95_dm_init(dc_drm->imx95->dm_regs);
	if (dc_crtc->db)
		dc_imx95_db_init(dc_crtc->db);
	dc_cf_init(dc_crtc->cf_cont);
	if (dc_crtc->cf_safe)
		dc_cf_init(dc_crtc->cf_safe);
	dc_ed_init(dc_crtc->ed_cont);
	if (dc_crtc->ed_safe)
		dc_ed_init(dc_crtc->ed_safe);
	if (dc_primary->lb)
		dc_lb_init(dc_primary->lb);
	if (dc_primary->fu) {
		const struct dc_fu_ops *fu_ops = dc_fu_get_ops(dc_primary->fu);

		if (fu_ops->init)
			fu_ops->init(dc_primary->fu);
	}
}

/* Software-vblank variant: no hardware scanout position. */
static const struct drm_crtc_funcs dc_crtc_swvblank_funcs = {
	.reset			= drm_atomic_helper_crtc_reset,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= dc_crtc_enable_vblank,
	.disable_vblank		= dc_crtc_disable_vblank,
};

static const struct drm_crtc_helper_funcs dc_helper_swvblank_funcs = {
	.mode_valid		= dc_crtc_mode_valid,
	.atomic_check		= dc_crtc_atomic_check,
	.mode_set_nofb		= dc_crtc_mode_set_nofb,
	.atomic_begin		= dc_crtc_atomic_begin,
	.atomic_flush		= dc_crtc_atomic_flush,
	.atomic_enable		= dc_crtc_atomic_enable,
	.atomic_disable		= dc_crtc_atomic_disable,
};

int dc_crtc_init(struct dc_drm_device *dc_drm, int crtc_index)
{
	struct dc_crtc *dc_crtc = &dc_drm->dc_crtc[crtc_index];
	struct drm_device *drm = &dc_drm->base;
	struct dc_plane *dc_primary;
	struct device *dev;
	int ret;

	init_completion(&dc_crtc->dec_seqcomplete_done);
	init_completion(&dc_crtc->dec_shdload_done);
	init_completion(&dc_crtc->ed_cont_shdload_done);
	init_completion(&dc_crtc->ed_safe_shdload_done);
	init_completion(&dc_crtc->db_shdload_done);

	dc_crtc->de = NULL;
	dc_crtc->cf_cont = dc_drm->cf_cont[crtc_index];
	dc_crtc->cf_safe = dc_drm->cf_safe[crtc_index];
	dc_crtc->ed_cont = dc_drm->ed_cont[crtc_index];
	dc_crtc->ed_safe = dc_drm->ed_safe[crtc_index];
	dc_crtc->fg = dc_drm->fg[crtc_index];
	dc_crtc->tc = NULL;
	dc_crtc->db = dc_drm->imx95->db[crtc_index];
	dc_crtc->di = dc_drm->imx95->di[crtc_index];

	/*
	 * Software vblank timer, used when the DISENGCFG frame-complete
	 * interrupt is not wired (via the DC interrupt controller).
	 */
	dc_crtc_setup_vblank_hrtimer(dc_crtc);

	/*
	 * The DISENGCFG frame-complete interrupt drives vblank when the
	 * DC interrupt controller routes it; otherwise irq_dec_framecomplete
	 * stays 0 and the software timer above is used.
	 */
	dc_crtc->irq_dec_framecomplete = dc_crtc->di ?
		dc_imx95_di_framecomplete_irq(dc_crtc->di) : 0;
	dc_crtc->irq_dec_dev = dc_crtc->di ?
		dc_imx95_di_dev(dc_crtc->di) : NULL;
	/*
	 * DISENGCFG sequence-complete doesn't fire on the i.MX95
	 * component datapath: there's no TCON sequencer between FrameGen
	 * and LDB, unlike the monolithic dpu95 driver.
	 * dc_crtc_atomic_disable() polls the frame index instead of
	 * waiting on this IRQ.
	 */
	dc_crtc->irq_dec_seqcomplete = 0;
	/*
	 * The per-block shadow-load IRQs are NOT used on i.MX95: a page
	 * flip with unchanged content produces no shadow load, so waiting
	 * on them would time out and stall the compositor.  The enable and
	 * flush paths poll the ExtDst PixEngCfg SYNC_BUSY bit instead.
	 */
	dc_crtc->irq_dec_shdload = 0;
	dc_crtc->irq_db_shdload = 0;
	dc_crtc->db_dev = dc_crtc->db ?
		dc_imx95_db_dev(dc_crtc->db) : NULL;
	dc_crtc->irq_ed_safe_shdload = 0;
	dc_crtc->irq_ed_cont_shdload = 0;

	dev = dc_crtc->fg ? dc_crtc->fg->dev : drm->dev;

	dc_primary = &dc_drm->dc_primary[crtc_index];
	ret = dc_plane_init(dc_drm, dc_primary, BIT(crtc_index));
	if (ret) {
		dev_err(dev, "failed to initialize primary plane: %d\n",
			ret);
		return ret;
	}

	/*
	 * i.MX95 has no reliable hardware vblank counter; use CRTC ops that do
	 * not read FrameGen's frame/line index (see dc_crtc_swvblank_funcs).
	 * vblank is still driven by the DISENGCFG frame-complete IRQ when wired
	 * (software-counted), or by the software timer otherwise.
	 */
	drm_crtc_helper_add(&dc_crtc->base, &dc_helper_swvblank_funcs);
	ret = drm_crtc_init_with_planes(drm, &dc_crtc->base,
					&dc_primary->base, NULL,
					&dc_crtc_swvblank_funcs, NULL);
	if (ret) {
		dev_err(dev, "failed to add CRTC: %d\n", ret);
		return ret;
	}

	/* One-time static hw init of the i.MX95 content-path blocks. */
	dc_crtc_pe_hw_init(dc_crtc);

	return ret;
}

int dc_crtc_post_init(struct dc_drm_device *dc_drm, int crtc_index)
{
	struct dc_crtc *dc_crtc = &dc_drm->dc_crtc[crtc_index];
	struct drm_device *drm = &dc_drm->base;

	return dc_crtc_request_irqs(drm, dc_crtc);
}
