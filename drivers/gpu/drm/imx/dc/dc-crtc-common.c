// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 *
 * CRTC helpers shared between the i.MX8QXP and i.MX95 DC8000 DRM drivers.
 */

#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "dc-crtc-common.h"
#include "dc-de.h"
#include "dc-dprc.h"
#include "dc-drv.h"
#include "dc-fu.h"
#include "dc-kms.h"
#include "dc-pe.h"

static enum hrtimer_restart dc_crtc_vblank_hrtimer(struct hrtimer *timer)
{
	struct dc_crtc *dc_crtc =
		container_of(timer, struct dc_crtc, vblank_hrtimer);

	drm_crtc_handle_vblank(&dc_crtc->base);
	hrtimer_forward_now(timer, dc_crtc->vblank_period);
	return HRTIMER_RESTART;
}

void dc_crtc_setup_vblank_hrtimer(struct dc_crtc *dc_crtc)
{
	hrtimer_setup(&dc_crtc->vblank_hrtimer, dc_crtc_vblank_hrtimer,
		      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
}
EXPORT_SYMBOL_GPL(dc_crtc_setup_vblank_hrtimer);

static ktime_t dc_crtc_vblank_period(struct drm_crtc *crtc)
{
	const struct drm_display_mode *mode;
	u64 pixels_per_frame, clock_hz;

	if (!crtc->state)
		return ms_to_ktime(16);

	mode = &crtc->state->adjusted_mode;
	pixels_per_frame = (u64)mode->htotal * mode->vtotal;
	clock_hz = (u64)mode->clock * 1000;

	if (!clock_hz || !pixels_per_frame)
		return ms_to_ktime(16);

	return ns_to_ktime(div64_u64(pixels_per_frame * NSEC_PER_SEC, clock_hz));
}

int dc_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);

	if (dc_crtc->irq_dec_framecomplete) {
		enable_irq(dc_crtc->irq_dec_framecomplete);
	} else {
		dc_crtc->vblank_period = dc_crtc_vblank_period(crtc);
		hrtimer_start(&dc_crtc->vblank_hrtimer,
			      dc_crtc->vblank_period, HRTIMER_MODE_REL);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dc_crtc_enable_vblank);

void dc_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);

	if (dc_crtc->irq_dec_framecomplete)
		disable_irq_nosync(dc_crtc->irq_dec_framecomplete);
	else
		hrtimer_cancel(&dc_crtc->vblank_hrtimer);
}
EXPORT_SYMBOL_GPL(dc_crtc_disable_vblank);

enum drm_mode_status
dc_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode)
{
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	enum drm_mode_status status;

	status = dc_fg_check_clock(dc_crtc->fg, mode->clock);
	if (status != MODE_OK)
		return status;

	if (mode->crtc_clock > DC_FRAMEGEN_MAX_CLOCK_KHZ)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}
EXPORT_SYMBOL_GPL(dc_crtc_mode_valid);

void dc_crtc_queue_state_event(struct drm_crtc_state *crtc_state, bool may_defer)
{
	struct drm_crtc *crtc = crtc_state->crtc;
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc_state->event) {
		if (may_defer && dc_crtc->irq_dec_framecomplete) {
			WARN_ON(drm_crtc_vblank_get(crtc));
			WARN_ON(dc_crtc->event);
			dc_crtc->event = crtc_state->event;
		} else {
			drm_crtc_send_vblank_event(crtc, crtc_state->event);
		}
		crtc_state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}
EXPORT_SYMBOL_GPL(dc_crtc_queue_state_event);

int
dc_crtc_atomic_check(struct drm_crtc *crtc, struct drm_atomic_commit *state)
{
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_display_mode *adj = &new_crtc_state->adjusted_mode;
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	enum drm_mode_status status;

	status = dc_fg_check_clock(dc_crtc->fg, adj->clock);
	if (status != MODE_OK)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(dc_crtc_atomic_check);

void
dc_crtc_atomic_begin(struct drm_crtc *crtc, struct drm_atomic_commit *state)
{
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, crtc);
	struct dc_drm_device *dc_drm = to_dc_drm_device(crtc->dev);
	int idx, ret;

	if (!drm_atomic_crtc_needs_modeset(new_crtc_state) ||
	    !new_crtc_state->active)
		return;

	if (!drm_dev_enter(crtc->dev, &idx))
		return;

	/*
	 * i.MX8QXP: the pixel engine (PE) owns the AXI bus clock for all
	 * fetch units, so resume pe->dev (dc_pe_runtime_resume enables clk_axi).
	 * i.MX95 has no PE; its DC device PM is managed in atomic_enable/disable.
	 */
	if (dc_drm->pe && dc_drm->pe->dev) {
		ret = pm_runtime_resume_and_get(dc_drm->pe->dev);
		if (ret)
			dc_crtc_err(crtc,
				    "failed to get DC pixel engine RPM: %d\n",
				    ret);
	}

	drm_dev_exit(idx);
}
EXPORT_SYMBOL_GPL(dc_crtc_atomic_begin);

void
dc_crtc_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_commit *state)
{
	struct drm_crtc_state *old_crtc_state =
				drm_atomic_get_old_crtc_state(state, crtc);
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_plane_state *old_plane_state =
			drm_atomic_get_old_plane_state(state, crtc->primary);
	struct drm_plane_state *new_plane_state =
			drm_atomic_get_new_plane_state(state, crtc->primary);
	struct dc_plane *dc_plane = to_dc_plane(crtc->primary);
	struct dc_crtc *dc_crtc = to_dc_crtc(crtc);
	bool disabling_plane;
	int idx;

	if ((!dc_crtc->db && drm_atomic_crtc_needs_modeset(new_crtc_state)) ||
	    (!old_crtc_state->active && !new_crtc_state->active))
		return;

	/*
	 * i.MX95: on a modeset the ExtDst content sync trigger is issued by
	 * ->atomic_enable, after the FrameGen has been started.  atomic_flush
	 * runs (commit_planes) BEFORE ->atomic_enable (commit_modeset_enables),
	 * i.e. while the FrameGen is still disabled from the preceding
	 * ->atomic_disable.  A sync trigger issued here would raise the ExtDst
	 * external-kick SYNC_BUSY with no running FrameGen to kick it, wedging
	 * the shadow load for the whole modeset.  The flip event is handled by
	 * ->atomic_enable; only non-modeset flips flush their plane update here.
	 */
	if (dc_crtc->db && drm_atomic_crtc_needs_modeset(new_crtc_state))
		return;

	if (!drm_dev_enter(crtc->dev, &idx))
		goto out;

	if (dc_crtc->irq_ed_cont_shdload)
		enable_irq(dc_crtc->irq_ed_cont_shdload);

	disabling_plane = drm_atomic_plane_disabling(old_plane_state,
						     new_plane_state);

	if (disabling_plane && dc_plane->fu->dprc) {
		unsigned long flags;

		dc_crtc_dbg(crtc, "disabling plane\n");

		/*
		 * Don't relinquish CPU until DPRC REPEAT_EN is disabled and
		 * sync is triggered.
		 */
		local_irq_save(flags);
		preempt_disable();

		DC_CRTC_WAIT_FOR_FRAMEGEN_FRAME_INDEX_MOVING(dc_crtc->fg);
		dc_dprc_disable_repeat_en(dc_plane->fu->dprc);
		/* flush plane update out to display */
		dc_ed_pec_sync_trigger(dc_crtc->ed_cont);

		local_irq_restore(flags);
		preempt_enable();
	} else {
		/* flush plane update out to display */
		dc_ed_pec_sync_trigger(dc_crtc->ed_cont);
	}

	if (dc_crtc->irq_ed_cont_shdload) {
		DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(ed_cont_shdload_done);
		disable_irq(dc_crtc->irq_ed_cont_shdload);
	}

	if (disabling_plane && dc_plane->fu->dprc)
		dc_dprc_disable(dc_plane->fu->dprc);

	DC_CRTC_CHECK_FRAMEGEN_FIFO(dc_crtc->fg);

	drm_dev_exit(idx);

out:
	dc_crtc_queue_state_event(new_crtc_state, !dc_crtc->db);
}
EXPORT_SYMBOL_GPL(dc_crtc_atomic_flush);

static irqreturn_t dc_crtc_irq_handler_dec_framecomplete(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;
	struct drm_crtc *crtc = &dc_crtc->base;
	unsigned long flags;

	drm_crtc_handle_vblank(crtc);

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (dc_crtc->event) {
		drm_crtc_send_vblank_event(crtc, dc_crtc->event);
		dc_crtc->event = NULL;
		drm_crtc_vblank_put(crtc);
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t
dc_crtc_irq_handler_dec_seqcomplete_done(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;

	complete(&dc_crtc->dec_seqcomplete_done);

	return IRQ_HANDLED;
}

static irqreturn_t dc_crtc_irq_handler_dec_shdload_done(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;

	complete(&dc_crtc->dec_shdload_done);

	return IRQ_HANDLED;
}

static irqreturn_t
dc_crtc_irq_handler_ed_cont_shdload_done(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;

	complete(&dc_crtc->ed_cont_shdload_done);

	return IRQ_HANDLED;
}

static irqreturn_t
dc_crtc_irq_handler_ed_safe_shdload_done(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;

	complete(&dc_crtc->ed_safe_shdload_done);

	return IRQ_HANDLED;
}

static irqreturn_t
dc_crtc_irq_handler_db_shdload_done(int irq, void *dev_id)
{
	struct dc_crtc *dc_crtc = dev_id;

	complete(&dc_crtc->db_shdload_done);

	return IRQ_HANDLED;
}

int dc_crtc_request_irqs(struct drm_device *drm, struct dc_crtc *dc_crtc)
{
	struct {
		struct device *dev;
		unsigned int irq;
		irqreturn_t (*irq_handler)(int irq, void *dev_id);
	} irqs[DC_CRTC_IRQS] = {
		{
			dc_crtc->irq_dec_dev,
			dc_crtc->irq_dec_framecomplete,
			dc_crtc_irq_handler_dec_framecomplete,
		}, {
			dc_crtc->irq_dec_dev,
			dc_crtc->irq_dec_seqcomplete,
			dc_crtc_irq_handler_dec_seqcomplete_done,
		}, {
			dc_crtc->irq_dec_dev,
			dc_crtc->irq_dec_shdload,
			dc_crtc_irq_handler_dec_shdload_done,
		}, {
			dc_crtc->ed_cont ? dc_crtc->ed_cont->dev : NULL,
			dc_crtc->irq_ed_cont_shdload,
			dc_crtc_irq_handler_ed_cont_shdload_done,
		}, {
			dc_crtc->ed_safe ? dc_crtc->ed_safe->dev : NULL,
			dc_crtc->irq_ed_safe_shdload,
			dc_crtc_irq_handler_ed_safe_shdload_done,
		}, {
			dc_crtc->db_dev,
			dc_crtc->irq_db_shdload,
			dc_crtc_irq_handler_db_shdload_done,
		},
	};
	int i, ret;

	for (i = 0; i < DC_CRTC_IRQS; i++) {
		struct dc_crtc_irq *irq = &dc_crtc->irqs[i];

		if (!irqs[i].dev || !irqs[i].irq)
			continue;

		ret = devm_request_irq(irqs[i].dev, irqs[i].irq,
				       irqs[i].irq_handler, IRQF_NO_AUTOEN,
				       dev_name(irqs[i].dev), dc_crtc);
		if (ret) {
			dev_err(irqs[i].dev, "failed to request irq(%u): %d\n",
				irqs[i].irq, ret);
			return ret;
		}

		irq->dc_crtc = dc_crtc;
		irq->irq = irqs[i].irq;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dc_crtc_request_irqs);
