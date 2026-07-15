/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 *
 * CRTC helpers shared between the i.MX8QXP and i.MX95 Display Controller
 * DRM drivers: the vblank hrtimer, IRQ handlers, dc_crtc_request_irqs()
 * and dc_crtc_queue_state_event().  No SoC identity checks and no
 * struct drm_driver coupling; each SoC driver keeps its own
 * drm_crtc_funcs/drm_crtc_helper_funcs and calls into these.
 *
 * mode_valid/atomic_check/atomic_begin/atomic_flush are not here on
 * purpose: the hardware sequences differ per SoC, so each SoC driver
 * implements its own instead of sharing one behind conditionals.
 */

#ifndef __DC_CRTC_COMMON_H__
#define __DC_CRTC_COMMON_H__

#include <linux/container_of.h>
#include <linux/interrupt.h>

#include <drm/drm_print.h>

#include "dc-kms.h"

static inline struct dc_crtc *to_dc_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct dc_crtc, base);
}

#define dc_crtc_dbg(crtc, fmt, ...)					\
do {									\
	struct drm_crtc *_crtc = (crtc);				\
	drm_dbg_kms(_crtc->dev, "[CRTC:%d:%s] " fmt,			\
		    _crtc->base.id, _crtc->name, ##__VA_ARGS__);	\
} while (0)

#define dc_crtc_err(crtc, fmt, ...)					\
do {									\
	struct drm_crtc *_crtc = (crtc);				\
	drm_err(_crtc->dev, "[CRTC:%d:%s] " fmt,			\
		_crtc->base.id, _crtc->name, ##__VA_ARGS__);		\
} while (0)

#define DC_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(c)				\
do {									\
	unsigned long ret;						\
	ret = wait_for_completion_timeout(&dc_crtc->c, HZ);		\
	if (ret == 0)							\
		dc_crtc_err(crtc, "%s: wait for " #c " timeout\n",	\
							__func__);	\
} while (0)

#define DC_CRTC_CHECK_FRAMEGEN_FIFO(fg)					\
do {									\
	struct dc_fg *_fg = (fg);					\
	if (dc_fg_secondary_requests_to_read_empty_fifo(_fg)) {		\
		dc_fg_secondary_clear_channel_status(_fg);		\
		dc_crtc_err(crtc, "%s: FrameGen FIFO empty\n",		\
							__func__);	\
	}								\
} while (0)

#define DC_CRTC_WAIT_FOR_FRAMEGEN_SECONDARY_SYNCUP(fg)			\
do {									\
	if (dc_fg_wait_for_secondary_syncup(fg))			\
		dc_crtc_err(crtc,					\
			"%s: FrameGen secondary channel isn't syncup\n",\
							__func__);	\
} while (0)

#define DC_CRTC_WAIT_FOR_FRAMEGEN_FRAME_INDEX_MOVING(fg)		\
do {									\
	if (!dc_fg_wait_for_frame_index_moving(fg))			\
		dc_crtc_err(crtc,					\
			"%s: FrameGen frame index isn't moving\n",	\
							__func__);	\
} while (0)

void dc_crtc_setup_vblank_hrtimer(struct dc_crtc *dc_crtc);
int dc_crtc_enable_vblank(struct drm_crtc *crtc);
void dc_crtc_disable_vblank(struct drm_crtc *crtc);

/*
 * may_defer: cache the event and send it from the frame-complete IRQ handler
 * instead of sending it immediately.  Only meaningful when
 * dc_crtc->irq_dec_framecomplete is wired; i.MX95 always passes false since
 * its atomic_enable already knows the modeset is complete when it queues the
 * event.
 */
void dc_crtc_queue_state_event(struct drm_crtc_state *crtc_state, bool may_defer);

int dc_crtc_request_irqs(struct drm_device *drm, struct dc_crtc *dc_crtc);

/*
 * Exported so i.MX95 can devm_free_irq()/devm_request_irq() this specific
 * IRQ around each modeset - see dc-crtc-imx95.c.
 */
irqreturn_t dc_crtc_irq_handler_dec_framecomplete(int irq, void *dev_id);

#endif /* __DC_CRTC_COMMON_H__ */
