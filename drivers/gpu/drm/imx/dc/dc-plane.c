// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/container_of.h>
#include <linux/module.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>

#include "dc-dprc.h"
#include "dc-drv.h"
#include "dc-fu.h"
#include "dc-kms.h"

#define DC_PLANE_MAX_PITCH	0x10000
#define DC_PLANE_MAX_PIX_CNT	8192

#define dc_plane_dbg(plane, fmt, ...)					\
do {									\
	struct drm_plane *_plane = (plane);				\
	drm_dbg_kms(_plane->dev, "[PLANE:%d:%s] " fmt,			\
		    _plane->base.id, _plane->name, ##__VA_ARGS__);	\
} while (0)

static const uint32_t dc_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const struct drm_plane_funcs dc_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static int dc_plane_check_max_source_resolution(struct drm_plane_state *state)
{
	int src_h = drm_rect_height(&state->src) >> 16;
	int src_w = drm_rect_width(&state->src) >> 16;

	if (src_w > DC_PLANE_MAX_PIX_CNT || src_h > DC_PLANE_MAX_PIX_CNT) {
		dc_plane_dbg(state->plane, "invalid source resolution\n");
		return -EINVAL;
	}

	return 0;
}

static int dc_plane_check_fb(struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	dma_addr_t baseaddr = drm_fb_dma_get_gem_addr(fb, state, 0);

	/* base address alignment */
	if (baseaddr & 0x3) {
		dc_plane_dbg(state->plane, "fb bad baddr alignment\n");
		return -EINVAL;
	}

	/* pitches[0] range */
	if (fb->pitches[0] > DC_PLANE_MAX_PITCH) {
		dc_plane_dbg(state->plane, "fb pitches[0] is out of range\n");
		return -EINVAL;
	}

	/* pitches[0] alignment */
	if (fb->pitches[0] & 0x3) {
		dc_plane_dbg(state->plane, "fb bad pitches[0] alignment\n");
		return -EINVAL;
	}

	return 0;
}

static int dc_plane_check_dprc(struct drm_plane_state *state)
{
	struct dc_plane *dplane = to_dc_plane(state->plane);
	struct drm_framebuffer *fb = state->fb;
	dma_addr_t baseaddr = drm_fb_dma_get_gem_addr(fb, state, 0);
	struct dc_dprc *dprc = dplane->fu->dprc;
	u32 src_w = drm_rect_width(&state->src) >> 16;

	/* i.MX95 has no DPRC; skip checks that require it */
	if (!dprc)
		return 0;

	if (!dc_dprc_rtram_width_supported(dprc, src_w)) {
		dc_plane_dbg(state->plane, "bad RTRAM width for DPRC\n");
		return -EINVAL;
	}

	if (!dc_dprc_stride_supported(dprc, fb->pitches[0], src_w, fb->format,
				      baseaddr)) {
		dc_plane_dbg(state->plane, "fb bad pitches[0] for DPRC\n");
		return -EINVAL;
	}

	return 0;
}

static int
dc_plane_atomic_check(struct drm_plane *plane, struct drm_atomic_commit *state)
{
	struct drm_plane_state *plane_state =
				drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *crtc_state;
	int ret;

	/* ok to disable */
	if (!plane_state->fb)
		return 0;

	if (!plane_state->crtc) {
		dc_plane_dbg(plane, "no CRTC in plane state\n");
		return -EINVAL;
	}

	crtc_state =
		drm_atomic_get_new_crtc_state(state, plane_state->crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	ret = drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  true, false);
	if (ret) {
		dc_plane_dbg(plane, "failed to check plane state: %d\n", ret);
		return ret;
	}

	ret = dc_plane_check_max_source_resolution(plane_state);
	if (ret)
		return ret;

	ret = dc_plane_check_fb(plane_state);
	if (ret)
		return ret;

	return dc_plane_check_dprc(plane_state);
}

static void
dc_plane_atomic_update(struct drm_plane *plane, struct drm_atomic_commit *state)
{
	struct drm_plane_state *new_state =
				drm_atomic_get_new_plane_state(state, plane);
	struct drm_plane_state *old_state =
				drm_atomic_get_old_plane_state(state, plane);
	struct drm_crtc_state *new_crtc_state =
				drm_atomic_get_new_crtc_state(state, new_state->crtc);
	bool needs_modeset = drm_atomic_crtc_needs_modeset(new_crtc_state);
	bool prefetch_start = needs_modeset || !old_state->fb;
	struct dc_plane *dplane = to_dc_plane(plane);
	struct drm_framebuffer *fb = new_state->fb;
	const struct dc_fu_ops *fu_ops;
	struct dc_lb *lb = dplane->lb;
	struct dc_fu *fu = dplane->fu;
	dma_addr_t baseaddr;
	int src_w, src_h;
	int idx;

	if (!drm_dev_enter(plane->dev, &idx))
		return;

	src_w = drm_rect_width(&new_state->src) >> 16;
	src_h = drm_rect_height(&new_state->src) >> 16;

	baseaddr = drm_fb_dma_get_gem_addr(fb, new_state, 0);

	fu_ops = dc_fu_get_ops(dplane->fu);

	fu_ops->set_layerblend(fu, lb);
	fu_ops->set_burstlength(fu, baseaddr);
	fu_ops->set_src_stride(fu, DC_FETCHUNIT_FRAC0, src_w,
			       fb->format->cpp[0] * 8, baseaddr);
	fu_ops->set_src_buf_dimensions(fu, DC_FETCHUNIT_FRAC0, src_w, src_h);
	fu_ops->set_fmt(fu, DC_FETCHUNIT_FRAC0, fb->format);
	fu_ops->set_framedimensions(fu, src_w, src_h);
	fu_ops->set_baseaddress(fu, DC_FETCHUNIT_FRAC0, baseaddr);
	fu_ops->enable_src_buf(fu, DC_FETCHUNIT_FRAC0);

	dc_plane_dbg(plane, "uses %s\n", fu_ops->get_name(fu));

	if (fu->dprc)
		dc_dprc_configure(fu->dprc, new_state->crtc->index, src_w, src_h,
				  fb->pitches[0], fb->format, baseaddr, prefetch_start);

	/*
	 * Background (ConstFrame) on PRIM, plane (FetchUnit) on SEC; the blend
	 * emits SEC. Same topology on i.MX8QXP and i.MX95 (see the LB prim/sec
	 * select lists in dc-lb.c).
	 */
	dc_lb_pec_dynamic_prim_sel(lb, dc_cf_get_link_id(dplane->cf));
	dc_lb_pec_dynamic_sec_sel(lb, fu_ops->get_link_id(fu));
	dc_lb_mode(lb, LB_BLEND);
	dc_lb_position(lb, new_state->dst.x1, new_state->dst.y1);
	dc_lb_pec_clken(lb, CLKEN_AUTOMATIC);

	dc_plane_dbg(plane, "uses LayerBlend%d\n", dc_lb_get_id(lb));

	/* set ExtDst's source to LayerBlend and enable clock */
	dc_ed_pec_src_sel(dplane->ed, dc_lb_get_link_id(lb));
	dc_ed_pec_clken(dplane->ed, CLKEN_AUTOMATIC);

	drm_dev_exit(idx);
}

static void dc_plane_atomic_disable(struct drm_plane *plane,
				    struct drm_atomic_commit *state)
{
	struct dc_plane *dplane = to_dc_plane(plane);
	const struct dc_fu_ops *fu_ops;
	int idx;

	if (!drm_dev_enter(plane->dev, &idx))
		return;

	/* disable fetchunit in shadow */
	fu_ops = dc_fu_get_ops(dplane->fu);
	fu_ops->disable_src_buf(dplane->fu, DC_FETCHUNIT_FRAC0);

	/* set ExtDst's source to ConstFrame for background */
	if (dplane->cf) {
		dc_ed_pec_src_sel(dplane->ed, dc_cf_get_link_id(dplane->cf));
		dc_ed_pec_clken(dplane->ed, CLKEN_AUTOMATIC);
	}

	drm_dev_exit(idx);
}

static const struct drm_plane_helper_funcs dc_plane_helper_funcs = {
	.atomic_check = dc_plane_atomic_check,
	.atomic_update = dc_plane_atomic_update,
	.atomic_disable = dc_plane_atomic_disable,
};

int dc_plane_init(struct dc_drm_device *dc_drm, struct dc_plane *dc_plane,
		  unsigned int possible_crtcs)
{
	struct drm_plane *plane = &dc_plane->base;
	int ret;

	ret = drm_universal_plane_init(&dc_drm->base, plane, possible_crtcs,
				       &dc_plane_funcs,
				       dc_plane_formats,
				       ARRAY_SIZE(dc_plane_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_plane_helper_add(plane, &dc_plane_helper_funcs);

	dc_plane->cf = dc_drm->pe->cf_cont[plane->index];
	dc_plane->ed = dc_drm->pe->ed_cont[plane->index];

	if (dc_drm->imx95) {
		/*
		 * FetchLayer0 is the FetchUnit wired into stream 1's blend
		 * chain (LB6); the working reference drives stream 1 from
		 * FL0.  Map stream 1 -> FL0 (fu_disp[0]) and stream 0 -> FL1
		 * so a single active display gets the correct fetch path.
		 */
		static const int imx95_fu_map[] = { 1, 0 };
		int fu_idx = plane->index < ARRAY_SIZE(imx95_fu_map) ?
			     imx95_fu_map[plane->index] : plane->index;

		dc_plane->fu = dc_drm->pe->fu_disp[fu_idx];
	} else {
		dc_plane->fu = dc_drm->pe->fu_disp[plane->index];
	}

	if (dc_drm->imx95) {
		/*
		 * pe->lb[] is indexed by hardware LayerBlend id (1..6 on i.MX95).
		 * Each display stream's blend chain terminates at a fixed
		 * LayerBlend that is wired to that stream's ExtDst: stream 0 ->
		 * LB1, stream 1 -> LB6.  ExtDst must source that terminal LB or
		 * the pixel/kick path never closes (FrameGen panics: green).
		 */
		static const int imx95_lb_map[] = { 1, 6 };
		int lb_idx = plane->index < ARRAY_SIZE(imx95_lb_map) ?
			     imx95_lb_map[plane->index] : plane->index;

		dc_plane->lb = dc_drm->pe->lb[lb_idx];
	} else {
		dc_plane->lb = dc_drm->pe->lb[plane->index];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dc_plane_init);
