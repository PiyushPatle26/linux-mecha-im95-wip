// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 *
 * i.MX95 DomainBlend component driver.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/component.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "dc-drv.h"
#include "dc-imx95-db.h"
#include "dc-lib.h"

#define STATICCONTROL			0x8
#define  SHDTOKSEL_MASK			GENMASK(6, 4)
#define  SHDTOKSEL_SW			(0x4 << 4)
#define  SHDLDSEL_MASK			GENMASK(3, 1)
#define  SHDLDSEL_SW			(0x4 << 1)

#define CONTROLTRIGGER			0xc
#define  SHDTOKGEN			BIT(0)

#define MODECONTROL			0x10

#define ALPHACONTROL			0x14
#define  ALPHAMASKENABLE		BIT(0)

#define BLENDCONTROL			0x18
#define  PRIM_C_BLD_FUNC_SHIFT		0
#define  SEC_C_BLD_FUNC_SHIFT		4
#define  PRIM_A_BLD_FUNC_SHIFT		8
#define  SEC_A_BLD_FUNC_SHIFT		12

enum db_blend_func {
	DB_BLEND_ZERO,
	DB_BLEND_ONE,
	DB_BLEND_PRIM_ALPHA,
	DB_BLEND_ONE_MINUS_PRIM_ALPHA,
	DB_BLEND_SEC_ALPHA,
	DB_BLEND_ONE_MINUS_SEC_ALPHA,
	DB_BLEND_CONST_ALPHA,
	DB_BLEND_ONE_MINUS_CONST_ALPHA,
};

enum dc_imx95_db_mode {
	DB_MODE_PRIMARY,
	DB_MODE_SECONDARY,
	DB_MODE_BLEND,
	DB_MODE_SIDEBYSIDE,
};

struct dc_imx95_db {
	void __iomem *base;
	struct device *dev;
	int irq_shdload;
};

struct device *dc_imx95_db_dev(struct dc_imx95_db *db)
{
	return db->dev;
}
EXPORT_SYMBOL_GPL(dc_imx95_db_dev);

int dc_imx95_db_shdload_irq(struct dc_imx95_db *db)
{
	return db->irq_shdload;
}

static const struct dc_subdev_info dc_imx95_db_info[] = {
	{ .reg_start = 0x4b6a0000, .id = 0, },
	{ .reg_start = 0x4b720000, .id = 1, },
};

static inline void db_write(struct dc_imx95_db *db, unsigned int offset, u32 val)
{
	writel(val, db->base + offset);
}

static inline void db_rmw(struct dc_imx95_db *db, unsigned int offset,
			  u32 mask, u32 val)
{
	u32 tmp = readl(db->base + offset);

	writel((tmp & ~mask) | val, db->base + offset);
}

void dc_imx95_db_shdtokgen(struct dc_imx95_db *db)
{
	if (!db)
		return;
	db_write(db, CONTROLTRIGGER, SHDTOKGEN);
}
EXPORT_SYMBOL_GPL(dc_imx95_db_shdtokgen);

void dc_imx95_db_mode_primary(struct dc_imx95_db *db)
{
	if (!db)
		return;
	db_write(db, MODECONTROL, DB_MODE_PRIMARY);
}

void dc_imx95_db_mode_blend(struct dc_imx95_db *db)
{
	if (!db)
		return;
	db_write(db, MODECONTROL, DB_MODE_BLEND);
}

void dc_imx95_db_init(struct dc_imx95_db *db)
{
	db_rmw(db, STATICCONTROL, SHDEN, SHDEN);
	db_rmw(db, STATICCONTROL, SHDTOKSEL_MASK, SHDTOKSEL_SW);
	db_rmw(db, STATICCONTROL, SHDLDSEL_MASK, SHDLDSEL_SW);
	db_write(db, MODECONTROL, DB_MODE_PRIMARY);
	db_rmw(db, ALPHACONTROL, ALPHAMASKENABLE, 0);
	db_write(db, BLENDCONTROL,
		 (DB_BLEND_ZERO << SEC_A_BLD_FUNC_SHIFT) |
		 (DB_BLEND_ZERO << PRIM_A_BLD_FUNC_SHIFT) |
		 (DB_BLEND_ONE  << SEC_C_BLD_FUNC_SHIFT)  |
		 (DB_BLEND_ZERO << PRIM_C_BLD_FUNC_SHIFT));
}
EXPORT_SYMBOL_GPL(dc_imx95_db_init);

static int dc_imx95_db_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dc_drm_device *dc_drm = data;
	struct resource *res;
	struct dc_imx95_db *db;
	int id;

	db = devm_kzalloc(dev, sizeof(*db), GFP_KERNEL);
	if (!db)
		return -ENOMEM;

	db->dev = dev;

	db->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(db->base))
		return PTR_ERR(db->base);

	/* DomainBlend shadow-load interrupt (via the DC interrupt controller). */
	db->irq_shdload = platform_get_irq_byname_optional(pdev, "shdload");
	if (db->irq_shdload == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (db->irq_shdload < 0)
		db->irq_shdload = 0;

	id = dc_subdev_get_id(dc_imx95_db_info,
			      ARRAY_SIZE(dc_imx95_db_info), res);
	if (id < 0) {
		dev_err(dev, "failed to get instance number: %d\n", id);
		return id;
	}

	dc_imx95_db_init(db);

	if (dc_drm->imx95 && id < 2)
		dc_drm->imx95->db[id] = db;

	return 0;
}

static const struct component_ops dc_imx95_db_ops = {
	.bind = dc_imx95_db_bind,
};

static int dc_imx95_db_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dc_imx95_db_ops);
}

static void dc_imx95_db_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dc_imx95_db_ops);
}

static const struct of_device_id dc_imx95_db_dt_ids[] = {
	{ .compatible = "fsl,imx95-dc-domainblend" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_imx95_db_dt_ids);

/* ---- Display Master (DM) region at DC base + 0x2000 ---- */
void dc_imx95_dm_init(void __iomem *dm_regs)
{
	if (!dm_regs)
		return;

	writel(DM_ALLOW_ALL, dm_regs + DM_EXTDST0_DM);
	writel(DM_ALLOW_ALL, dm_regs + DM_EXTDST4_DM);
	writel(DM_ALLOW_ALL, dm_regs + DM_EXTDST1_DM);
	writel(DM_ALLOW_ALL, dm_regs + DM_EXTDST5_DM);

	writel(DM_MASTER, dm_regs + DM_EXTDST0_STATIC);
	writel(DM_MASTER, dm_regs + DM_EXTDST4_STATIC);
	writel(DM_MASTER, dm_regs + DM_EXTDST1_STATIC);
	writel(DM_MASTER, dm_regs + DM_EXTDST5_STATIC);
}
EXPORT_SYMBOL_GPL(dc_imx95_dm_init);

void dc_imx95_dm_disable_pipeline_sync(void __iomem *dm_regs)
{
	if (dm_regs)
		writel(0, dm_regs + DM_DISPLAY_STATIC);
}
EXPORT_SYMBOL_GPL(dc_imx95_dm_disable_pipeline_sync);

void dc_imx95_dm_enable_pipeline(void __iomem *dm_regs, unsigned int stream_id)
{
	if (dm_regs)
		writel(BIT(stream_id), dm_regs + DM_DISPLAY_STATIC);
}

struct platform_driver dc_imx95_db_driver = {
	.probe  = dc_imx95_db_probe,
	.remove = dc_imx95_db_remove,
	.driver = {
		.name = "imx95-dc-domainblend",
		.suppress_bind_attrs = true,
		.of_match_table = dc_imx95_db_dt_ids,
	},
};
