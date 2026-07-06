// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 *
 * i.MX95 DC8000 DRM platform driver.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_of.h>

#include "dc-de.h"
#include "dc-drv.h"
#include "dc-imx95-db.h"
#include "dc-pe.h"

/*
 * QoS setting in the display-mix blk-ctrl (dispmix_csr, "nxp,blk-ctrl" phandle).
 * Without raising AXI QoS the display fabric rejects read transactions from
 * Fetch Layer register ports, returning SLVERR on regmap_update_bits() calls.
 * Mirrors dpu95_set_qos() / QOS_SETTING in the downstream dpu95-core.c.
 */
#define DISPMIX_QOS_SETTING		0x1c
#define  DISPMIX_PANIC_QOS_MASK		0x70
#define  DISPMIX_PANIC_QOS(n)		(((n) & 0x7) << 4)
#define  DISPMIX_ARQOS_MASK		0x7
#define  DISPMIX_ARQOS(n)		((n) & 0x7)

struct dc_priv {
	struct drm_device *drm;
	struct clk *clk_cfg;
	struct clk *clk_axi;
	struct clk *clk_ocram;
	struct clk *clk_pix;
	struct clk *clk_ldb;
	struct clk *clk_ldb_vco;
	struct regmap *dispmix_csr;
};

DEFINE_DRM_GEM_DMA_FOPS(dc_drm_driver_fops);

static struct drm_driver dc_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	DRM_GEM_DMA_DRIVER_OPS,
	DRM_FBDEV_DMA_DRIVER_OPS,
	.fops = &dc_drm_driver_fops,
	.name = "imx95-dc",
	.desc = "i.MX95 DC8000 DRM graphics",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
};

static void
dc_add_components(struct device *dev, struct component_match **matchptr)
{
	struct device_node *child, *grandchild;

	for_each_available_child_of_node(dev->of_node, child) {
		if (!of_find_property(child, "compatible", NULL))
			continue;

		drm_of_component_match_add(dev, matchptr, component_compare_of,
					   child);

		for_each_available_child_of_node(child, grandchild)
			drm_of_component_match_add(dev, matchptr,
						   component_compare_of,
						   grandchild);
	}
}

static int dc_drm_component_bind_all(struct dc_drm_device *dc_drm)
{
	struct drm_device *drm = &dc_drm->base;
	int ret;

	ret = component_bind_all(drm->dev, dc_drm);
	if (ret)
		return ret;

	dc_de_post_bind(dc_drm);

	/* i.MX95 has no pixel-engine component; create a virtual one */
	if (!dc_drm->pe) {
		dc_drm->pe = devm_kzalloc(drm->dev, sizeof(*dc_drm->pe),
					  GFP_KERNEL);
		if (!dc_drm->pe)
			return -ENOMEM;
	}
	dc_pe_post_bind(dc_drm);

	return 0;
}

static void dc_drm_component_unbind_all(void *ptr)
{
	struct dc_drm_device *dc_drm = ptr;
	struct drm_device *drm = &dc_drm->base;

	component_unbind_all(drm->dev, dc_drm);
}

static int dc_drm_bind(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);
	struct platform_device *pdev = to_platform_device(dev);
	struct dc_drm_device *dc_drm;
	struct drm_device *drm;
	struct drm_crtc *crtc;
	struct resource *dc_res;
	int ret;

	dc_drm = devm_drm_dev_alloc(dev, &dc_drm_driver, struct dc_drm_device,
				    base);
	if (IS_ERR(dc_drm))
		return PTR_ERR(dc_drm);

	drm = &dc_drm->base;
	dc_drm->soc = DC_SOC_IMX95;
	dc_drm->imx95 = devm_kzalloc(dev, sizeof(*dc_drm->imx95), GFP_KERNEL);
	if (!dc_drm->imx95)
		return -ENOMEM;
	dc_drm->clk_pix = priv->clk_pix;
	dc_drm->clk_ldb = priv->clk_ldb;
	dc_drm->clk_ldb_vco = priv->clk_ldb_vco;
	dc_drm->dispmix_csr = priv->dispmix_csr;

	dc_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (dc_res) {
		dc_drm->imx95->dm_regs =
			devm_ioremap(dev, dc_res->start + 0x2000, 0x30);
		if (!dc_drm->imx95->dm_regs)
			dev_warn(dev, "failed to map DM regs\n");
	}
	dc_imx95_dm_init(dc_drm->imx95->dm_regs);

	ret = dc_drm_component_bind_all(dc_drm);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, dc_drm_component_unbind_all,
				       dc_drm);
	if (ret)
		return ret;

	ret = dc_kms_init(dc_drm);
	if (ret)
		return ret;

	drm_for_each_crtc(crtc, drm)
		dc_crtc_disable_at_boot(crtc);

	ret = drm_dev_register(drm, 0);
	if (ret) {
		dev_err(dev, "failed to register drm device: %d\n", ret);
		goto err;
	}

	drm_client_setup_with_fourcc(drm, DRM_FORMAT_XRGB8888);

	priv->drm = drm;

	return 0;

err:
	dc_kms_uninit(dc_drm);

	return ret;
}

static void dc_drm_unbind(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);
	struct dc_drm_device *dc_drm = to_dc_drm_device(priv->drm);
	struct drm_device *drm = &dc_drm->base;

	priv->drm = NULL;
	drm_dev_unplug(drm);
	dc_kms_uninit(dc_drm);
	drm_atomic_helper_shutdown(drm);
}

static const struct component_master_ops dc_drm_ops = {
	.bind = dc_drm_bind,
	.unbind = dc_drm_unbind,
};

static int dc_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct dc_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk_cfg = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk_cfg))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->clk_cfg),
				     "failed to get cfg clock\n");

	priv->clk_axi = devm_clk_get_optional(&pdev->dev, "axi");
	if (IS_ERR(priv->clk_axi))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->clk_axi),
				     "failed to get axi clock\n");

	priv->clk_ocram = devm_clk_get_optional(&pdev->dev, "ocram");
	if (IS_ERR(priv->clk_ocram))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->clk_ocram),
				     "failed to get ocram clock\n");

	priv->clk_pix = devm_clk_get(&pdev->dev, "pix");
	if (IS_ERR(priv->clk_pix))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->clk_pix),
				     "failed to get pix clock\n");

	priv->clk_ldb = devm_clk_get(&pdev->dev, "ldb");
	if (IS_ERR(priv->clk_ldb))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->clk_ldb),
				     "failed to get ldb clock\n");

	priv->clk_ldb_vco = devm_clk_get(&pdev->dev, "ldb_vco");
	if (IS_ERR(priv->clk_ldb_vco))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->clk_ldb_vco),
				     "failed to get ldb_vco clock\n");

	/*
	 * Look up the display-mix blk-ctrl syscon for QoS programming
	 * (nxp,blk-ctrl phandle in DT).
	 */
	priv->dispmix_csr = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							     "nxp,blk-ctrl");
	if (IS_ERR(priv->dispmix_csr)) {
		int err = PTR_ERR(priv->dispmix_csr);

		if (err != -ENODEV)
			return dev_err_probe(&pdev->dev, err,
					     "failed to get blk-ctrl regmap\n");
		priv->dispmix_csr = NULL;
	}

	dev_set_drvdata(&pdev->dev, priv);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	/*
	 * Take a permanent PM reference so that genpd keeps IMX95_PD_DISPLAY
	 * powered on after sync_state() fires.  Without this, genpd sees
	 * active_count=0 after the power domain's sync_state callback runs and
	 * immediately powers off the display domain, making all sub-block
	 * register accesses fault.  The reference is released in dc_remove.
	 */
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "failed to resume DC\n");

	ret = devm_of_platform_populate(&pdev->dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to populate children\n");

	dc_add_components(&pdev->dev, &match);

	ret = component_master_add_with_match(&pdev->dev, &dc_drm_ops, match);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to add component master\n");

	return 0;
}

static void dc_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &dc_drm_ops);
	pm_runtime_put_noidle(&pdev->dev);
}

static int dc_runtime_suspend(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->clk_ocram);
	clk_disable_unprepare(priv->clk_axi);
	clk_disable_unprepare(priv->clk_cfg);

	return 0;
}

static int dc_runtime_resume(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(priv->clk_cfg);
	if (ret) {
		dev_err(dev, "failed to enable cfg clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->clk_axi);
	if (ret) {
		dev_err(dev, "failed to enable axi clock: %d\n", ret);
		goto err_axi;
	}

	ret = clk_prepare_enable(priv->clk_ocram);
	if (ret) {
		dev_err(dev, "failed to enable ocram clock: %d\n", ret);
		goto err_ocram;
	}

	/*
	 * Program AXI QoS in the display-mix blk-ctrl.  After a power-domain
	 * cycle this register resets to 0; with zero QoS the display AXI
	 * fabric rejects Fetch Layer read transactions with SLVERR, causing a
	 * synchronous external abort in regmap_update_bits().
	 * Mirrors dpu95_set_qos() from the downstream driver (dpu95-core.c).
	 */
	if (priv->dispmix_csr) {
		ret = regmap_update_bits(priv->dispmix_csr, DISPMIX_QOS_SETTING,
					 DISPMIX_PANIC_QOS_MASK | DISPMIX_ARQOS_MASK,
					 DISPMIX_PANIC_QOS(3) | DISPMIX_ARQOS(3));
		if (ret) {
			dev_err(dev, "failed to set display QoS: %d\n", ret);
			goto err_qos;
		}
	}

	return 0;

err_qos:
	clk_disable_unprepare(priv->clk_ocram);
err_ocram:
	clk_disable_unprepare(priv->clk_axi);
err_axi:
	clk_disable_unprepare(priv->clk_cfg);
	return ret;
}

static int dc_suspend(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(priv->drm);
}

static int dc_resume(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(priv->drm);
}

static void dc_shutdown(struct platform_device *pdev)
{
	struct dc_priv *priv = dev_get_drvdata(&pdev->dev);

	drm_atomic_helper_shutdown(priv->drm);
}

static const struct dev_pm_ops dc_pm_ops = {
	RUNTIME_PM_OPS(dc_runtime_suspend, dc_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(dc_suspend, dc_resume)
};

static const struct of_device_id dc_imx95_dt_ids[] = {
	{ .compatible = "fsl,imx95-dc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_imx95_dt_ids);

static struct platform_driver dc_imx95_driver = {
	.probe		= dc_probe,
	.remove		= dc_remove,
	.shutdown	= dc_shutdown,
	.driver = {
		.name		= "imx95-dc",
		.of_match_table	= dc_imx95_dt_ids,
		.pm		= pm_sleep_ptr(&dc_pm_ops),
	},
};
module_platform_driver(dc_imx95_driver);

MODULE_DESCRIPTION("i.MX95 DC8000 DRM Driver");
MODULE_AUTHOR("Liu Ying <victor.liu@nxp.com>");
MODULE_LICENSE("GPL");
