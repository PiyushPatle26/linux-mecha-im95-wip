# SPDX-License-Identifier: GPL-2.0

# Common hardware sub-block library.
# Hidden tristate; automatically selected by the per-SoC drivers below.
# Contains the sub-block component drivers and the CRTC/KMS helpers shared
# between the two independent SoC drivers below.  Each SoC driver implements
# its own struct drm_driver, probe/bind and mode-setting callback tables.
imx-dc-lib-objs := dc-cf.o dc-ed.o dc-fg.o dc-lb.o dc-fu.o dc-fw.o \
		   dc-ic.o dc-tc.o dc-de.o dc-pe.o dc-prg.o dc-dprc.o \
		   dc-fl.o \
		   dc-imx95-db.o dc-imx95-di.o \
		   dc-plane.o dc-kms.o dc-crtc-common.o dc-subblocks.o
obj-$(CONFIG_DRM_IMX_DC_LIB) += imx-dc-lib.o

# i.MX8QXP DC8000 DRM driver: own struct drm_driver, probe/bind, CRTC/KMS.
imx8qxp-dc-drm-objs := dc-drv-imx8qxp.o dc-crtc-imx8qxp.o dc-kms-imx8qxp.o
obj-$(CONFIG_DRM_IMX8QXP_DC) += imx8qxp-dc-drm.o

# i.MX95 DC8000 DRM driver: own struct drm_driver, probe/bind, CRTC/KMS.
imx95-dc-drm-objs := dc-drv-imx95.o dc-crtc-imx95.o dc-kms-imx95.o
obj-$(CONFIG_DRM_IMX95_DC) += imx95-dc-drm.o
