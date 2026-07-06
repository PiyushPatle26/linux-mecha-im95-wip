// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 *
 * DC COMCTRL interrupt controller: chained irqchip with irq_domain and
 * generic irq chips.  Supports both i.MX8QXP (USERINTERRUPT* dual-mode)
 * and i.MX95 (single-mode INTERRUPT* at different offsets).
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

/* ------------------------------------------------------------------ */
/* Variant data: per-compatible register layout and config             */
/* ------------------------------------------------------------------ */

struct dc_ic_regs {
	unsigned int enable;
	unsigned int preset;
	unsigned int clear;
	unsigned int status;
};

struct dc_ic_variant {
	int			irq_count;
	int			irq_reserved;	/* -1 = none */
	int			banks;
	const unsigned long	*unused;

	/* Primary register set (used for generic chip mask/ack + handler) */
	const struct dc_ic_regs	*regs;	/* per bank */

	/* i.MX8QXP-only: secondary USER registers */
	const struct dc_ic_regs	*user_regs;	/* per bank, NULL = not present */
	unsigned int		user_mask_base;	/* USERINTERRUPTMASK(0) */
};

/* ------------------------------------------------------------------ */
/* 8QXP register layout                                               */
/* ------------------------------------------------------------------ */

static const struct dc_ic_regs dc_ic_8qxp_sys_regs[] = {
	{ 0x10, 0x18, 0x20, 0x28 },
	{ 0x14, 0x1c, 0x24, 0x2c },
};

static const struct dc_ic_regs dc_ic_8qxp_user_regs[] = {
	{ 0x40, 0x48, 0x50, 0x58 },
	{ 0x44, 0x4c, 0x54, 0x5c },
};

static const unsigned long dc_ic_8qxp_unused[] = {
	0x00000000,
	0xfffe0008,
};

/* ------------------------------------------------------------------ */
/* i.MX95 register layout (single mode, no USER registers)            */
/* ------------------------------------------------------------------ */

static const struct dc_ic_regs dc_ic_imx95_regs[] = {
	{ 0x08, 0x14, 0x20, 0x2c },
	{ 0x0c, 0x18, 0x24, 0x30 },
	{ 0x10, 0x1c, 0x28, 0x34 },
};

static const unsigned long dc_ic_imx95_unused[] = {
	0x00000000,
	0x00000000,
	0xffc00000,
};

/* ------------------------------------------------------------------ */
/* Variant descriptors                                                */
/* ------------------------------------------------------------------ */

static const struct dc_ic_variant dc_ic_8qxp = {
	.irq_count	= 49,
	.irq_reserved	= 35,
	.banks		= 2,
	.unused		= dc_ic_8qxp_unused,
	.regs		= dc_ic_8qxp_sys_regs,
	.user_regs	= dc_ic_8qxp_user_regs,
	.user_mask_base	= 0x08,
};

static const struct dc_ic_variant dc_ic_imx95 = {
	.irq_count	= 86,
	.irq_reserved	= -1,
	.banks		= 3,
	.unused		= dc_ic_imx95_unused,
	.regs		= dc_ic_imx95_regs,
	.user_regs	= NULL,
	.user_mask_base	= 0,
};

/* ------------------------------------------------------------------ */
/* Per-instance data                                                   */
/* ------------------------------------------------------------------ */

struct dc_ic_data {
	struct regmap		*regs;
	struct clk		*clk_axi;
	struct irq_domain	*domain;
	const struct dc_ic_variant *variant;
	int			*parent_irq;	/* physical parent or -ENOENT */
	struct dc_ic_entry	*entry;		/* one per hwirq */
};

struct dc_ic_entry {
	struct dc_ic_data *data;
	int		  hwirq;
};

/* ------------------------------------------------------------------ */
/* Chained handler                                                     */
/* ------------------------------------------------------------------ */

static void dc_ic_irq_handler(struct irq_desc *desc)
{
	struct dc_ic_entry *entry = irq_desc_get_handler_data(desc);
	struct dc_ic_data *data = entry->data;
	const struct dc_ic_regs *r;
	unsigned int hwirq = entry->hwirq;
	unsigned int bank = hwirq / 32;
	unsigned int virq;
	u32 status, enable;

	r = data->variant->user_regs ? data->variant->user_regs : data->variant->regs;

	chained_irq_enter(irq_desc_get_chip(desc), desc);

	regmap_read(data->regs, r[bank].status, &status);
	regmap_read(data->regs, r[bank].enable, &enable);

	if (status & enable & BIT(hwirq % 32)) {
		virq = irq_find_mapping(data->domain, hwirq);
		if (virq)
			generic_handle_irq(virq);
	}

	chained_irq_exit(irq_desc_get_chip(desc), desc);
}

/* ------------------------------------------------------------------ */
/* Regmap config, common: covers both variants' register ranges        */
/* ------------------------------------------------------------------ */

static const struct regmap_range dc_ic_regmap_write_ranges[] = {
	regmap_reg_range(0x08, 0x5c),
};

static const struct regmap_access_table dc_ic_regmap_write_table = {
	.yes_ranges = dc_ic_regmap_write_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_ic_regmap_write_ranges),
};

static const struct regmap_range dc_ic_regmap_read_ranges[] = {
	regmap_reg_range(0x08, 0x5c),
};

static const struct regmap_access_table dc_ic_regmap_read_table = {
	.yes_ranges = dc_ic_regmap_read_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_ic_regmap_read_ranges),
};

/* PRESET and CLEAR are write-only; STATUS is read-only.  Marking them
 * volatile prevents regmap from caching them (they would be stale).
 * We also include USER variants where present.
 */
static const struct regmap_range dc_ic_regmap_volatile_ranges[] = {
	regmap_reg_range(0x14, 0x34),
	regmap_reg_range(0x48, 0x5c),
};

static const struct regmap_access_table dc_ic_regmap_volatile_table = {
	.yes_ranges = dc_ic_regmap_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(dc_ic_regmap_volatile_ranges),
};

static const struct regmap_config dc_ic_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.fast_io	= true,
	.wr_table	= &dc_ic_regmap_write_table,
	.rd_table	= &dc_ic_regmap_read_table,
	.volatile_table	= &dc_ic_regmap_volatile_table,
	.max_register	= 0x5c,
};

/* ------------------------------------------------------------------ */
/* Probe / remove                                                      */
/* ------------------------------------------------------------------ */

static int dc_ic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct dc_ic_variant *variant;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	struct dc_ic_data *data;
	void __iomem *base;
	int i, ret;

	variant = of_device_get_match_data(dev);
	if (!variant)
		return -EINVAL;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->variant = variant;

	data->parent_irq = devm_kcalloc(dev, variant->irq_count,
					sizeof(*data->parent_irq), GFP_KERNEL);
	if (!data->parent_irq)
		return -ENOMEM;

	data->entry = devm_kcalloc(dev, variant->irq_count,
				   sizeof(*data->entry), GFP_KERNEL);
	if (!data->entry)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(dev, "failed to map regs\n");
		return PTR_ERR(base);
	}

	data->regs = devm_regmap_init_mmio(dev, base, &dc_ic_regmap_config);
	if (IS_ERR(data->regs))
		return PTR_ERR(data->regs);

	data->clk_axi = devm_clk_get(dev, NULL);
	if (IS_ERR(data->clk_axi))
		return dev_err_probe(dev, PTR_ERR(data->clk_axi),
				     "failed to get clock\n");

	/*
	 * Collect parent physical IRQs.  hwirqs that have no DT interrupt
	 * entry (i.MX95) return -ENXIO and are skipped.  On i.MX8QXP all
	 * non-reserved hwirqs must have a parent.
	 */
	for (i = 0; i < variant->irq_count; i++)
		data->parent_irq[i] = -ENOENT;

	for (i = 0; i < variant->irq_count; i++) {
		if (i == variant->irq_reserved)
			continue;

		ret = platform_get_irq_optional(pdev, i);
		if (ret == -ENXIO)
			continue;
		if (ret < 0)
			return ret;
		data->parent_irq[i] = ret;
	}

	dev_set_drvdata(dev, data);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err(dev, "failed to resume: %d\n", ret);
		return ret;
	}

	/* ---- hardware init ---- */

	if (variant->user_regs) {
		/* i.MX8QXP: mask + clear both USER and SYSTEM regs,
		 * then switch all IRQs to USER mode.
		 */
		for (i = 0; i < variant->banks; i++) {
			regmap_write(data->regs, variant->user_regs[i].enable, 0);
			regmap_write(data->regs, variant->regs[i].enable, 0);
			regmap_write(data->regs, variant->user_regs[i].clear,
				     0xffffffff);
			regmap_write(data->regs, variant->regs[i].clear,
				     0xffffffff);
			regmap_write(data->regs,
				     variant->user_mask_base + 4 * i,
				     0xffffffff);
		}
	} else {
		/* i.MX95: just mask + clear */
		for (i = 0; i < variant->banks; i++) {
			regmap_write(data->regs, variant->regs[i].enable, 0);
			regmap_write(data->regs, variant->regs[i].clear,
				     ~variant->unused[i]);
		}
	}

	/* ---- irq_domain + generic chips ---- */

	data->domain = irq_domain_add_linear(dev->of_node, variant->irq_count,
					     &irq_generic_chip_ops, data);
	if (!data->domain) {
		dev_err(dev, "failed to create IRQ domain\n");
		pm_runtime_put(dev);
		return -ENOMEM;
	}
	irq_domain_set_pm_device(data->domain, dev);

	ret = irq_alloc_domain_generic_chips(data->domain, 32, 1, "DC-IC",
					     handle_level_irq, 0, 0, 0);
	if (ret) {
		dev_err(dev, "failed to alloc generic IRQ chips: %d\n", ret);
		irq_domain_remove(data->domain);
		pm_runtime_put(dev);
		return ret;
	}

	for (i = 0; i < variant->banks; i++) {
		const struct dc_ic_regs *r;

		r = variant->user_regs ? variant->user_regs : variant->regs;

		gc = irq_get_domain_generic_chip(data->domain, i * 32);
		gc->reg_base = base;
		gc->unused   = variant->unused[i];
		ct = gc->chip_types;
		ct->chip.irq_ack    = irq_gc_ack_set_bit;
		ct->chip.irq_mask   = irq_gc_mask_clr_bit;
		ct->chip.irq_unmask = irq_gc_mask_set_bit;
		ct->regs.ack  = r[i].clear;
		ct->regs.mask = r[i].enable;
	}

	/* ---- chained handlers ---- */

	for (i = 0; i < variant->irq_count; i++) {
		if (i == variant->irq_reserved)
			continue;
		if (data->parent_irq[i] < 0)
			continue;

		data->entry[i].data  = data;
		data->entry[i].hwirq = i;

		irq_set_chained_handler_and_data(data->parent_irq[i],
						 dc_ic_irq_handler,
						 &data->entry[i]);
	}

	return 0;
}

static void dc_ic_remove(struct platform_device *pdev)
{
	struct dc_ic_data *data = dev_get_drvdata(&pdev->dev);
	int i;

	for (i = 0; i < data->variant->irq_count; i++) {
		if (i == data->variant->irq_reserved)
			continue;
		if (data->parent_irq[i] < 0)
			continue;
		irq_set_chained_handler_and_data(data->parent_irq[i],
						 NULL, NULL);
	}

	irq_domain_remove(data->domain);
	pm_runtime_put_sync(&pdev->dev);
}

/* ------------------------------------------------------------------ */
/* Runtime PM                                                          */
/* ------------------------------------------------------------------ */

static int dc_ic_runtime_suspend(struct device *dev)
{
	struct dc_ic_data *data = dev_get_drvdata(dev);

	clk_disable_unprepare(data->clk_axi);
	return 0;
}

static int dc_ic_runtime_resume(struct device *dev)
{
	struct dc_ic_data *data = dev_get_drvdata(dev);

	return clk_prepare_enable(data->clk_axi);
}

static const struct dev_pm_ops dc_ic_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				      pm_runtime_force_resume)
	RUNTIME_PM_OPS(dc_ic_runtime_suspend, dc_ic_runtime_resume, NULL)
};

/* ------------------------------------------------------------------ */
/* Driver registration                                                 */
/* ------------------------------------------------------------------ */

static const struct of_device_id dc_ic_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-dc-intc",
	  .data       = &dc_ic_8qxp },
	{ .compatible = "fsl,imx95-dc-intc",
	  .data       = &dc_ic_imx95 },
	{ /* sentinel */ }
};

struct platform_driver dc_ic_driver = {
	.probe  = dc_ic_probe,
	.remove = dc_ic_remove,
	.driver = {
		.name			= "imx8-dc-intc",
		.suppress_bind_attrs	= true,
		.of_match_table		= dc_ic_dt_ids,
		.pm			= pm_sleep_ptr(&dc_ic_pm_ops),
	},
};
