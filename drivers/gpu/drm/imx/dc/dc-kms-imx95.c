// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 *
 * i.MX95 KMS init.
 */

#include <drm/drm_device.h>
#include <drm/drm_managed.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "dc-de.h"
#include "dc-drv.h"
#include "dc-kms.h"

int dc_kms_init(struct dc_drm_device *dc_drm)
{
	struct drm_device *drm = &dc_drm->base;
	int ret, i;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 60;
	drm->mode_config.min_height = 60;
	drm->mode_config.max_width = 8192;
	drm->mode_config.max_height = 8192;
	drm->mode_config.funcs = &dc_drm_mode_config_funcs;

	drm->vblank_disable_immediate = true;
	/*
	 * i.MX95 has no wired frame-complete IRQ and uses a software hrtimer
	 * vblank with no hardware counter, which requires max_vblank_count == 0
	 * for DRM's software vblank accounting (see dc_crtc_swvblank_funcs).
	 */

	for (i = 0; i < DC_DISPLAYS; i++) {
		ret = dc_crtc_init(dc_drm, i);
		if (ret)
			return ret;

		ret = dc_kms_init_encoder_per_crtc(dc_drm, i);
		if (ret)
			return ret;
	}

	for (i = 0; i < DC_DISPLAYS; i++) {
		ret = dc_crtc_post_init(dc_drm, i);
		if (ret)
			return ret;
	}

	ret = drm_vblank_init(drm, DC_DISPLAYS);
	if (ret) {
		dev_err(drm->dev, "failed to init vblank support: %d\n", ret);
		return ret;
	}

	drm_mode_config_reset(drm);

	drm_kms_helper_poll_init(drm);

	return 0;
}

void dc_kms_uninit(struct dc_drm_device *dc_drm)
{
	drm_kms_helper_poll_fini(&dc_drm->base);
}
