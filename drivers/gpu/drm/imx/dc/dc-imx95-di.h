/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 *
 * i.MX95 Dither block API.
 */
#ifndef __DC_IMX95_DI_H__
#define __DC_IMX95_DI_H__

struct device;
struct dc_imx95_di;

struct device *dc_imx95_di_dev(struct dc_imx95_di *di);
int dc_imx95_di_framecomplete_irq(struct dc_imx95_di *di);
int dc_imx95_di_shdload_irq(struct dc_imx95_di *di);
int dc_imx95_di_seqcomplete_irq(struct dc_imx95_di *di);

void dc_imx95_di_polhs_active_high(struct dc_imx95_di *di);
void dc_imx95_di_polhs_active_low(struct dc_imx95_di *di);
void dc_imx95_di_polvs_active_high(struct dc_imx95_di *di);
void dc_imx95_di_polvs_active_low(struct dc_imx95_di *di);
void dc_imx95_di_polen_active_high(struct dc_imx95_di *di);
void dc_imx95_di_polen_active_low(struct dc_imx95_di *di);

#endif
