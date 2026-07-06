// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 *
 * i.MX95 Dither component driver.
 */

#include <linux/bits.h>
#include <linux/component.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "dc-drv.h"
#include "dc-imx95-di.h"
#include "dc-lib.h"

/*
 * Dither register layout: only the aux (PixEngPath) sub-block is
 * used here to set polarity; the main CFG block has no init work.
 * aux addresses: DI0=0x4b711000, DI1=0x4b771020 (base+aux_ofs)
 */
#define POLARITYCTRL		0x8
#define  POLEN			BIT(2)
#define  POLVS			BIT(1)
#define  POLHS			BIT(0)

struct dc_imx95_di {
	void __iomem *aux_base;
	struct device *dev;
	int irq_framecomplete;
	int irq_shdload;
	int irq_seqcomplete;
};

int dc_imx95_di_shdload_irq(struct dc_imx95_di *di)
{
	return di->irq_shdload;
}

int dc_imx95_di_seqcomplete_irq(struct dc_imx95_di *di)
{
	return di->irq_seqcomplete;
}

struct device *dc_imx95_di_dev(struct dc_imx95_di *di)
{
	return di->dev;
}
EXPORT_SYMBOL_GPL(dc_imx95_di_dev);

int dc_imx95_di_framecomplete_irq(struct dc_imx95_di *di)
{
	return di->irq_framecomplete;
}
EXPORT_SYMBOL_GPL(dc_imx95_di_framecomplete_irq);

static const struct dc_subdev_info dc_imx95_di_info[] = {
	{ .reg_start = 0x4b711000, .id = 0, },
	{ .reg_start = 0x4b771020, .id = 1, },
};

static inline void di_write(struct dc_imx95_di *di,
			    unsigned int offset, u32 val)
{
	writel(val, di->aux_base + offset);
}

static inline void di_rmw(struct dc_imx95_di *di,
			  unsigned int offset, u32 mask, u32 val)
{
	u32 tmp = readl(di->aux_base + offset);

	writel((tmp & ~mask) | val, di->aux_base + offset);
}

void dc_imx95_di_polhs_active_high(struct dc_imx95_di *di)
{
	di_rmw(di, POLARITYCTRL, POLHS, POLHS);
}
EXPORT_SYMBOL_GPL(dc_imx95_di_polhs_active_high);

void dc_imx95_di_polhs_active_low(struct dc_imx95_di *di)
{
	di_rmw(di, POLARITYCTRL, POLHS, 0);
}
EXPORT_SYMBOL_GPL(dc_imx95_di_polhs_active_low);

void dc_imx95_di_polvs_active_high(struct dc_imx95_di *di)
{
	di_rmw(di, POLARITYCTRL, POLVS, POLVS);
}
EXPORT_SYMBOL_GPL(dc_imx95_di_polvs_active_high);

void dc_imx95_di_polvs_active_low(struct dc_imx95_di *di)
{
	di_rmw(di, POLARITYCTRL, POLVS, 0);
}
EXPORT_SYMBOL_GPL(dc_imx95_di_polvs_active_low);

void dc_imx95_di_polen_active_high(struct dc_imx95_di *di)
{
	di_rmw(di, POLARITYCTRL, POLEN, POLEN);
}
EXPORT_SYMBOL_GPL(dc_imx95_di_polen_active_high);

void dc_imx95_di_polen_active_low(struct dc_imx95_di *di)
{
	di_rmw(di, POLARITYCTRL, POLEN, 0);
}

static int dc_imx95_di_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dc_drm_device *dc_drm = data;
	struct resource *res;
	struct dc_imx95_di *di;
	int id;

	di = devm_kzalloc(dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = dev;

	/* resource 0 = aux/PixEngPath (polarity regs) */
	di->aux_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(di->aux_base))
		return PTR_ERR(di->aux_base);

	/*
	 * DISENGCFG frame-complete interrupt (via the DC interrupt controller),
	 * used as the CRTC vblank source.  Optional: when absent the CRTC falls
	 * back to a software vblank timer.
	 */
	di->irq_framecomplete =
		platform_get_irq_byname_optional(pdev, "framecomplete");
	if (di->irq_framecomplete == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (di->irq_framecomplete < 0)
		di->irq_framecomplete = 0;

	/* DISENGCFG shadow-load interrupt (via the DC interrupt controller). */
	di->irq_shdload = platform_get_irq_byname_optional(pdev, "shdload");
	if (di->irq_shdload == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (di->irq_shdload < 0)
		di->irq_shdload = 0;

	/*
	 * DISENGCFG sequence-complete interrupt.  Used by the CRTC disable path
	 * to wait for the FrameGen to cleanly finish its output sequence before
	 * the pixel clock is pulled; without it a mode-change re-tune re-enables
	 * a FrameGen left in a dirty state and its ExtDst shadow load never
	 * latches (shadow load timeout / primary channel not syncup).
	 */
	di->irq_seqcomplete = platform_get_irq_byname_optional(pdev, "seqcomplete");
	if (di->irq_seqcomplete == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (di->irq_seqcomplete < 0)
		di->irq_seqcomplete = 0;

	id = dc_subdev_get_id(dc_imx95_di_info,
			      ARRAY_SIZE(dc_imx95_di_info), res);
	if (id < 0) {
		dev_err(dev, "failed to get instance number: %d\n", id);
		return id;
	}

	if (dc_drm->imx95 && id < 2)
		dc_drm->imx95->di[id] = di;

	return 0;
}

static const struct component_ops dc_imx95_di_ops = {
	.bind = dc_imx95_di_bind,
};

static int dc_imx95_di_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dc_imx95_di_ops);
}

static void dc_imx95_di_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dc_imx95_di_ops);
}

static const struct of_device_id dc_imx95_di_dt_ids[] = {
	{ .compatible = "fsl,imx95-dc-dither" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_imx95_di_dt_ids);

struct platform_driver dc_imx95_dither_driver = {
	.probe  = dc_imx95_di_probe,
	.remove = dc_imx95_di_remove,
	.driver = {
		.name = "imx95-dc-dither",
		.suppress_bind_attrs = true,
		.of_match_table = dc_imx95_di_dt_ids,
	},
};
