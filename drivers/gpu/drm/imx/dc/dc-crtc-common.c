// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 *
 * CRTC helpers shared between the i.MX8QXP and i.MX95 Display Controller DRM
 * drivers.
 */

#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/module.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "dc-crtc-common.h"
#include "dc-de.h"
#include "dc-kms.h"

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

irqreturn_t dc_crtc_irq_handler_dec_framecomplete(int irq, void *dev_id)
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
EXPORT_SYMBOL_GPL(dc_crtc_irq_handler_dec_framecomplete);

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
