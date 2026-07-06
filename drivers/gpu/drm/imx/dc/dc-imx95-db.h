/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 *
 * i.MX95 DomainBlend block API.
 */
#ifndef __DC_IMX95_DB_H__
#define __DC_IMX95_DB_H__

struct device;
struct dc_imx95_db;

struct device *dc_imx95_db_dev(struct dc_imx95_db *db);
int dc_imx95_db_shdload_irq(struct dc_imx95_db *db);

void dc_imx95_db_init(struct dc_imx95_db *db);
void dc_imx95_db_shdtokgen(struct dc_imx95_db *db);
void dc_imx95_db_mode_primary(struct dc_imx95_db *db);
void dc_imx95_db_mode_blend(struct dc_imx95_db *db);

void dc_imx95_dm_init(void __iomem *dm_regs);
void dc_imx95_dm_disable_pipeline_sync(void __iomem *dm_regs);
void dc_imx95_dm_enable_pipeline(void __iomem *dm_regs, unsigned int stream_id);

#endif
