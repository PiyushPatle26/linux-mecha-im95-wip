// SPDX-License-Identifier: GPL-2.0+
/*
 * NEOISP context registers/memory setting helpers
 *
 * Copyright 2023-2026 NXP
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/media/nxp/nxp_neoisp.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-isp.h>
#include <media/videobuf2-dma-contig.h>

#include "neoisp.h"
#include "neoisp_ctx.h"

#define FIELD_PREP_S16_CONST(_mask, _val) \
	FIELD_PREP_CONST(_mask, (_val) & GENMASK(15, 0))
/*
 * This is the initial set of parameters setup by driver upon a streamon ioctl for INPUT0 node.
 * It could be updated later by the driver depending on input/output formats setup by userspace
 * and also if fine tuned parameters are provided by the camera stack.
 */
static const struct neoisp_context_s def_context = {
	.hw = {
		.pipe_conf = {
			.img_conf =
				FIELD_PREP_CONST(NEO_PIPE_CONF_IMG_CONF_CAM0_INALIGN0, 1) |
				FIELD_PREP_CONST(NEO_PIPE_CONF_IMG_CONF_CAM0_LPALIGN0, 1) |
				FIELD_PREP_CONST(NEO_PIPE_CONF_IMG_CONF_CAM0_INALIGN1, 1) |
				FIELD_PREP_CONST(NEO_PIPE_CONF_IMG_CONF_CAM0_LPALIGN1, 1),
		},
		.hdr_decompress0 = {
			.ctrl =
				FIELD_PREP_CONST(NEO_HDR_DECOMPRESS0_CTRL_CAM0_ENABLE, 1),
			.knee_ratio4 =
				FIELD_PREP_CONST(NEO_HDR_DECOMPRESS0_KNEE_RATIO4_CAM0_RATIO4,
						 1 << 5),
		},
		.hdr_decompress1 = {
			.ctrl =
				FIELD_PREP_CONST(NEO_HDR_DECOMPRESS1_CTRL_CAM0_ENABLE, 0),
			.knee_ratio4 =
				FIELD_PREP_CONST(NEO_HDR_DECOMPRESS1_KNEE_RATIO4_CAM0_RATIO4,
						 1 << 5),
		},
		.obwb0 = {
			.ctrl =
				FIELD_PREP_CONST(NEO_OB_WB0_CTRL_CAM0_OBPP, 3),
			.r_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB0_R_CTRL_CAM0_GAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_OB_WB0_R_CTRL_CAM0_OFFSET, 0),
			.gr_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB0_GR_CTRL_CAM0_GAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_OB_WB0_GR_CTRL_CAM0_OFFSET, 0),
			.gb_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB0_GB_CTRL_CAM0_GAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_OB_WB0_GB_CTRL_CAM0_OFFSET, 0),
			.b_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB0_B_CTRL_CAM0_GAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_OB_WB0_B_CTRL_CAM0_OFFSET, 0),
		},
		.obwb1 = {
			.ctrl =
				FIELD_PREP_CONST(NEO_OB_WB1_CTRL_CAM0_OBPP, 2),
			.r_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB1_R_CTRL_CAM0_GAIN, 1 << 8)	|
				FIELD_PREP_CONST(NEO_OB_WB1_R_CTRL_CAM0_OFFSET, 0),
			.gr_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB1_GR_CTRL_CAM0_GAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_OB_WB1_GR_CTRL_CAM0_OFFSET, 0),
			.gb_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB1_GB_CTRL_CAM0_GAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_OB_WB1_GB_CTRL_CAM0_OFFSET, 0),
			.b_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB1_B_CTRL_CAM0_GAIN, 1 << 8)	|
				FIELD_PREP_CONST(NEO_OB_WB1_B_CTRL_CAM0_OFFSET, 0),
		},
		.obwb2 = {
			.ctrl =
				FIELD_PREP_CONST(NEO_OB_WB2_CTRL_CAM0_OBPP, 3),
			.r_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB2_R_CTRL_CAM0_GAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_OB_WB2_R_CTRL_CAM0_OFFSET, 0),
			.gr_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB2_GR_CTRL_CAM0_GAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_OB_WB2_GR_CTRL_CAM0_OFFSET, 0),
			.gb_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB2_GB_CTRL_CAM0_GAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_OB_WB2_GB_CTRL_CAM0_OFFSET, 0),
			.b_ctrl =
				FIELD_PREP_CONST(NEO_OB_WB2_B_CTRL_CAM0_GAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_OB_WB2_B_CTRL_CAM0_OFFSET, 0),
		},
		.hdr_merge = {
			.ctrl =
				FIELD_PREP_CONST(NEO_HDR_MERGE_CTRL_CAM0_ENABLE, 0) |
				FIELD_PREP_CONST(NEO_HDR_MERGE_CTRL_CAM0_GAIN1BPP, 3) |
				FIELD_PREP_CONST(NEO_HDR_MERGE_CTRL_CAM0_GAIN0BPP, 3) |
				FIELD_PREP_CONST(NEO_HDR_MERGE_CTRL_CAM0_OBPP, 3),
			.gain_scale =
				FIELD_PREP_CONST(NEO_HDR_MERGE_GAIN_SCALE_CAM0_SCALE1, 8) |
				FIELD_PREP_CONST(NEO_HDR_MERGE_GAIN_SCALE_CAM0_SCALE0, 1 << 12),
			.gain_shift =
				FIELD_PREP_CONST(NEO_HDR_MERGE_GAIN_SHIFT_CAM0_SHIFT1, 12) |
				FIELD_PREP_CONST(NEO_HDR_MERGE_GAIN_SHIFT_CAM0_SHIFT0, 4),
			.luma_th =
				FIELD_PREP_CONST(NEO_HDR_MERGE_LUMA_TH_CAM0_TH0, 4),
			.luma_scale =
				FIELD_PREP_CONST(NEO_HDR_MERGE_LUMA_SCALE_CAM0_SCALE, 1 << 8) |
				FIELD_PREP_CONST(NEO_HDR_MERGE_LUMA_SCALE_CAM0_SHIFT, 8) |
				FIELD_PREP_CONST(NEO_HDR_MERGE_LUMA_SCALE_CAM0_THSHIFT, 8),
			.downscale =
				FIELD_PREP_CONST(NEO_HDR_MERGE_DOWNSCALE_CAM0_IMGSCALE0, 8),
			.upscale =
				FIELD_PREP_CONST(NEO_HDR_MERGE_UPSCALE_CAM0_IMGSCALE1, 8),
		},
		.ctemp = {},
		.rgbir = {
			.ctrl =
				FIELD_PREP_CONST(NEO_RGBIR_CTRL_CAM0_ENABLE, 0),
			.ccm0 =
				FIELD_PREP_CONST(NEO_RGBIR_CCM0_CAM0_CCM, 1 << 8),
			.ccm1 =
				FIELD_PREP_CONST(NEO_RGBIR_CCM1_CAM0_CCM, 1 << 8),
			.ccm2 =
				FIELD_PREP_CONST(NEO_RGBIR_CCM2_CAM0_CCM, 1 << 8),
			.ccm0_th =
				FIELD_PREP_CONST(NEO_RGBIR_CCM0_TH_CAM0_THRESHOLD, 0xff000),
			.ccm1_th =
				FIELD_PREP_CONST(NEO_RGBIR_CCM1_TH_CAM0_THRESHOLD, 0xff000),
			.ccm2_th =
				FIELD_PREP_CONST(NEO_RGBIR_CCM2_TH_CAM0_THRESHOLD, 0xff000),
		},
		.stat = {},
		.ir_compress = {
			.ctrl =
				FIELD_PREP_CONST(NEO_IR_COMPRESS_CTRL_CAM0_ENABLE, 0) |
				FIELD_PREP_CONST(NEO_IR_COMPRESS_CTRL_CAM0_OBPP, 0),
			.knee_point1 =
				FIELD_PREP_CONST(NEO_IR_COMPRESS_KNEE_POINT1_CAM0_KNEEPOINT,
						 (1 << 20) - 1),
			.knee_ratio01 =
				FIELD_PREP_CONST(NEO_IR_COMPRESS_KNEE_RATIO01_CAM0_RATIO0, 8),
			.knee_ratio4 =
				FIELD_PREP_CONST(NEO_IR_COMPRESS_KNEE_RATIO4_CAM0_RATIO4, 8),
		},
		.bnr = {
			.ctrl =
				FIELD_PREP_CONST(NEO_BNR_CTRL_CAM0_ENABLE, 1) |
				FIELD_PREP_CONST(NEO_BNR_CTRL_CAM0_NHOOD, 0) |
				FIELD_PREP_CONST(NEO_BNR_CTRL_CAM0_DEBUG, 0) |
				FIELD_PREP_CONST(NEO_BNR_CTRL_CAM0_OBPP, 3),
			.ypeak =
				FIELD_PREP_CONST(NEO_BNR_YPEAK_CAM0_PEAK_OUTSEL, 0) |
				FIELD_PREP_CONST(NEO_BNR_YPEAK_CAM0_PEAK_HIGH, 1 << 8) |
				FIELD_PREP_CONST(NEO_BNR_YPEAK_CAM0_PEAK_SEL, 0) |
				FIELD_PREP_CONST(NEO_BNR_YPEAK_CAM0_PEAK_LOW, 1 << 7),
			.yedge_th0 =
				FIELD_PREP_CONST(NEO_BNR_YEDGE_TH0_CAM0_EDGE_TH0, 20),
			.yedge_scale =
				FIELD_PREP_CONST(NEO_BNR_YEDGE_SCALE_CAM0_SHIFT, 10) |
				FIELD_PREP_CONST(NEO_BNR_YEDGE_SCALE_CAM0_SCALE, 1 << 10),
			.yedges_th0 =
				FIELD_PREP_CONST(NEO_BNR_YEDGES_TH0_CAM0_EDGE_TH0, 20),
			.yedges_scale =
				FIELD_PREP_CONST(NEO_BNR_YEDGES_SCALE_CAM0_SHIFT, 10) |
				FIELD_PREP_CONST(NEO_BNR_YEDGES_SCALE_CAM0_SCALE, 1 << 10),
			.yedgea_th0 =
				FIELD_PREP_CONST(NEO_BNR_YEDGEA_TH0_CAM0_EDGE_TH0, 20),
			.yedgea_scale =
				FIELD_PREP_CONST(NEO_BNR_YEDGEA_SCALE_CAM0_SHIFT, 10) |
				FIELD_PREP_CONST(NEO_BNR_YEDGEA_SCALE_CAM0_SCALE, 10),
			.yluma_x_th0 =
				FIELD_PREP_CONST(NEO_BNR_YLUMA_X_TH0_CAM0_TH, 20),
			.yluma_y_th =
				FIELD_PREP_CONST(NEO_BNR_YLUMA_Y_TH_CAM0_LUMA_Y_TH1, 1 << 8) |
				FIELD_PREP_CONST(NEO_BNR_YLUMA_Y_TH_CAM0_LUMA_Y_TH0, 10),
			.yluma_scale =
				FIELD_PREP_CONST(NEO_BNR_YLUMA_SCALE_CAM0_SHIFT, 10) |
				FIELD_PREP_CONST(NEO_BNR_YLUMA_SCALE_CAM0_SCALE, 1 << 10),
			.yalpha_gain =
				FIELD_PREP_CONST(NEO_BNR_YALPHA_GAIN_CAM0_OFFSET, 0) |
				FIELD_PREP_CONST(NEO_BNR_YALPHA_GAIN_CAM0_GAIN, 1 << 8),
			.cpeak =
				FIELD_PREP_CONST(NEO_BNR_CPEAK_CAM0_PEAK_OUTSEL, 0) |
				FIELD_PREP_CONST(NEO_BNR_CPEAK_CAM0_PEAK_HIGH, 1 << 8) |
				FIELD_PREP_CONST(NEO_BNR_CPEAK_CAM0_PEAK_SEL, 0) |
				FIELD_PREP_CONST(NEO_BNR_CPEAK_CAM0_PEAK_LOW, 1 << 7),
			.cedge_th0 =
				FIELD_PREP_CONST(NEO_BNR_CEDGE_TH0_CAM0_EDGE_TH0, 20),
			.cedge_scale =
				FIELD_PREP_CONST(NEO_BNR_CEDGE_SCALE_CAM0_SHIFT, 10) |
				FIELD_PREP_CONST(NEO_BNR_CEDGE_SCALE_CAM0_SCALE, 1 << 10),
			.cedges_th0 =
				FIELD_PREP_CONST(NEO_BNR_CEDGES_TH0_CAM0_EDGE_TH0, 20),
			.cedges_scale =
				FIELD_PREP_CONST(NEO_BNR_CEDGES_SCALE_CAM0_SHIFT, 10) |
				FIELD_PREP_CONST(NEO_BNR_CEDGES_SCALE_CAM0_SCALE, 1 << 10),
			.cedgea_th0 =
				FIELD_PREP_CONST(NEO_BNR_CEDGEA_TH0_CAM0_EDGE_TH0, 20),
			.cedgea_scale =
				FIELD_PREP_CONST(NEO_BNR_CEDGEA_SCALE_CAM0_SHIFT, 10) |
				FIELD_PREP_CONST(NEO_BNR_CEDGEA_SCALE_CAM0_SCALE, 1 << 10),
			.cluma_x_th0 =
				FIELD_PREP_CONST(NEO_BNR_CLUMA_X_TH0_CAM0_TH, 20),
			.cluma_y_th =
				FIELD_PREP_CONST(NEO_BNR_CLUMA_Y_TH_CAM0_LUMA_Y_TH1, 1 << 8) |
				FIELD_PREP_CONST(NEO_BNR_CLUMA_Y_TH_CAM0_LUMA_Y_TH0, 10),
			.cluma_scale =
				FIELD_PREP_CONST(NEO_BNR_CLUMA_SCALE_CAM0_SHIFT, 10) |
				FIELD_PREP_CONST(NEO_BNR_CLUMA_SCALE_CAM0_SCALE, 1 << 10),
			.calpha_gain =
				FIELD_PREP_CONST(NEO_BNR_CALPHA_GAIN_CAM0_OFFSET, 0) |
				FIELD_PREP_CONST(NEO_BNR_CALPHA_GAIN_CAM0_GAIN, 1 << 8),
			.stretch =
				FIELD_PREP_CONST(NEO_BNR_STRETCH_CAM0_GAIN, 1 << 8),
		},
		.idbg1 = {
			.line_num_t =
				NEO_IDBG1_LINE_NUM_LINE_NUM,
		},
		.demosaic = {
			.ctrl =
				FIELD_PREP_CONST(NEO_DEMOSAIC_CTRL_CAM0_FMT, 0),
			.activity_ctl =
				FIELD_PREP_CONST(NEO_DEMOSAIC_ACTIVITY_CTL_CAM0_ACT_RATIO, 1 << 8) |
				FIELD_PREP_CONST(NEO_DEMOSAIC_ACTIVITY_CTL_CAM0_ALPHA, 1 << 8),
			.dynamics_ctl0 =
				FIELD_PREP_CONST(NEO_DEMOSAIC_DYNAMICS_CTL0_CAM0_STRENGTHC,
						 1 << 8) |
				FIELD_PREP_CONST(NEO_DEMOSAIC_DYNAMICS_CTL0_CAM0_STRENGTHG,
						 1 << 8),
			.dynamics_ctl2 =
				FIELD_PREP_CONST(NEO_DEMOSAIC_DYNAMICS_CTL2_CAM0_MAX_IMPACT,
						 1 << 7),
		},
		.rgb2yuv = {
			.gain_ctrl =
				FIELD_PREP_CONST(NEO_RGB_TO_YUV_GAIN_CTRL_CAM0_BGAIN, 1 << 8) |
				FIELD_PREP_CONST(NEO_RGB_TO_YUV_GAIN_CTRL_CAM0_RGAIN, 1 << 8),
			/* Constants defined by V4L2_YCBCR_ENC_601, full range and
			 * formatted in s8.8. This matrix will define the gcm.imat_rxcy
			 * as its inverse.
			 * https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/colorspaces-details.html
			 *	{77, 150,  29},
			 *	{-43, -85, 128},
			 *	{128, -107, -21},
			 */
			.mat0 =
				FIELD_PREP_CONST(NEO_RGB_TO_YUV_MAT0_CAM0_R0C0, 77) |
				FIELD_PREP_CONST(NEO_RGB_TO_YUV_MAT0_CAM0_R0C1, 150),
			.mat1 =
				FIELD_PREP_CONST(NEO_RGB_TO_YUV_MAT1_CAM0_R0C2, 29),
			.mat2 =
				FIELD_PREP_S16_CONST(NEO_RGB_TO_YUV_MAT2_CAM0_R1C0, -43) |
				FIELD_PREP_S16_CONST(NEO_RGB_TO_YUV_MAT2_CAM0_R1C1, -85),
			.mat3 =
				FIELD_PREP_CONST(NEO_RGB_TO_YUV_MAT3_CAM0_R1C2, 128),
			.mat4 =
				FIELD_PREP_CONST(NEO_RGB_TO_YUV_MAT4_CAM0_R2C0, 128) |
				FIELD_PREP_S16_CONST(NEO_RGB_TO_YUV_MAT4_CAM0_R2C1, -107),
			.mat5 =
				FIELD_PREP_S16_CONST(NEO_RGB_TO_YUV_MAT5_CAM0_R2C2, -21),
		},
		.drc = {
			.gbl_gain =
				FIELD_PREP_CONST(NEO_DRC_GBL_GAIN_CAM0_GAIN, 1 << 8),
			.lcl_stretch =
				FIELD_PREP_CONST(NEO_DRC_LCL_STRETCH_CAM0_STRETCH, 1 << 8),
			.alpha =
				FIELD_PREP_CONST(NEO_DRC_ALPHA_CAM0_ALPHA, 1 << 8),
		},
		.cas = {
			.gain =
				FIELD_PREP_CONST(NEO_CAS_GAIN_CAM0_SCALE, 1),
		},
		.packetizer = {
			.ch0_ctrl =
				FIELD_PREP_CONST(NEO_PACKETIZER_CH0_CTRL_CAM0_OBPP, 6) |
				FIELD_PREP_CONST(NEO_PACKETIZER_CH0_CTRL_CAM0_RSA, 4) |
				FIELD_PREP_CONST(NEO_PACKETIZER_CH0_CTRL_CAM0_LSA, 0),
			.ch12_ctrl =
				FIELD_PREP_CONST(NEO_PACKETIZER_CH12_CTRL_CAM0_OBPP, 6) |
				FIELD_PREP_CONST(NEO_PACKETIZER_CH12_CTRL_CAM0_RSA, 4) |
				FIELD_PREP_CONST(NEO_PACKETIZER_CH12_CTRL_CAM0_LSA, 0) |
				FIELD_PREP_CONST(NEO_PACKETIZER_CH12_CTRL_CAM0_SUBSAMPLE, 0),
			.pack_ctrl =
				FIELD_PREP_CONST(NEO_PACKETIZER_PACK_CTRL_CAM0_TYPE, 1) |
				FIELD_PREP_CONST(NEO_PACKETIZER_PACK_CTRL_CAM0_ORDER0, 0) |
				FIELD_PREP_CONST(NEO_PACKETIZER_PACK_CTRL_CAM0_ORDER1, 1) |
				FIELD_PREP_CONST(NEO_PACKETIZER_PACK_CTRL_CAM0_ORDER2, 2) |
				FIELD_PREP_CONST(NEO_PACKETIZER_PACK_CTRL_CAM0_A0S, 0),
		},
		.gcm = {
			.imat0 =
				FIELD_PREP_CONST(NEO_GCM_IMAT0_CAM0_R0C0, 256) |
				FIELD_PREP_CONST(NEO_GCM_IMAT0_CAM0_R0C1, 0),
			.imat1 =
				FIELD_PREP_CONST(NEO_GCM_IMAT1_CAM0_R0C2, 359),
			.imat2 =
				FIELD_PREP_CONST(NEO_GCM_IMAT2_CAM0_R1C0, 256) |
				FIELD_PREP_S16_CONST(NEO_GCM_IMAT2_CAM0_R1C1, -88),
			.imat3 =
				FIELD_PREP_S16_CONST(NEO_GCM_IMAT3_CAM0_R1C2, -183),
			.imat4 =
				FIELD_PREP_CONST(NEO_GCM_IMAT4_CAM0_R2C0, 256) |
				FIELD_PREP_CONST(NEO_GCM_IMAT4_CAM0_R2C1, 454),
			.imat5 =
				FIELD_PREP_CONST(NEO_GCM_IMAT5_CAM0_R2C2, 0),
			.omat0 =
				FIELD_PREP_CONST(NEO_GCM_OMAT0_CAM0_R0C0, 256),
			.omat2 =
				FIELD_PREP_CONST(NEO_GCM_OMAT2_CAM0_R1C1, 256),
			.omat5 =
				FIELD_PREP_CONST(NEO_GCM_OMAT5_CAM0_R2C2, 256),
			.mat_confg =
				FIELD_PREP_CONST(NEO_GCM_MAT_CONFG_CAM0_SIGN_CONFG, 1),
		},
	},
	.gtm = {
		/* Fill default global tonemap lut with 1.0 value (256) */
		.drc_global_tonemap = { [0 ... NEO_DRC_GLOBAL_TONEMAP_SIZE - 1] = (1 << 8) },
	},
};

union neoisp_params_block_u {
	struct v4l2_isp_block_header header;
	struct neoisp_pipe_conf_cfg_es pipe_conf;
	struct neoisp_head_color_cfg_es head_color;
	struct neoisp_hdr_decompress0_cfg_es hdr_decompress0;
	struct neoisp_hdr_decompress1_cfg_es hdr_decompress1;
	struct neoisp_obwb_cfg_es obwb;
	struct neoisp_hdr_merge_cfg_es hdr_merge;
	struct neoisp_rgbir_cfg_es rgbir;
	struct neoisp_stat_cfg_es stat;
	struct neoisp_ir_compress_cfg_es ir_compress;
	struct neoisp_bnr_cfg_es bnr;
	struct neoisp_vignetting_ctrl_cfg_es vignetting_ctrl;
	struct neoisp_ctemp_cfg_es ctemp;
	struct neoisp_demosaic_cfg_es demosaic;
	struct neoisp_rgb2yuv_cfg_es rgb2yuv;
	struct neoisp_dr_comp_cfg_es dr_comp;
	struct neoisp_nr_cfg_es nr;
	struct neoisp_af_cfg_es af;
	struct neoisp_ee_cfg_es ee;
	struct neoisp_df_cfg_es df;
	struct neoisp_convmed_cfg_es convmed;
	struct neoisp_cas_cfg_es cas;
	struct neoisp_gcm_cfg_es gcm;
	struct neoisp_vignetting_table_mem_params_es vignetting_table;
	struct neoisp_drc_global_tonemap_mem_params_es drc_global_tonemap;
	struct neoisp_drc_local_tonemap_mem_params_es drc_local_tonemap;
};

union neoisp_stats_block_u {
	struct v4l2_isp_block_header header;
	struct neoisp_ctemp_reg_stats_es rctemp;
	struct neoisp_drc_reg_stats_es rdrc;
	struct neoisp_af_reg_stats_es raf;
	struct neoisp_bnr_reg_stats_es rbnr;
	struct neoisp_nr_reg_stats_es rnr;
	struct neoisp_ee_reg_stats_es ree;
	struct neoisp_df_reg_stats_es rdf;
	struct neoisp_ctemp_mem_stats_es mctemp;
	struct neoisp_rgbir_mem_stats_es mrgbir;
	struct neoisp_hist_mem_stats_es mhist;
	struct neoisp_drc_mem_stats_es mdrc;
};

static dma_addr_t get_addr(struct neoisp_buffer_s *buf, u32 num_plane)
{
	if (buf)
		return vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, num_plane);
	return 0;
}

static u32 *get_vaddr(struct neoisp_buffer_s *buf)
{
	if (buf)
		return vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
	return NULL;
}

/*
 * Extract offset and size in bytes from memory region map
 */
static inline void get_offsize(enum isp_block_map_e map, u32 *offset, u32 *size)
{
	*offset = ISP_GET_OFF(map);
	*size = ISP_GET_SZ(map);
}

static inline void
local_mem_blk_write(struct neoisp_dev_s *neoispd, enum isp_block_map_e map, void *src)
{
	u32 offset, count;

	get_offsize(map, &offset, &count);
	memcpy(neoispd->local_mem + offset, src, count);
}

static inline void
local_mem_blk_read(struct neoisp_dev_s *neoispd, enum isp_block_map_e map, void *dst)
{
	u32 offset, count;

	get_offsize(map, &offset, &count);
	memcpy(dst, neoispd->local_mem + offset, count);
}

static inline void
reg_blk_write(struct neoisp_dev_s *neoispd, u32 offset, u32 *src, size_t count)
{
	size_t size = 0;

	while (size < count) {
		neoisp_wr(neoispd, offset + size, *src++);
		size += sizeof(u32);
	}
}

static inline void
reg_blk_read(struct neoisp_dev_s *neoispd, u32 offset, u32 *dst, size_t count)
{
	size_t size = 0;

	while (size < count) {
		*dst++ = neoisp_rd(neoispd, offset + size);
		size += sizeof(u32);
	}
}

/*------------------------------------------------------------------------------
 * Extensible parameters format handling
 */
static void
neoisp_params_handler_pipe_conf(struct neoisp_context_s *ctx,
				const union neoisp_params_block_u *block)
{
	struct neoisp_pipe_conf_s *pc = &ctx->hw.pipe_conf;
	const struct neoisp_pipe_conf_cfg_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->pipe_conf.cfg;

	FIELD_MODIFY(NEO_PIPE_CONF_IMG_CONF_CAM0_INALIGN0,
		     &pc->img_conf, cfg->img_conf_inalign0);
	FIELD_MODIFY(NEO_PIPE_CONF_IMG_CONF_CAM0_LPALIGN0,
		     &pc->img_conf, cfg->img_conf_lpalign0);
	FIELD_MODIFY(NEO_PIPE_CONF_IMG_CONF_CAM0_INALIGN1,
		     &pc->img_conf, cfg->img_conf_inalign1);
	FIELD_MODIFY(NEO_PIPE_CONF_IMG_CONF_CAM0_LPALIGN1,
		     &pc->img_conf, cfg->img_conf_lpalign1);
}

static void
neoisp_params_handler_head_color(struct neoisp_context_s *ctx,
				 const union neoisp_params_block_u *block)
{
	struct neoisp_hc_s *hc = &ctx->hw.hc;
	const struct neoisp_head_color_cfg_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->head_color.cfg;

	hc->ctrl =
		FIELD_PREP(NEO_HC_CTRL_CAM0_HOFFSET, cfg->ctrl_hoffset) |
		FIELD_PREP(NEO_HC_CTRL_CAM0_VOFFSET, cfg->ctrl_voffset);
}

static void
neoisp_params_handler_hdr_decompress0(struct neoisp_context_s *ctx,
				      const union neoisp_params_block_u *block)
{
	struct neoisp_hdr_decompress0_s *hd0 = &ctx->hw.hdr_decompress0;
	const struct neoisp_hdr_decompress0_cfg_s *cfg;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_HDR_DECOMPRESS0_CTRL_CAM0_ENABLE, &hd0->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_HDR_DECOMPRESS0_CTRL_CAM0_ENABLE, &hd0->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->hdr_decompress0.cfg;

	hd0->knee_point1 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_POINT1_CAM0_KNEEPOINT, cfg->knee_point1);
	hd0->knee_point2 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_POINT2_CAM0_KNEEPOINT, cfg->knee_point2);
	hd0->knee_point3 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_POINT3_CAM0_KNEEPOINT, cfg->knee_point3);
	hd0->knee_point4 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_POINT4_CAM0_KNEEPOINT, cfg->knee_point4);
	hd0->knee_offset0 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_OFFSET0_CAM0_OFFSET, cfg->knee_offset0);
	hd0->knee_offset1 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_OFFSET1_CAM0_OFFSET, cfg->knee_offset1);
	hd0->knee_offset2 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_OFFSET2_CAM0_OFFSET, cfg->knee_offset2);
	hd0->knee_offset3 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_OFFSET3_CAM0_OFFSET, cfg->knee_offset3);
	hd0->knee_offset4 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_OFFSET4_CAM0_OFFSET, cfg->knee_offset4);
	hd0->knee_ratio01 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_RATIO01_CAM0_RATIO0, cfg->knee_ratio0) |
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_RATIO01_CAM0_RATIO1, cfg->knee_ratio1);
	hd0->knee_ratio23 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_RATIO23_CAM0_RATIO2, cfg->knee_ratio2) |
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_RATIO23_CAM0_RATIO3, cfg->knee_ratio3);
	hd0->knee_ratio4 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_RATIO4_CAM0_RATIO4, cfg->knee_ratio4);
	hd0->knee_npoint0 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_NPOINT0_CAM0_KNEEPOINT, cfg->knee_npoint0);
	hd0->knee_npoint1 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_NPOINT1_CAM0_KNEEPOINT, cfg->knee_npoint1);
	hd0->knee_npoint2 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_NPOINT2_CAM0_KNEEPOINT, cfg->knee_npoint2);
	hd0->knee_npoint3 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_NPOINT3_CAM0_KNEEPOINT, cfg->knee_npoint3);
	hd0->knee_npoint4 =
		FIELD_PREP(NEO_HDR_DECOMPRESS0_KNEE_NPOINT4_CAM0_KNEEPOINT, cfg->knee_npoint4);
}

static void
neoisp_params_handler_hdr_decompress1(struct neoisp_context_s *ctx,
				      const union neoisp_params_block_u *block)
{
	struct neoisp_hdr_decompress1_s *hd1 = &ctx->hw.hdr_decompress1;
	const struct neoisp_hdr_decompress1_cfg_s *cfg;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_HDR_DECOMPRESS1_CTRL_CAM0_ENABLE, &hd1->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_HDR_DECOMPRESS1_CTRL_CAM0_ENABLE, &hd1->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->hdr_decompress1.cfg;

	hd1->knee_point1 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_POINT1_CAM0_KNEEPOINT, cfg->knee_point1);
	hd1->knee_point2 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_POINT2_CAM0_KNEEPOINT, cfg->knee_point2);
	hd1->knee_point3 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_POINT3_CAM0_KNEEPOINT, cfg->knee_point3);
	hd1->knee_point4 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_POINT4_CAM0_KNEEPOINT, cfg->knee_point4);
	hd1->knee_offset0 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_OFFSET0_CAM0_OFFSET, cfg->knee_offset0);
	hd1->knee_offset1 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_OFFSET1_CAM0_OFFSET, cfg->knee_offset1);
	hd1->knee_offset2 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_OFFSET2_CAM0_OFFSET, cfg->knee_offset2);
	hd1->knee_offset3 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_OFFSET3_CAM0_OFFSET, cfg->knee_offset3);
	hd1->knee_offset4 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_OFFSET4_CAM0_OFFSET, cfg->knee_offset4);
	hd1->knee_ratio01 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_RATIO01_CAM0_RATIO0, cfg->knee_ratio0) |
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_RATIO01_CAM0_RATIO1, cfg->knee_ratio1);
	hd1->knee_ratio23 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_RATIO23_CAM0_RATIO2, cfg->knee_ratio2) |
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_RATIO23_CAM0_RATIO3, cfg->knee_ratio3);
	hd1->knee_ratio4 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_RATIO4_CAM0_RATIO4, cfg->knee_ratio4);
	hd1->knee_npoint0 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_NPOINT0_CAM0_KNEEPOINT, cfg->knee_npoint0);
	hd1->knee_npoint1 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_NPOINT1_CAM0_KNEEPOINT, cfg->knee_npoint1);
	hd1->knee_npoint2 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_NPOINT2_CAM0_KNEEPOINT, cfg->knee_npoint2);
	hd1->knee_npoint3 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_NPOINT3_CAM0_KNEEPOINT, cfg->knee_npoint3);
	hd1->knee_npoint4 =
		FIELD_PREP(NEO_HDR_DECOMPRESS1_KNEE_NPOINT4_CAM0_KNEEPOINT, cfg->knee_npoint4);
}

static void
__neoisp_params_handler_obwb(struct neoisp_context_s *ctx,
			     const union neoisp_params_block_u *block,
			     u8 id)
{
	struct neoisp_obwb_s *obwb;
	const struct neoisp_obwb_cfg_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	switch (id) {
	case 0:
		obwb = &ctx->hw.obwb0;
		break;
	case 1:
		obwb = &ctx->hw.obwb1;
		break;
	case 2:
		obwb = &ctx->hw.obwb2;
		break;
	default:
		return;
	}

	cfg = &block->obwb.cfg;

	obwb->ctrl =
		FIELD_PREP(NEO_OB_WB0_CTRL_CAM0_OBPP, cfg->ctrl_obpp);
	obwb->r_ctrl =
		FIELD_PREP(NEO_OB_WB0_R_CTRL_CAM0_OFFSET, cfg->r_ctrl_offset) |
		FIELD_PREP(NEO_OB_WB0_R_CTRL_CAM0_GAIN, cfg->r_ctrl_gain);
	obwb->gr_ctrl =
		FIELD_PREP(NEO_OB_WB0_GR_CTRL_CAM0_OFFSET, cfg->gr_ctrl_offset) |
		FIELD_PREP(NEO_OB_WB0_GR_CTRL_CAM0_GAIN, cfg->gr_ctrl_gain);
	obwb->gb_ctrl =
		FIELD_PREP(NEO_OB_WB0_GB_CTRL_CAM0_OFFSET, cfg->gb_ctrl_offset) |
		FIELD_PREP(NEO_OB_WB0_GB_CTRL_CAM0_GAIN, cfg->gb_ctrl_gain);
	obwb->b_ctrl =
		FIELD_PREP(NEO_OB_WB0_B_CTRL_CAM0_OFFSET, cfg->b_ctrl_offset) |
		FIELD_PREP(NEO_OB_WB0_B_CTRL_CAM0_GAIN, cfg->b_ctrl_gain);
}

static void neoisp_params_handler_obwb0(struct neoisp_context_s *ctx,
					const union neoisp_params_block_u *block)
{
	__neoisp_params_handler_obwb(ctx, block, 0);
}

static void neoisp_params_handler_obwb1(struct neoisp_context_s *ctx,
					const union neoisp_params_block_u *block)
{
	__neoisp_params_handler_obwb(ctx, block, 1);
}

static void neoisp_params_handler_obwb2(struct neoisp_context_s *ctx,
					const union neoisp_params_block_u *block)
{
	__neoisp_params_handler_obwb(ctx, block, 2);
}

static void
neoisp_params_handler_hdr_merge(struct neoisp_context_s *ctx,
				const union neoisp_params_block_u *block)
{
	struct neoisp_hdr_merge_s *hmg = &ctx->hw.hdr_merge;
	const struct neoisp_hdr_merge_cfg_s *cfg;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_HDR_MERGE_CTRL_CAM0_ENABLE, &hmg->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_HDR_MERGE_CTRL_CAM0_ENABLE, &hmg->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->hdr_merge.cfg;

	FIELD_MODIFY(NEO_HDR_MERGE_CTRL_CAM0_OBPP, &hmg->ctrl, cfg->ctrl_obpp);
	FIELD_MODIFY(NEO_HDR_MERGE_CTRL_CAM0_MOTION_FIX_EN, &hmg->ctrl, cfg->ctrl_motion_fix_en);
	FIELD_MODIFY(NEO_HDR_MERGE_CTRL_CAM0_BLEND_3X3, &hmg->ctrl, cfg->ctrl_blend_3x3);
	FIELD_MODIFY(NEO_HDR_MERGE_CTRL_CAM0_GAIN0BPP, &hmg->ctrl, cfg->ctrl_gain0bpp);
	FIELD_MODIFY(NEO_HDR_MERGE_CTRL_CAM0_GAIN1BPP, &hmg->ctrl, cfg->ctrl_gain1bpp);

	hmg->gain_offset =
		FIELD_PREP(NEO_HDR_MERGE_GAIN_OFFSET_CAM0_OFFSET0, cfg->gain_offset_offset0) |
		FIELD_PREP(NEO_HDR_MERGE_GAIN_OFFSET_CAM0_OFFSET1, cfg->gain_offset_offset1);
	hmg->gain_scale =
		FIELD_PREP(NEO_HDR_MERGE_GAIN_SCALE_CAM0_SCALE0, cfg->gain_scale_scale0) |
		FIELD_PREP(NEO_HDR_MERGE_GAIN_SCALE_CAM0_SCALE1, cfg->gain_scale_scale1);
	hmg->gain_shift =
		FIELD_PREP(NEO_HDR_MERGE_GAIN_SHIFT_CAM0_SHIFT0, cfg->gain_shift_shift0) |
		FIELD_PREP(NEO_HDR_MERGE_GAIN_SHIFT_CAM0_SHIFT1, cfg->gain_shift_shift1);
	hmg->luma_th =
		FIELD_PREP(NEO_HDR_MERGE_LUMA_TH_CAM0_TH0, cfg->luma_th_th0);
	hmg->luma_scale =
		FIELD_PREP(NEO_HDR_MERGE_LUMA_SCALE_CAM0_SCALE, cfg->luma_scale_scale) |
		FIELD_PREP(NEO_HDR_MERGE_LUMA_SCALE_CAM0_SHIFT, cfg->luma_scale_shift) |
		FIELD_PREP(NEO_HDR_MERGE_LUMA_SCALE_CAM0_THSHIFT, cfg->luma_scale_thshift);
	hmg->downscale =
		FIELD_PREP(NEO_HDR_MERGE_DOWNSCALE_CAM0_IMGSCALE0, cfg->downscale_imgscale0) |
		FIELD_PREP(NEO_HDR_MERGE_DOWNSCALE_CAM0_IMGSCALE1, cfg->downscale_imgscale1);
	hmg->upscale =
		FIELD_PREP(NEO_HDR_MERGE_UPSCALE_CAM0_IMGSCALE0, cfg->upscale_imgscale0) |
		FIELD_PREP(NEO_HDR_MERGE_UPSCALE_CAM0_IMGSCALE1, cfg->upscale_imgscale1);
	hmg->post_scale =
		FIELD_PREP(NEO_HDR_MERGE_POST_SCALE_CAM0_SCALE, cfg->post_scale_scale);
}

static void
neoisp_params_handler_rgbir(struct neoisp_context_s *ctx,
			    const union neoisp_params_block_u *block)
{
	struct neoisp_rgbir_s *rgbir = &ctx->hw.rgbir;
	const struct neoisp_rgbir_cfg_s *cfg;
	const struct neoisp_stat_hist_cfg_s *hist;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_RGBIR_CTRL_CAM0_ENABLE, &rgbir->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_RGBIR_CTRL_CAM0_ENABLE, &rgbir->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->rgbir.cfg;

	rgbir->ccm0 =
		FIELD_PREP(NEO_RGBIR_CCM0_CAM0_CCM, cfg->ccm0_ccm);
	rgbir->ccm1 =
		FIELD_PREP(NEO_RGBIR_CCM1_CAM0_CCM, cfg->ccm1_ccm);
	rgbir->ccm2 =
		FIELD_PREP(NEO_RGBIR_CCM2_CAM0_CCM, cfg->ccm2_ccm);
	rgbir->ccm0_th =
		FIELD_PREP(NEO_RGBIR_CCM0_TH_CAM0_THRESHOLD, cfg->ccm0_th_threshold);
	rgbir->ccm1_th =
		FIELD_PREP(NEO_RGBIR_CCM1_TH_CAM0_THRESHOLD, cfg->ccm1_th_threshold);
	rgbir->ccm2_th =
		FIELD_PREP(NEO_RGBIR_CCM2_TH_CAM0_THRESHOLD, cfg->ccm2_th_threshold);
	rgbir->roi0_pos =
		FIELD_PREP(NEO_RGBIR_ROI0_POS_CAM0_XPOS, cfg->roi[0].xpos) |
		FIELD_PREP(NEO_RGBIR_ROI0_POS_CAM0_YPOS, cfg->roi[0].ypos);
	rgbir->roi0_size =
		FIELD_PREP(NEO_RGBIR_ROI0_SIZE_CAM0_WIDTH, cfg->roi[0].width) |
		FIELD_PREP(NEO_RGBIR_ROI0_SIZE_CAM0_HEIGHT, cfg->roi[0].height);
	rgbir->roi1_pos =
		FIELD_PREP(NEO_RGBIR_ROI1_POS_CAM0_XPOS, cfg->roi[1].xpos) |
		FIELD_PREP(NEO_RGBIR_ROI1_POS_CAM0_YPOS, cfg->roi[1].ypos);
	rgbir->roi1_size =
		FIELD_PREP(NEO_RGBIR_ROI1_SIZE_CAM0_WIDTH, cfg->roi[1].width) |
		FIELD_PREP(NEO_RGBIR_ROI1_SIZE_CAM0_HEIGHT, cfg->roi[1].height);

	hist = &cfg->hists[0];
	rgbir->hist0_ctrl =
		FIELD_PREP(NEO_RGBIR_HIST0_CTRL_CAM0_LIN_INPUT1_LOG,
			   hist->hist_ctrl_lin_input1_log) |
		FIELD_PREP(NEO_RGBIR_HIST0_CTRL_CAM0_DIR_INPUT1_DIF,
			   hist->hist_ctrl_dir_input1_dif) |
		FIELD_PREP(NEO_RGBIR_HIST0_CTRL_CAM0_PATTERN, hist->hist_ctrl_pattern) |
		FIELD_PREP(NEO_RGBIR_HIST0_CTRL_CAM0_CHANNEL, hist->hist_ctrl_channel) |
		FIELD_PREP(NEO_RGBIR_HIST0_CTRL_CAM0_OFFSET, hist->hist_ctrl_offset);
	rgbir->hist0_scale =
		FIELD_PREP(NEO_RGBIR_HIST0_SCALE_CAM0_SCALE, hist->hist_scale_scale);

	hist = &cfg->hists[1];
	rgbir->hist1_ctrl =
		FIELD_PREP(NEO_RGBIR_HIST1_CTRL_CAM0_LIN_INPUT1_LOG,
			   hist->hist_ctrl_lin_input1_log) |
		FIELD_PREP(NEO_RGBIR_HIST1_CTRL_CAM0_DIR_INPUT1_DIF,
			   hist->hist_ctrl_dir_input1_dif) |
		FIELD_PREP(NEO_RGBIR_HIST1_CTRL_CAM0_PATTERN, hist->hist_ctrl_pattern) |
		FIELD_PREP(NEO_RGBIR_HIST1_CTRL_CAM0_CHANNEL, hist->hist_ctrl_channel) |
		FIELD_PREP(NEO_RGBIR_HIST1_CTRL_CAM0_OFFSET, hist->hist_ctrl_offset);
	rgbir->hist1_scale =
		FIELD_PREP(NEO_RGBIR_HIST1_SCALE_CAM0_SCALE, hist->hist_scale_scale);
}

static void
neoisp_params_handler_stat(struct neoisp_context_s *ctx,
			   const union neoisp_params_block_u *block)
{
	struct neoisp_stat_s *stat = &ctx->hw.stat;
	const struct neoisp_stat_cfg_s *cfg;
	const struct neoisp_stat_hist_cfg_s *hist;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->stat.cfg;

	stat->roi0_pos =
		FIELD_PREP(NEO_STAT_ROI0_POS_CAM0_XPOS, cfg->roi0.xpos) |
		FIELD_PREP(NEO_STAT_ROI0_POS_CAM0_YPOS, cfg->roi0.ypos);
	stat->roi0_size =
		FIELD_PREP(NEO_STAT_ROI0_SIZE_CAM0_WIDTH, cfg->roi0.width) |
		FIELD_PREP(NEO_STAT_ROI0_SIZE_CAM0_HEIGHT, cfg->roi0.height);
	stat->roi1_pos =
		FIELD_PREP(NEO_STAT_ROI1_POS_CAM0_XPOS, cfg->roi1.xpos) |
		FIELD_PREP(NEO_STAT_ROI1_POS_CAM0_YPOS, cfg->roi1.ypos);
	stat->roi1_size =
		FIELD_PREP(NEO_STAT_ROI1_SIZE_CAM0_WIDTH, cfg->roi1.width) |
		FIELD_PREP(NEO_STAT_ROI1_SIZE_CAM0_HEIGHT, cfg->roi1.height);

	hist = &cfg->hists[0];
	stat->hist0_ctrl =
		FIELD_PREP(NEO_STAT_HIST0_CTRL_CAM0_LIN_INPUT1_LOG,
			   hist->hist_ctrl_lin_input1_log) |
		FIELD_PREP(NEO_STAT_HIST0_CTRL_CAM0_DIR_INPUT1_DIF,
			   hist->hist_ctrl_dir_input1_dif) |
		FIELD_PREP(NEO_STAT_HIST0_CTRL_CAM0_PATTERN, hist->hist_ctrl_pattern) |
		FIELD_PREP(NEO_STAT_HIST0_CTRL_CAM0_CHANNEL, hist->hist_ctrl_channel) |
		FIELD_PREP(NEO_STAT_HIST0_CTRL_CAM0_OFFSET, hist->hist_ctrl_offset);
	stat->hist0_scale =
		FIELD_PREP(NEO_STAT_HIST0_SCALE_CAM0_SCALE, hist->hist_scale_scale);

	hist = &cfg->hists[1];
	stat->hist1_ctrl =
		FIELD_PREP(NEO_STAT_HIST1_CTRL_CAM0_LIN_INPUT1_LOG,
			   hist->hist_ctrl_lin_input1_log) |
		FIELD_PREP(NEO_STAT_HIST1_CTRL_CAM0_DIR_INPUT1_DIF,
			   hist->hist_ctrl_dir_input1_dif) |
		FIELD_PREP(NEO_STAT_HIST1_CTRL_CAM0_PATTERN, hist->hist_ctrl_pattern) |
		FIELD_PREP(NEO_STAT_HIST1_CTRL_CAM0_CHANNEL, hist->hist_ctrl_channel) |
		FIELD_PREP(NEO_STAT_HIST1_CTRL_CAM0_OFFSET, hist->hist_ctrl_offset);
	stat->hist1_scale =
		FIELD_PREP(NEO_STAT_HIST1_SCALE_CAM0_SCALE, hist->hist_scale_scale);

	hist = &cfg->hists[2];
	stat->hist2_ctrl =
		FIELD_PREP(NEO_STAT_HIST2_CTRL_CAM0_LIN_INPUT1_LOG,
			   hist->hist_ctrl_lin_input1_log) |
		FIELD_PREP(NEO_STAT_HIST2_CTRL_CAM0_DIR_INPUT1_DIF,
			   hist->hist_ctrl_dir_input1_dif) |
		FIELD_PREP(NEO_STAT_HIST2_CTRL_CAM0_PATTERN, hist->hist_ctrl_pattern) |
		FIELD_PREP(NEO_STAT_HIST2_CTRL_CAM0_CHANNEL, hist->hist_ctrl_channel) |
		FIELD_PREP(NEO_STAT_HIST2_CTRL_CAM0_OFFSET, hist->hist_ctrl_offset);
	stat->hist2_scale =
		FIELD_PREP(NEO_STAT_HIST2_SCALE_CAM0_SCALE, hist->hist_scale_scale);

	hist = &cfg->hists[3];
	stat->hist3_ctrl =
		FIELD_PREP(NEO_STAT_HIST2_CTRL_CAM0_LIN_INPUT1_LOG,
			   hist->hist_ctrl_lin_input1_log) |
		FIELD_PREP(NEO_STAT_HIST2_CTRL_CAM0_DIR_INPUT1_DIF,
			   hist->hist_ctrl_dir_input1_dif) |
		FIELD_PREP(NEO_STAT_HIST2_CTRL_CAM0_PATTERN, hist->hist_ctrl_pattern) |
		FIELD_PREP(NEO_STAT_HIST2_CTRL_CAM0_CHANNEL, hist->hist_ctrl_channel) |
		FIELD_PREP(NEO_STAT_HIST2_CTRL_CAM0_OFFSET, hist->hist_ctrl_offset);
	stat->hist3_scale =
		FIELD_PREP(NEO_STAT_HIST3_SCALE_CAM0_SCALE, hist->hist_scale_scale);
}

static void
neoisp_params_handler_ir_compress(struct neoisp_context_s *ctx,
				  const union neoisp_params_block_u *block)
{
	struct neoisp_ir_compress_s *ircomp = &ctx->hw.ir_compress;
	const struct neoisp_ir_compress_cfg_s *cfg;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_IR_COMPRESS_CTRL_CAM0_ENABLE, &ircomp->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_IR_COMPRESS_CTRL_CAM0_ENABLE, &ircomp->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->ir_compress.cfg;

	FIELD_MODIFY(NEO_IR_COMPRESS_CTRL_CAM0_OBPP, &ircomp->ctrl, cfg->ctrl_obpp);

	ircomp->knee_point1 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_POINT1_CAM0_KNEEPOINT, cfg->knee_point1_kneepoint);
	ircomp->knee_point2 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_POINT2_CAM0_KNEEPOINT, cfg->knee_point2_kneepoint);
	ircomp->knee_point3 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_POINT3_CAM0_KNEEPOINT, cfg->knee_point3_kneepoint);
	ircomp->knee_point4 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_POINT4_CAM0_KNEEPOINT, cfg->knee_point4_kneepoint);
	ircomp->knee_offset0 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_OFFSET0_CAM0_OFFSET, cfg->knee_offset0_offset);
	ircomp->knee_offset1 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_OFFSET1_CAM0_OFFSET, cfg->knee_offset1_offset);
	ircomp->knee_offset2 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_OFFSET2_CAM0_OFFSET, cfg->knee_offset2_offset);
	ircomp->knee_offset3 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_OFFSET3_CAM0_OFFSET, cfg->knee_offset3_offset);
	ircomp->knee_offset4 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_OFFSET4_CAM0_OFFSET, cfg->knee_offset4_offset);
	ircomp->knee_ratio01 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_RATIO01_CAM0_RATIO0, cfg->knee_ratio01_ratio0) |
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_RATIO01_CAM0_RATIO1, cfg->knee_ratio01_ratio1);
	ircomp->knee_ratio23 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_RATIO23_CAM0_RATIO2, cfg->knee_ratio23_ratio2) |
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_RATIO23_CAM0_RATIO3, cfg->knee_ratio23_ratio3);
	ircomp->knee_ratio4 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_RATIO4_CAM0_RATIO4, cfg->knee_ratio4_ratio4);
	ircomp->knee_npoint0 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_NPOINT0_CAM0_KNEEPOINT,
			   cfg->knee_npoint0_kneepoint);
	ircomp->knee_npoint1 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_NPOINT1_CAM0_KNEEPOINT,
			   cfg->knee_npoint1_kneepoint);
	ircomp->knee_npoint2 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_NPOINT2_CAM0_KNEEPOINT,
			   cfg->knee_npoint2_kneepoint);
	ircomp->knee_npoint3 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_NPOINT3_CAM0_KNEEPOINT,
			   cfg->knee_npoint3_kneepoint);
	ircomp->knee_npoint4 =
		FIELD_PREP(NEO_IR_COMPRESS_KNEE_NPOINT4_CAM0_KNEEPOINT,
			   cfg->knee_npoint4_kneepoint);
}

static void
neoisp_params_handler_ctemp(struct neoisp_context_s *ctx,
			    const union neoisp_params_block_u *block)
{
	struct neoisp_ctemp_s *ctemp = &ctx->hw.ctemp;
	const struct neoisp_ctemp_cfg_s *cfg;
	const struct neoisp_ctemp_roi_desc_s *croi;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_COLOR_TEMP_CTRL_CAM0_ENABLE, &ctemp->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_COLOR_TEMP_CTRL_CAM0_ENABLE, &ctemp->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->ctemp.cfg;

	FIELD_MODIFY(NEO_COLOR_TEMP_CTRL_CAM0_IBPP, &ctemp->ctrl, cfg->ctrl_ibpp);
	FIELD_MODIFY(NEO_COLOR_TEMP_CTRL_CAM0_CSCON, &ctemp->ctrl, cfg->ctrl_cscon);

	ctemp->roi_pos =
		FIELD_PREP(NEO_COLOR_TEMP_ROI_POS_CAM0_XPOS, cfg->roi.xpos) |
		FIELD_PREP(NEO_COLOR_TEMP_ROI_POS_CAM0_YPOS, cfg->roi.ypos);
	ctemp->roi_size =
		FIELD_PREP(NEO_COLOR_TEMP_ROI_SIZE_CAM0_WIDTH, cfg->roi.width) |
		FIELD_PREP(NEO_COLOR_TEMP_ROI_SIZE_CAM0_HEIGHT, cfg->roi.height);
	ctemp->redgain =
		FIELD_PREP(NEO_COLOR_TEMP_REDGAIN_CAM0_MIN, cfg->redgain_min) |
		FIELD_PREP(NEO_COLOR_TEMP_REDGAIN_CAM0_MAX, cfg->redgain_max);
	ctemp->bluegain =
		FIELD_PREP(NEO_COLOR_TEMP_BLUEGAIN_CAM0_MIN, cfg->bluegain_min) |
		FIELD_PREP(NEO_COLOR_TEMP_BLUEGAIN_CAM0_MAX, cfg->bluegain_max);
	ctemp->point1 =
		FIELD_PREP(NEO_COLOR_TEMP_POINT1_CAM0_BLUE, cfg->point1_blue) |
		FIELD_PREP(NEO_COLOR_TEMP_POINT1_CAM0_RED, cfg->point1_red);
	ctemp->point2 =
		FIELD_PREP(NEO_COLOR_TEMP_POINT2_CAM0_BLUE, cfg->point2_blue) |
		FIELD_PREP(NEO_COLOR_TEMP_POINT2_CAM0_RED, cfg->point2_red);
	ctemp->hoffset =
		FIELD_PREP(NEO_COLOR_TEMP_HOFFSET_CAM0_RIGHT, cfg->hoffset_right) |
		FIELD_PREP(NEO_COLOR_TEMP_HOFFSET_CAM0_LEFT, cfg->hoffset_left);
	ctemp->voffset =
		FIELD_PREP(NEO_COLOR_TEMP_VOFFSET_CAM0_UP, cfg->voffset_up) |
		FIELD_PREP(NEO_COLOR_TEMP_VOFFSET_CAM0_DOWN, cfg->voffset_down);
	ctemp->point1_slope =
		FIELD_PREP(NEO_COLOR_TEMP_POINT1_SLOPE_CAM0_SLOPE_L, cfg->point1_slope_slope_l) |
		FIELD_PREP(NEO_COLOR_TEMP_POINT1_SLOPE_CAM0_SLOPE_R, cfg->point1_slope_slope_r);
	ctemp->point2_slope =
		FIELD_PREP(NEO_COLOR_TEMP_POINT2_SLOPE_CAM0_SLOPE_L, cfg->point2_slope_slope_l) |
		FIELD_PREP(NEO_COLOR_TEMP_POINT2_SLOPE_CAM0_SLOPE_R, cfg->point2_slope_slope_r);
	ctemp->luma_th =
		FIELD_PREP(NEO_COLOR_TEMP_LUMA_TH_CAM0_THL, cfg->luma_th_thl) |
		FIELD_PREP(NEO_COLOR_TEMP_LUMA_TH_CAM0_THH, cfg->luma_th_thh);
	ctemp->csc_mat0 =
		FIELD_PREP(NEO_COLOR_TEMP_CSC_MAT0_CAM0_R0C0, cfg->csc_matrix[0][0]) |
		FIELD_PREP(NEO_COLOR_TEMP_CSC_MAT0_CAM0_R0C1, cfg->csc_matrix[0][1]);
	ctemp->csc_mat1 =
		FIELD_PREP(NEO_COLOR_TEMP_CSC_MAT1_CAM0_R0C2, cfg->csc_matrix[0][2]) |
		FIELD_PREP(NEO_COLOR_TEMP_CSC_MAT1_CAM0_R1C0, cfg->csc_matrix[1][0]);
	ctemp->csc_mat2 =
		FIELD_PREP(NEO_COLOR_TEMP_CSC_MAT2_CAM0_R1C1, cfg->csc_matrix[1][1]) |
		FIELD_PREP(NEO_COLOR_TEMP_CSC_MAT2_CAM0_R1C2, cfg->csc_matrix[1][2]);
	ctemp->csc_mat3 =
		FIELD_PREP(NEO_COLOR_TEMP_CSC_MAT3_CAM0_R2C0, cfg->csc_matrix[2][0]) |
		FIELD_PREP(NEO_COLOR_TEMP_CSC_MAT3_CAM0_R2C1, cfg->csc_matrix[2][1]);
	ctemp->csc_mat4 =
		FIELD_PREP(NEO_COLOR_TEMP_CSC_MAT4_CAM0_R2C2, cfg->csc_matrix[2][2]);
	ctemp->r_gr_offset =
		FIELD_PREP(NEO_COLOR_TEMP_R_GR_OFFSET_CAM0_OFFSET0, cfg->offsets[0]) |
		FIELD_PREP(NEO_COLOR_TEMP_R_GR_OFFSET_CAM0_OFFSET1, cfg->offsets[1]);
	ctemp->gb_b_offset =
		FIELD_PREP(NEO_COLOR_TEMP_GB_B_OFFSET_CAM0_OFFSET0, cfg->offsets[2]) |
		FIELD_PREP(NEO_COLOR_TEMP_GB_B_OFFSET_CAM0_OFFSET1, cfg->offsets[3]);
	ctemp->stat_blk_size0 =
		FIELD_PREP(NEO_COLOR_TEMP_STAT_BLK_SIZE0_XSIZE, cfg->stat_blk_size0_xsize) |
		FIELD_PREP(NEO_COLOR_TEMP_STAT_BLK_SIZE0_YSIZE, cfg->stat_blk_size0_ysize);

	croi = &cfg->color_rois[0];
	ctemp->croi0_pos =
		FIELD_PREP(NEO_COLOR_TEMP_CROI0_POS_CAM0_ROVERG_LOW, croi->pos_roverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI0_POS_CAM0_ROVERG_HIGH, croi->pos_roverg_high) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI0_POS_CAM0_BOVERG_LOW, croi->pos_boverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI0_POS_CAM0_BOVERG_HIGH, croi->pos_boverg_high);

	croi = &cfg->color_rois[1];
	ctemp->croi1_pos =
		FIELD_PREP(NEO_COLOR_TEMP_CROI1_POS_CAM0_ROVERG_LOW, croi->pos_roverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI1_POS_CAM0_ROVERG_HIGH, croi->pos_roverg_high) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI1_POS_CAM0_BOVERG_LOW, croi->pos_boverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI1_POS_CAM0_BOVERG_HIGH, croi->pos_boverg_high);

	croi = &cfg->color_rois[2];
	ctemp->croi2_pos =
		FIELD_PREP(NEO_COLOR_TEMP_CROI2_POS_CAM0_ROVERG_LOW, croi->pos_roverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI2_POS_CAM0_ROVERG_HIGH, croi->pos_roverg_high) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI2_POS_CAM0_BOVERG_LOW, croi->pos_boverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI2_POS_CAM0_BOVERG_HIGH, croi->pos_boverg_high);

	croi = &cfg->color_rois[3];
	ctemp->croi3_pos =
		FIELD_PREP(NEO_COLOR_TEMP_CROI3_POS_CAM0_ROVERG_LOW, croi->pos_roverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI3_POS_CAM0_ROVERG_HIGH, croi->pos_roverg_high) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI3_POS_CAM0_BOVERG_LOW, croi->pos_boverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI3_POS_CAM0_BOVERG_HIGH, croi->pos_boverg_high);

	croi = &cfg->color_rois[4];
	ctemp->croi4_pos =
		FIELD_PREP(NEO_COLOR_TEMP_CROI4_POS_CAM0_ROVERG_LOW, croi->pos_roverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI4_POS_CAM0_ROVERG_HIGH, croi->pos_roverg_high) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI4_POS_CAM0_BOVERG_LOW, croi->pos_boverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI4_POS_CAM0_BOVERG_HIGH, croi->pos_boverg_high);

	croi = &cfg->color_rois[5];
	ctemp->croi5_pos =
		FIELD_PREP(NEO_COLOR_TEMP_CROI5_POS_CAM0_ROVERG_LOW, croi->pos_roverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI5_POS_CAM0_ROVERG_HIGH, croi->pos_roverg_high) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI5_POS_CAM0_BOVERG_LOW, croi->pos_boverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI5_POS_CAM0_BOVERG_HIGH, croi->pos_boverg_high);

	croi = &cfg->color_rois[6];
	ctemp->croi6_pos =
		FIELD_PREP(NEO_COLOR_TEMP_CROI6_POS_CAM0_ROVERG_LOW, croi->pos_roverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI6_POS_CAM0_ROVERG_HIGH, croi->pos_roverg_high) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI6_POS_CAM0_BOVERG_LOW, croi->pos_boverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI6_POS_CAM0_BOVERG_HIGH, croi->pos_boverg_high);

	croi = &cfg->color_rois[7];
	ctemp->croi7_pos =
		FIELD_PREP(NEO_COLOR_TEMP_CROI7_POS_CAM0_ROVERG_LOW, croi->pos_roverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI7_POS_CAM0_ROVERG_HIGH, croi->pos_roverg_high) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI7_POS_CAM0_BOVERG_LOW, croi->pos_boverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI7_POS_CAM0_BOVERG_HIGH, croi->pos_boverg_high);

	croi = &cfg->color_rois[8];
	ctemp->croi8_pos =
		FIELD_PREP(NEO_COLOR_TEMP_CROI8_POS_CAM0_ROVERG_LOW, croi->pos_roverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI8_POS_CAM0_ROVERG_HIGH, croi->pos_roverg_high) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI8_POS_CAM0_BOVERG_LOW, croi->pos_boverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI8_POS_CAM0_BOVERG_HIGH, croi->pos_boverg_high);

	croi = &cfg->color_rois[9];
	ctemp->croi9_pos =
		FIELD_PREP(NEO_COLOR_TEMP_CROI9_POS_CAM0_ROVERG_LOW, croi->pos_roverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI9_POS_CAM0_ROVERG_HIGH, croi->pos_roverg_high) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI9_POS_CAM0_BOVERG_LOW, croi->pos_boverg_low) |
		FIELD_PREP(NEO_COLOR_TEMP_CROI9_POS_CAM0_BOVERG_HIGH, croi->pos_boverg_high);

	ctemp->gr_avg_in =
		FIELD_PREP(NEO_COLOR_TEMP_GR_AVG_IN_CAM0_GR_AGV, cfg->gr_avg_in_gr_agv);
	ctemp->gb_avg_in =
		FIELD_PREP(NEO_COLOR_TEMP_GB_AVG_IN_CAM0_GB_AGV, cfg->gb_avg_in_gb_agv);
}

static void
neoisp_params_handler_bnr(struct neoisp_context_s *ctx,
			  const union neoisp_params_block_u *block)
{
	struct neoisp_bnr_s *bnr = &ctx->hw.bnr;
	const struct neoisp_bnr_cfg_s *cfg;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_BNR_CTRL_CAM0_ENABLE, &bnr->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_BNR_CTRL_CAM0_ENABLE, &bnr->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->bnr.cfg;

	FIELD_MODIFY(NEO_BNR_CTRL_CAM0_OBPP, &bnr->ctrl, cfg->ctrl_obpp);
	FIELD_MODIFY(NEO_BNR_CTRL_CAM0_DEBUG, &bnr->ctrl, cfg->ctrl_debug);
	FIELD_MODIFY(NEO_BNR_CTRL_CAM0_NHOOD, &bnr->ctrl, cfg->ctrl_nhood);

	bnr->ypeak =
		FIELD_PREP(NEO_BNR_YPEAK_CAM0_PEAK_LOW, cfg->ypeak_peak_low) |
		FIELD_PREP(NEO_BNR_YPEAK_CAM0_PEAK_SEL, cfg->ypeak_peak_sel) |
		FIELD_PREP(NEO_BNR_YPEAK_CAM0_PEAK_HIGH, cfg->ypeak_peak_high) |
		FIELD_PREP(NEO_BNR_YPEAK_CAM0_PEAK_OUTSEL, cfg->ypeak_peak_outsel);
	bnr->yedge_th0 =
		FIELD_PREP(NEO_BNR_YEDGE_TH0_CAM0_EDGE_TH0, cfg->yedge_th0_edge_th0);
	bnr->yedge_scale =
		FIELD_PREP(NEO_BNR_YEDGE_SCALE_CAM0_SCALE, cfg->yedge_scale_scale) |
		FIELD_PREP(NEO_BNR_YEDGE_SCALE_CAM0_SHIFT, cfg->yedge_scale_shift);
	bnr->yedges_th0 =
		FIELD_PREP(NEO_BNR_YEDGES_TH0_CAM0_EDGE_TH0, cfg->yedges_th0_edge_th0);
	bnr->yedges_scale =
		FIELD_PREP(NEO_BNR_YEDGES_SCALE_CAM0_SCALE, cfg->yedges_scale_scale) |
		FIELD_PREP(NEO_BNR_YEDGES_SCALE_CAM0_SHIFT, cfg->yedges_scale_shift);
	bnr->yedgea_th0 =
		FIELD_PREP(NEO_BNR_YEDGEA_TH0_CAM0_EDGE_TH0, cfg->yedgea_th0_edge_th0);
	bnr->yedgea_scale =
		FIELD_PREP(NEO_BNR_YEDGEA_SCALE_CAM0_SCALE, cfg->yedgea_scale_scale) |
		FIELD_PREP(NEO_BNR_YEDGEA_SCALE_CAM0_SHIFT, cfg->yedgea_scale_shift);
	bnr->yluma_x_th0 =
		FIELD_PREP(NEO_BNR_YLUMA_X_TH0_CAM0_TH, cfg->yluma_x_th0_th);
	bnr->yluma_y_th =
		FIELD_PREP(NEO_BNR_YLUMA_Y_TH_CAM0_LUMA_Y_TH0, cfg->yluma_y_th_luma_y_th0) |
		FIELD_PREP(NEO_BNR_YLUMA_Y_TH_CAM0_LUMA_Y_TH1, cfg->yluma_y_th_luma_y_th1);
	bnr->yluma_scale =
		FIELD_PREP(NEO_BNR_YLUMA_SCALE_CAM0_SCALE, cfg->yluma_scale_scale) |
		FIELD_PREP(NEO_BNR_YLUMA_SCALE_CAM0_SHIFT, cfg->yluma_scale_shift);
	bnr->yalpha_gain =
		FIELD_PREP(NEO_BNR_YALPHA_GAIN_CAM0_GAIN, cfg->yalpha_gain_gain) |
		FIELD_PREP(NEO_BNR_YALPHA_GAIN_CAM0_OFFSET, cfg->yalpha_gain_offset);
	bnr->cpeak =
		FIELD_PREP(NEO_BNR_CPEAK_CAM0_PEAK_LOW, cfg->cpeak_peak_low) |
		FIELD_PREP(NEO_BNR_CPEAK_CAM0_PEAK_SEL, cfg->cpeak_peak_sel) |
		FIELD_PREP(NEO_BNR_CPEAK_CAM0_PEAK_HIGH, cfg->cpeak_peak_high) |
		FIELD_PREP(NEO_BNR_CPEAK_CAM0_PEAK_OUTSEL, cfg->cpeak_peak_outsel);
	bnr->cedge_th0 =
		FIELD_PREP(NEO_BNR_CEDGE_TH0_CAM0_EDGE_TH0, cfg->cedge_th0_edge_th0);
	bnr->cedge_scale =
		FIELD_PREP(NEO_BNR_CEDGE_SCALE_CAM0_SCALE, cfg->cedge_scale_scale) |
		FIELD_PREP(NEO_BNR_CEDGE_SCALE_CAM0_SHIFT, cfg->cedge_scale_shift);
	bnr->cedges_th0 =
		FIELD_PREP(NEO_BNR_CEDGES_TH0_CAM0_EDGE_TH0, cfg->cedges_th0_edge_th0);
	bnr->cedges_scale =
		FIELD_PREP(NEO_BNR_CEDGES_SCALE_CAM0_SCALE, cfg->cedges_scale_scale) |
		FIELD_PREP(NEO_BNR_CEDGES_SCALE_CAM0_SHIFT, cfg->cedges_scale_shift);
	bnr->cedgea_th0 =
		FIELD_PREP(NEO_BNR_CEDGEA_TH0_CAM0_EDGE_TH0, cfg->cedgea_th0_edge_th0);
	bnr->cedgea_scale =
		FIELD_PREP(NEO_BNR_CEDGEA_SCALE_CAM0_SCALE, cfg->cedgea_scale_scale) |
		FIELD_PREP(NEO_BNR_CEDGEA_SCALE_CAM0_SHIFT, cfg->cedgea_scale_shift);
	bnr->cluma_x_th0 =
		FIELD_PREP(NEO_BNR_CLUMA_X_TH0_CAM0_TH, cfg->cluma_x_th0_th);
	bnr->cluma_y_th =
		FIELD_PREP(NEO_BNR_CLUMA_Y_TH_CAM0_LUMA_Y_TH0, cfg->cluma_y_th_luma_y_th0) |
		FIELD_PREP(NEO_BNR_CLUMA_Y_TH_CAM0_LUMA_Y_TH1, cfg->cluma_y_th_luma_y_th1);
	bnr->cluma_scale =
		FIELD_PREP(NEO_BNR_CLUMA_SCALE_CAM0_SCALE, cfg->cluma_scale_scale) |
		FIELD_PREP(NEO_BNR_CLUMA_SCALE_CAM0_SHIFT, cfg->cluma_scale_shift);
	bnr->calpha_gain =
		FIELD_PREP(NEO_BNR_CALPHA_GAIN_CAM0_GAIN, cfg->calpha_gain_gain) |
		FIELD_PREP(NEO_BNR_CALPHA_GAIN_CAM0_OFFSET, cfg->calpha_gain_offset);
	bnr->stretch =
		FIELD_PREP(NEO_BNR_STRETCH_CAM0_GAIN, cfg->stretch_gain);
}

static void
neoisp_params_handler_vignetting_ctrl(struct neoisp_context_s *ctx,
				      const union neoisp_params_block_u *block)
{
	struct neoisp_vignetting_ctrl_s *vignetting = &ctx->hw.vignetting_ctrl;
	const struct neoisp_vignetting_ctrl_cfg_s *cfg;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_VIGNETTING_CTRL_CAM0_ENABLE, &vignetting->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_VIGNETTING_CTRL_CAM0_ENABLE, &vignetting->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->vignetting_ctrl.cfg;

	vignetting->blk_conf =
		FIELD_PREP(NEO_VIGNETTING_BLK_CONF_CAM0_COLS, cfg->blk_conf_cols) |
		FIELD_PREP(NEO_VIGNETTING_BLK_CONF_CAM0_ROWS, cfg->blk_conf_rows);
	vignetting->blk_size =
		FIELD_PREP(NEO_VIGNETTING_BLK_SIZE_CAM0_XSIZE, cfg->blk_size_xsize) |
		FIELD_PREP(NEO_VIGNETTING_BLK_SIZE_CAM0_YSIZE, cfg->blk_size_ysize);
	vignetting->blk_stepy =
		FIELD_PREP(NEO_VIGNETTING_BLK_STEPY_CAM0_STEP, cfg->blk_stepy_step);
	vignetting->blk_stepx =
		FIELD_PREP(NEO_VIGNETTING_BLK_STEPX_CAM0_STEP, cfg->blk_stepx_step);
}

static void
neoisp_params_handler_demosaic(struct neoisp_context_s *ctx,
			       const union neoisp_params_block_u *block)
{
	struct neoisp_demosaic_s *demosaic = &ctx->hw.demosaic;
	const struct neoisp_demosaic_cfg_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->demosaic.cfg;

	demosaic->ctrl =
		FIELD_PREP(NEO_DEMOSAIC_CTRL_CAM0_FMT, cfg->ctrl_fmt);
	demosaic->activity_ctl =
		FIELD_PREP(NEO_DEMOSAIC_ACTIVITY_CTL_CAM0_ALPHA, cfg->activity_ctl_alpha) |
		FIELD_PREP(NEO_DEMOSAIC_ACTIVITY_CTL_CAM0_ACT_RATIO, cfg->activity_ctl_act_ratio);
	demosaic->dynamics_ctl0 =
		FIELD_PREP(NEO_DEMOSAIC_DYNAMICS_CTL0_CAM0_STRENGTHG,
			   cfg->dynamics_ctl0_strengthg);
	demosaic->dynamics_ctl0 |=
		FIELD_PREP(NEO_DEMOSAIC_DYNAMICS_CTL0_CAM0_STRENGTHC,
			   cfg->dynamics_ctl0_strengthc);
	demosaic->dynamics_ctl2 =
		FIELD_PREP(NEO_DEMOSAIC_DYNAMICS_CTL2_CAM0_MAX_IMPACT,
			   cfg->dynamics_ctl2_max_impact);
}

static void
neoisp_params_handler_rgb2yuv(struct neoisp_context_s *ctx,
			      const union neoisp_params_block_u *block)
{
	struct neoisp_rgb2yuv_s *rgb2yuv = &ctx->hw.rgb2yuv;
	const struct neoisp_rgb2yuv_cfg_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->rgb2yuv.cfg;

	rgb2yuv->gain_ctrl =
		FIELD_PREP(NEO_RGB_TO_YUV_GAIN_CTRL_CAM0_RGAIN, cfg->gain_ctrl_rgain) |
		FIELD_PREP(NEO_RGB_TO_YUV_GAIN_CTRL_CAM0_BGAIN, cfg->gain_ctrl_bgain);
	rgb2yuv->mat0 =
		FIELD_PREP(NEO_RGB_TO_YUV_MAT0_CAM0_R0C0, cfg->mat_rxcy[0][0]) |
		FIELD_PREP(NEO_RGB_TO_YUV_MAT0_CAM0_R0C1, cfg->mat_rxcy[0][1]);
	rgb2yuv->mat1 =
		FIELD_PREP(NEO_RGB_TO_YUV_MAT1_CAM0_R0C2, cfg->mat_rxcy[0][2]);
	rgb2yuv->mat2 =
		FIELD_PREP(NEO_RGB_TO_YUV_MAT2_CAM0_R1C0, cfg->mat_rxcy[1][0]) |
		FIELD_PREP(NEO_RGB_TO_YUV_MAT2_CAM0_R1C1, cfg->mat_rxcy[1][1]);
	rgb2yuv->mat3 =
		FIELD_PREP(NEO_RGB_TO_YUV_MAT3_CAM0_R1C2, cfg->mat_rxcy[1][2]);
	rgb2yuv->mat4 =
		FIELD_PREP(NEO_RGB_TO_YUV_MAT4_CAM0_R2C0, cfg->mat_rxcy[2][0]) |
		FIELD_PREP(NEO_RGB_TO_YUV_MAT4_CAM0_R2C1, cfg->mat_rxcy[2][1]);
	rgb2yuv->mat5 =
		FIELD_PREP(NEO_RGB_TO_YUV_MAT5_CAM0_R2C2, cfg->mat_rxcy[2][2]);
	rgb2yuv->offset0 =
		FIELD_PREP(NEO_RGB_TO_YUV_OFFSET0_CAM0_OFFSET, cfg->csc_offsets[0]);
	rgb2yuv->offset1 =
		FIELD_PREP(NEO_RGB_TO_YUV_OFFSET1_CAM0_OFFSET, cfg->csc_offsets[1]);
	rgb2yuv->offset2 =
		FIELD_PREP(NEO_RGB_TO_YUV_OFFSET2_CAM0_OFFSET, cfg->csc_offsets[2]);
}

static void
neoisp_params_handler_dr_comp(struct neoisp_context_s *ctx,
			      const union neoisp_params_block_u *block)
{
	struct neoisp_dr_comp_s *drc = &ctx->hw.drc;
	const struct neoisp_dr_comp_cfg_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->dr_comp.cfg;

	drc->roi0_pos =
		FIELD_PREP(NEO_DRC_ROI0_POS_CAM0_XPOS, cfg->roi0.xpos) |
		FIELD_PREP(NEO_DRC_ROI0_POS_CAM0_YPOS, cfg->roi0.ypos);
	drc->roi0_size =
		FIELD_PREP(NEO_DRC_ROI0_SIZE_CAM0_WIDTH, cfg->roi0.width) |
		FIELD_PREP(NEO_DRC_ROI0_SIZE_CAM0_HEIGHT, cfg->roi0.height);
	drc->roi1_pos =
		FIELD_PREP(NEO_DRC_ROI1_POS_CAM0_XPOS, cfg->roi1.xpos) |
		FIELD_PREP(NEO_DRC_ROI1_POS_CAM0_YPOS, cfg->roi1.ypos);
	drc->roi1_size =
		FIELD_PREP(NEO_DRC_ROI1_SIZE_CAM0_WIDTH, cfg->roi1.width) |
		FIELD_PREP(NEO_DRC_ROI1_SIZE_CAM0_HEIGHT, cfg->roi1.height);
	drc->groi_sum_shift =
		FIELD_PREP(NEO_DRC_GROI_SUM_SHIFT_CAM0_SHIFT0, cfg->groi_sum_shift_shift0) |
		FIELD_PREP(NEO_DRC_GROI_SUM_SHIFT_CAM0_SHIFT1, cfg->groi_sum_shift_shift1);
	drc->gbl_gain =
		FIELD_PREP(NEO_DRC_GBL_GAIN_CAM0_GAIN, cfg->gbl_gain_gain);
	drc->lcl_blk_size =
		FIELD_PREP(NEO_DRC_LCL_BLK_SIZE_CAM0_XSIZE, cfg->lcl_blk_size_xsize) |
		FIELD_PREP(NEO_DRC_LCL_BLK_SIZE_CAM0_YSIZE, cfg->lcl_blk_size_ysize);
	drc->lcl_stretch =
		FIELD_PREP(NEO_DRC_LCL_STRETCH_CAM0_STRETCH, cfg->lcl_stretch_stretch) |
		FIELD_PREP(NEO_DRC_LCL_STRETCH_CAM0_OFFSET, cfg->lcl_stretch_offset);
	drc->lcl_blk_stepy =
		FIELD_PREP(NEO_DRC_LCL_BLK_STEPY_CAM0_STEP, cfg->lcl_blk_stepy_step);
	drc->lcl_blk_stepx =
		FIELD_PREP(NEO_DRC_LCL_BLK_STEPX_CAM0_STEP, cfg->lcl_blk_stepx_step);
	drc->lcl_sum_shift =
		FIELD_PREP(NEO_DRC_LCL_SUM_SHIFT_CAM0_SHIFT, cfg->lcl_sum_shift_shift);
	drc->alpha =
		FIELD_PREP(NEO_DRC_ALPHA_CAM0_ALPHA, cfg->alpha_alpha);
}

static void
neoisp_params_handler_nr(struct neoisp_context_s *ctx,
			 const union neoisp_params_block_u *block)
{
	struct neoisp_nr_s *nr = &ctx->hw.nr;
	const struct neoisp_nr_cfg_s *cfg;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_NR_CTRL_CAM0_ENABLE, &nr->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_NR_CTRL_CAM0_ENABLE, &nr->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->nr.cfg;

	FIELD_MODIFY(NEO_NR_CTRL_CAM0_DEBUG, &nr->ctrl, cfg->ctrl_debug);

	nr->blend_scale =
		FIELD_PREP(NEO_NR_BLEND_SCALE_CAM0_SCALE, cfg->blend_scale_scale) |
		FIELD_PREP(NEO_NR_BLEND_SCALE_CAM0_SHIFT, cfg->blend_scale_shift) |
		FIELD_PREP(NEO_NR_BLEND_SCALE_CAM0_GAIN, cfg->blend_scale_gain);
	nr->blend_th0 =
		FIELD_PREP(NEO_NR_BLEND_TH0_CAM0_TH, cfg->blend_th0_th);
}

static void
neoisp_params_handler_df(struct neoisp_context_s *ctx,
			 const union neoisp_params_block_u *block)
{
	struct neoisp_df_s *df = &ctx->hw.df;
	const struct neoisp_df_cfg_s *cfg;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_DF_CTRL_CAM0_ENABLE, &df->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_DF_CTRL_CAM0_ENABLE, &df->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->df.cfg;

	FIELD_MODIFY(NEO_DF_CTRL_CAM0_DEBUG, &df->ctrl, cfg->ctrl_debug);

	df->th_scale =
		FIELD_PREP(NEO_DF_TH_SCALE_CAM0_SCALE, cfg->th_scale_scale);
	df->blend_shift =
		FIELD_PREP(NEO_DF_BLEND_SHIFT_CAM0_SHIFT, cfg->blend_shift_shift);
	df->blend_th0 =
		FIELD_PREP(NEO_DF_BLEND_TH0_CAM0_TH, cfg->blend_th0_th);
}

static void
neoisp_params_handler_ee(struct neoisp_context_s *ctx,
			 const union neoisp_params_block_u *block)
{
	struct neoisp_ee_s *ee = &ctx->hw.ee;
	const struct neoisp_ee_cfg_s *cfg;

	if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		FIELD_MODIFY(NEO_EE_CTRL_CAM0_ENABLE, &ee->ctrl, 0);
	else if (block->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_ENABLE)
		FIELD_MODIFY(NEO_EE_CTRL_CAM0_ENABLE, &ee->ctrl, 1);

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing else to do */
		return;

	cfg = &block->ee.cfg;

	FIELD_MODIFY(NEO_EE_CTRL_CAM0_DEBUG, &ee->ctrl, cfg->ctrl_debug);

	ee->coring =
		FIELD_PREP(NEO_EE_CORING_CAM0_CORING, cfg->coring_coring);
	ee->clip =
		FIELD_PREP(NEO_EE_CLIP_CAM0_CLIP, cfg->clip_clip);
	ee->maskgain =
		FIELD_PREP(NEO_EE_MASKGAIN_CAM0_GAIN, cfg->maskgain_gain);
}

static void
neoisp_params_handler_convmed(struct neoisp_context_s *ctx,
			      const union neoisp_params_block_u *block)
{
	struct neoisp_convmed_s *convmed = &ctx->hw.convmed;
	const struct neoisp_convmed_cfg_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->convmed.cfg;

	convmed->ctrl =
		FIELD_PREP(NEO_CCONVMED_CTRL_CAM0_FLT, cfg->ctrl_flt);
}

static void
neoisp_params_handler_cas(struct neoisp_context_s *ctx,
			  const union neoisp_params_block_u *block)
{
	struct neoisp_cas_s *cas = &ctx->hw.cas;
	const struct neoisp_cas_cfg_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->cas.cfg;

	cas->gain =
		FIELD_PREP(NEO_CAS_GAIN_CAM0_SCALE, cfg->gain_scale) |
		FIELD_PREP(NEO_CAS_GAIN_CAM0_SHIFT, cfg->gain_shift);
	cas->corr =
		FIELD_PREP(NEO_CAS_CORR_CAM0_CORR, cfg->corr_corr);
	cas->offset =
		FIELD_PREP(NEO_CAS_OFFSET_CAM0_OFFSET, cfg->offset_offset);
}

static void
neoisp_params_handler_gcm(struct neoisp_context_s *ctx,
			  const union neoisp_params_block_u *block)
{
	struct neoisp_gcm_s *gcm = &ctx->hw.gcm;
	const struct neoisp_gcm_cfg_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->gcm.cfg;

	gcm->imat0 =
		FIELD_PREP(NEO_GCM_IMAT0_CAM0_R0C0, cfg->imat_rxcy[0][0]) |
		FIELD_PREP(NEO_GCM_IMAT0_CAM0_R0C1, cfg->imat_rxcy[0][1]);
	gcm->imat1 =
		FIELD_PREP(NEO_GCM_IMAT1_CAM0_R0C2, cfg->imat_rxcy[0][2]);
	gcm->imat2 =
		FIELD_PREP(NEO_GCM_IMAT2_CAM0_R1C0, cfg->imat_rxcy[1][0]) |
		FIELD_PREP(NEO_GCM_IMAT2_CAM0_R1C1, cfg->imat_rxcy[1][1]);
	gcm->imat3 =
		FIELD_PREP(NEO_GCM_IMAT3_CAM0_R1C2, cfg->imat_rxcy[1][2]);
	gcm->imat4 =
		FIELD_PREP(NEO_GCM_IMAT4_CAM0_R2C0, cfg->imat_rxcy[2][0]) |
		FIELD_PREP(NEO_GCM_IMAT4_CAM0_R2C1, cfg->imat_rxcy[2][1]);
	gcm->imat5 =
		FIELD_PREP(NEO_GCM_IMAT5_CAM0_R2C2, cfg->imat_rxcy[2][2]);
	gcm->ioffset0 =
		FIELD_PREP(NEO_GCM_IOFFSET0_CAM0_OFFSET0, cfg->ioffsets[0]);
	gcm->ioffset1 =
		FIELD_PREP(NEO_GCM_IOFFSET1_CAM0_OFFSET1, cfg->ioffsets[1]);
	gcm->ioffset2 =
		FIELD_PREP(NEO_GCM_IOFFSET2_CAM0_OFFSET2, cfg->ioffsets[2]);
	gcm->omat0 =
		FIELD_PREP(NEO_GCM_OMAT0_CAM0_R0C0, cfg->omat_rxcy[0][0]) |
		FIELD_PREP(NEO_GCM_OMAT0_CAM0_R0C1, cfg->omat_rxcy[0][1]);
	gcm->omat1 =
		FIELD_PREP(NEO_GCM_OMAT1_CAM0_R0C2, cfg->omat_rxcy[0][2]);
	gcm->omat2 =
		FIELD_PREP(NEO_GCM_OMAT2_CAM0_R1C0, cfg->omat_rxcy[1][0]) |
		FIELD_PREP(NEO_GCM_OMAT2_CAM0_R1C1, cfg->omat_rxcy[1][1]);
	gcm->omat3 =
		FIELD_PREP(NEO_GCM_OMAT3_CAM0_R1C2, cfg->omat_rxcy[1][2]);
	gcm->omat4 =
		FIELD_PREP(NEO_GCM_OMAT4_CAM0_R2C0, cfg->omat_rxcy[2][0]) |
		FIELD_PREP(NEO_GCM_OMAT4_CAM0_R2C1, cfg->omat_rxcy[2][1]);
	gcm->omat5 =
		FIELD_PREP(NEO_GCM_OMAT5_CAM0_R2C2, cfg->omat_rxcy[2][2]);
	gcm->ooffset0 =
		FIELD_PREP(NEO_GCM_OOFFSET0_CAM0_OFFSET0, cfg->ooffsets[0]);
	gcm->ooffset1 =
		FIELD_PREP(NEO_GCM_OOFFSET1_CAM0_OFFSET1, cfg->ooffsets[1]);
	gcm->ooffset2 =
		FIELD_PREP(NEO_GCM_OOFFSET2_CAM0_OFFSET2, cfg->ooffsets[2]);
	gcm->gamma0 =
		FIELD_PREP(NEO_GCM_GAMMA0_CAM0_GAMMA0, cfg->gamma0_gamma0) |
		FIELD_PREP(NEO_GCM_GAMMA0_CAM0_OFFSET0, cfg->gamma0_offset0);
	gcm->gamma1 =
		FIELD_PREP(NEO_GCM_GAMMA1_CAM0_GAMMA1, cfg->gamma1_gamma1) |
		FIELD_PREP(NEO_GCM_GAMMA1_CAM0_OFFSET1, cfg->gamma1_offset1);
	gcm->gamma2 =
		FIELD_PREP(NEO_GCM_GAMMA2_CAM0_GAMMA2, cfg->gamma2_gamma2) |
		FIELD_PREP(NEO_GCM_GAMMA2_CAM0_OFFSET2, cfg->gamma2_offset2);
	gcm->blklvl0_ctrl =
		FIELD_PREP(NEO_GCM_BLKLVL0_CTRL_CAM0_OFFSET0, cfg->blklvl0_ctrl_offset0) |
		FIELD_PREP(NEO_GCM_BLKLVL0_CTRL_CAM0_GAIN0, cfg->blklvl0_ctrl_gain0);
	gcm->blklvl1_ctrl =
		FIELD_PREP(NEO_GCM_BLKLVL1_CTRL_CAM0_OFFSET1, cfg->blklvl1_ctrl_offset1) |
		FIELD_PREP(NEO_GCM_BLKLVL1_CTRL_CAM0_GAIN1, cfg->blklvl1_ctrl_gain1);
	gcm->blklvl2_ctrl =
		FIELD_PREP(NEO_GCM_BLKLVL2_CTRL_CAM0_OFFSET2, cfg->blklvl2_ctrl_offset2) |
		FIELD_PREP(NEO_GCM_BLKLVL2_CTRL_CAM0_GAIN2, cfg->blklvl2_ctrl_gain2);
	gcm->lowth_ctrl01 =
		FIELD_PREP(NEO_GCM_LOWTH_CTRL01_CAM0_THRESHOLD0, cfg->lowth_ctrl01_threshold0) |
		FIELD_PREP(NEO_GCM_LOWTH_CTRL01_CAM0_THRESHOLD1, cfg->lowth_ctrl01_threshold1);
	gcm->lowth_ctrl2 =
		FIELD_PREP(NEO_GCM_LOWTH_CTRL2_CAM0_THRESHOLD2, cfg->lowth_ctrl2_threshold2);
	gcm->mat_confg =
		FIELD_PREP(NEO_GCM_MAT_CONFG_CAM0_SIGN_CONFG, cfg->mat_confg_sign_confg);
}

static void
neoisp_params_handler_af(struct neoisp_context_s *ctx,
			 const union neoisp_params_block_u *block)
{
	struct neoisp_autofocus_s *af = &ctx->hw.autofocus;
	const struct neoisp_af_cfg_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->af.cfg;

	af->roi0_pos =
		FIELD_PREP(NEO_AUTOFOCUS_ROI0_POS_CAM0_XPOS, cfg->af_roi[0].xpos) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI0_POS_CAM0_YPOS, cfg->af_roi[0].ypos);
	af->roi0_size =
		FIELD_PREP(NEO_AUTOFOCUS_ROI0_SIZE_CAM0_WIDTH, cfg->af_roi[0].width) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI0_SIZE_CAM0_HEIGHT, cfg->af_roi[0].height);
	af->roi1_pos =
		FIELD_PREP(NEO_AUTOFOCUS_ROI1_POS_CAM0_XPOS, cfg->af_roi[1].xpos) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI1_POS_CAM0_YPOS, cfg->af_roi[1].ypos);
	af->roi1_size =
		FIELD_PREP(NEO_AUTOFOCUS_ROI1_SIZE_CAM0_WIDTH, cfg->af_roi[1].width) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI1_SIZE_CAM0_HEIGHT, cfg->af_roi[1].height);
	af->roi2_pos =
		FIELD_PREP(NEO_AUTOFOCUS_ROI2_POS_CAM0_XPOS, cfg->af_roi[2].xpos) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI2_POS_CAM0_YPOS, cfg->af_roi[2].ypos);
	af->roi2_size =
		FIELD_PREP(NEO_AUTOFOCUS_ROI2_SIZE_CAM0_WIDTH, cfg->af_roi[2].width) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI2_SIZE_CAM0_HEIGHT, cfg->af_roi[2].height);
	af->roi3_pos =
		FIELD_PREP(NEO_AUTOFOCUS_ROI3_POS_CAM0_XPOS, cfg->af_roi[3].xpos) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI3_POS_CAM0_YPOS, cfg->af_roi[3].ypos);
	af->roi3_size =
		FIELD_PREP(NEO_AUTOFOCUS_ROI3_SIZE_CAM0_WIDTH, cfg->af_roi[3].width) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI3_SIZE_CAM0_HEIGHT, cfg->af_roi[3].height);
	af->roi4_pos =
		FIELD_PREP(NEO_AUTOFOCUS_ROI4_POS_CAM0_XPOS, cfg->af_roi[4].xpos) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI4_POS_CAM0_YPOS, cfg->af_roi[4].ypos);
	af->roi4_size =
		FIELD_PREP(NEO_AUTOFOCUS_ROI4_SIZE_CAM0_WIDTH, cfg->af_roi[4].width) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI4_SIZE_CAM0_HEIGHT, cfg->af_roi[4].height);
	af->roi5_pos =
		FIELD_PREP(NEO_AUTOFOCUS_ROI5_POS_CAM0_XPOS, cfg->af_roi[5].xpos) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI5_POS_CAM0_YPOS, cfg->af_roi[5].ypos);
	af->roi5_size =
		FIELD_PREP(NEO_AUTOFOCUS_ROI5_SIZE_CAM0_WIDTH, cfg->af_roi[5].width) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI5_SIZE_CAM0_HEIGHT, cfg->af_roi[5].height);
	af->roi6_pos =
		FIELD_PREP(NEO_AUTOFOCUS_ROI6_POS_CAM0_XPOS, cfg->af_roi[6].xpos) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI6_POS_CAM0_YPOS, cfg->af_roi[6].ypos);
	af->roi6_size =
		FIELD_PREP(NEO_AUTOFOCUS_ROI6_SIZE_CAM0_WIDTH, cfg->af_roi[6].width) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI6_SIZE_CAM0_HEIGHT, cfg->af_roi[6].height);
	af->roi7_pos =
		FIELD_PREP(NEO_AUTOFOCUS_ROI7_POS_CAM0_XPOS, cfg->af_roi[7].xpos) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI7_POS_CAM0_YPOS, cfg->af_roi[7].ypos);
	af->roi7_size =
		FIELD_PREP(NEO_AUTOFOCUS_ROI7_SIZE_CAM0_WIDTH, cfg->af_roi[7].width) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI7_SIZE_CAM0_HEIGHT, cfg->af_roi[7].height);
	af->roi8_pos =
		FIELD_PREP(NEO_AUTOFOCUS_ROI8_POS_CAM0_XPOS, cfg->af_roi[8].xpos) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI8_POS_CAM0_YPOS, cfg->af_roi[8].ypos);
	af->roi8_size =
		FIELD_PREP(NEO_AUTOFOCUS_ROI8_SIZE_CAM0_WIDTH, cfg->af_roi[8].width) |
		FIELD_PREP(NEO_AUTOFOCUS_ROI8_SIZE_CAM0_HEIGHT, cfg->af_roi[8].height);
	af->fil0_coeffs0 =
		FIELD_PREP(NEO_AUTOFOCUS_FIL0_COEFFS0_CAM0_COEFF0, cfg->fil0_coeffs[0]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL0_COEFFS0_CAM0_COEFF1, cfg->fil0_coeffs[1]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL0_COEFFS0_CAM0_COEFF2, cfg->fil0_coeffs[2]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL0_COEFFS0_CAM0_COEFF3, cfg->fil0_coeffs[3]);
	af->fil0_coeffs1 =
		FIELD_PREP(NEO_AUTOFOCUS_FIL0_COEFFS1_CAM0_COEFF4, cfg->fil0_coeffs[4]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL0_COEFFS1_CAM0_COEFF5, cfg->fil0_coeffs[5]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL0_COEFFS1_CAM0_COEFF6, cfg->fil0_coeffs[6]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL0_COEFFS1_CAM0_COEFF7, cfg->fil0_coeffs[7]);
	af->fil0_coeffs2 =
		FIELD_PREP(NEO_AUTOFOCUS_FIL0_COEFFS2_CAM0_COEFF8, cfg->fil0_coeffs[8]);
	af->fil0_shift =
		FIELD_PREP(NEO_AUTOFOCUS_FIL0_SHIFT_CAM0_SHIFT, cfg->fil0_shift_shift);
	af->fil1_coeffs0 =
		FIELD_PREP(NEO_AUTOFOCUS_FIL1_COEFFS0_CAM0_COEFF0, cfg->fil1_coeffs[0]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL1_COEFFS0_CAM0_COEFF1, cfg->fil1_coeffs[1]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL1_COEFFS0_CAM0_COEFF2, cfg->fil1_coeffs[2]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL1_COEFFS0_CAM0_COEFF3, cfg->fil1_coeffs[3]);
	af->fil1_coeffs1 =
		FIELD_PREP(NEO_AUTOFOCUS_FIL1_COEFFS1_CAM0_COEFF4, cfg->fil1_coeffs[4]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL1_COEFFS1_CAM0_COEFF5, cfg->fil1_coeffs[5]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL1_COEFFS1_CAM0_COEFF6, cfg->fil1_coeffs[6]) |
		FIELD_PREP(NEO_AUTOFOCUS_FIL1_COEFFS1_CAM0_COEFF7, cfg->fil1_coeffs[7]);
	af->fil1_coeffs2 =
		FIELD_PREP(NEO_AUTOFOCUS_FIL1_COEFFS2_CAM0_COEFF8, cfg->fil1_coeffs[8]);
	af->fil1_shift =
		FIELD_PREP(NEO_AUTOFOCUS_FIL1_SHIFT_CAM0_SHIFT, cfg->fil1_shift_shift);
}

static void
neoisp_params_handler_vignetting_table(struct neoisp_context_s *ctx,
				       const union neoisp_params_block_u *block)
{
	const struct neoisp_vignetting_table_mem_params_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->vignetting_table.cfg;

	memcpy((u8 *)(uintptr_t)&ctx->vig,
	       (u8 *)(uintptr_t)cfg->vignetting_table,
	       sizeof(struct neoisp_vignetting_table_mem_params_s));
}

static void
neoisp_params_handler_drc_global_tonemap(struct neoisp_context_s *ctx,
					 const union neoisp_params_block_u *block)
{
	const struct neoisp_drc_global_tonemap_mem_params_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->drc_global_tonemap.cfg;

	memcpy((u8 *)(uintptr_t)&ctx->gtm,
	       (u8 *)(uintptr_t)cfg->drc_global_tonemap,
	       sizeof(struct neoisp_drc_global_tonemap_mem_params_s));
}

static void
neoisp_params_handler_drc_local_tonemap(struct neoisp_context_s *ctx,
					const union neoisp_params_block_u *block)
{
	const struct neoisp_drc_local_tonemap_mem_params_s *cfg;

	if (block->header.size == sizeof(struct v4l2_isp_block_header))
		/* nothing to do */
		return;

	cfg = &block->drc_local_tonemap.cfg;

	memcpy((u8 *)(uintptr_t)&ctx->ltm,
	       (u8 *)(uintptr_t)cfg->drc_local_tonemap,
	       sizeof(struct neoisp_drc_local_tonemap_mem_params_s));
}

static const struct neoisp_block_handler_s {
	void (*handler)(struct neoisp_context_s *ctx, const union neoisp_params_block_u *blk);
} neoisp_block_handlers[] = {
	[NEOISP_PARAM_BLK_PIPE_CONF] = {
		.handler = &neoisp_params_handler_pipe_conf,
	},
	[NEOISP_PARAM_BLK_HEAD_COLOR] = {
		.handler = &neoisp_params_handler_head_color,
	},
	[NEOISP_PARAM_BLK_HDR_DECOMPRESS0] = {
		.handler = &neoisp_params_handler_hdr_decompress0,
	},
	[NEOISP_PARAM_BLK_HDR_DECOMPRESS1] = {
		.handler = &neoisp_params_handler_hdr_decompress1,
	},
	[NEOISP_PARAM_BLK_OBWB0] = {
		.handler = &neoisp_params_handler_obwb0,
	},
	[NEOISP_PARAM_BLK_OBWB1] = {
		.handler = &neoisp_params_handler_obwb1,
	},
	[NEOISP_PARAM_BLK_OBWB2] = {
		.handler = &neoisp_params_handler_obwb2,
	},
	[NEOISP_PARAM_BLK_HDR_MERGE] = {
		.handler = &neoisp_params_handler_hdr_merge,
	},
	[NEOISP_PARAM_BLK_RGBIR] = {
		.handler = &neoisp_params_handler_rgbir,
	},
	[NEOISP_PARAM_BLK_STAT] = {
		.handler = &neoisp_params_handler_stat,
	},
	[NEOISP_PARAM_BLK_IR_COMPRESS] = {
		.handler = &neoisp_params_handler_ir_compress,
	},
	[NEOISP_PARAM_BLK_BNR] = {
		.handler = &neoisp_params_handler_bnr,
	},
	[NEOISP_PARAM_BLK_VIGNETTING_CTRL] = {
		.handler = &neoisp_params_handler_vignetting_ctrl,
	},
	[NEOISP_PARAM_BLK_CTEMP] = {
		.handler = &neoisp_params_handler_ctemp,
	},
	[NEOISP_PARAM_BLK_DEMOSAIC] = {
		.handler = &neoisp_params_handler_demosaic,
	},
	[NEOISP_PARAM_BLK_RGB2YUV] = {
		.handler = &neoisp_params_handler_rgb2yuv,
	},
	[NEOISP_PARAM_BLK_DR_COMP] = {
		.handler = &neoisp_params_handler_dr_comp,
	},
	[NEOISP_PARAM_BLK_NR] = {
		.handler = &neoisp_params_handler_nr,
	},
	[NEOISP_PARAM_BLK_AF] = {
		.handler = &neoisp_params_handler_af,
	},
	[NEOISP_PARAM_BLK_EE] = {
		.handler = &neoisp_params_handler_ee,
	},
	[NEOISP_PARAM_BLK_DF] = {
		.handler = &neoisp_params_handler_df,
	},
	[NEOISP_PARAM_BLK_CONVMED] = {
		.handler = &neoisp_params_handler_convmed,
	},
	[NEOISP_PARAM_BLK_CAS] = {
		.handler = &neoisp_params_handler_cas,
	},
	[NEOISP_PARAM_BLK_GCM] = {
		.handler = &neoisp_params_handler_gcm,
	},
	[NEOISP_PARAM_BLK_VIGNETTING_TABLE] = {
		.handler = &neoisp_params_handler_vignetting_table,
	},
	[NEOISP_PARAM_BLK_DRC_GLOBAL_TONEMAP] = {
		.handler = &neoisp_params_handler_drc_global_tonemap,
	},
	[NEOISP_PARAM_BLK_DRC_LOCAL_TONEMAP] = {
		.handler = &neoisp_params_handler_drc_local_tonemap,
	},
};

struct ycbcr_enc {
	/* Matrix stored in s8.8 format */
	s16 matrix[NEO_GAMMA_MATRIX_SIZE][NEO_GAMMA_MATRIX_SIZE];
	/* This range [-128, 127] is remapped to [0, 255] for full-range quantization.
	 * Thus, chrominance channels offset is 0.5 in s0.12 format that is 0.5 * 4096.
	 */
	s16 offsets[NEO_GAMMA_MATRIX_SIZE];
};

struct xfer_func {
	s16 gain; /* s8.8 format*/
	s16 blklvl_gain; /* s8.8 format */
	s16 threshold; /* s0.16 format */
	s16 gamma; /* s1.8 format */
	s16 gamma_offset; /* s0.12 format */
};

static const struct ycbcr_enc enc_lut[] = {
	[V4L2_YCBCR_ENC_601] = {
		/* BT.601 full-range encoding - floating-point matrix:
		 *	[0.299, 0.5870, 0.1140
		 *	 -0.1687, -0.3313, 0.5
		 *	 0.5, -0.4187, -0.0813]
		 */
		.matrix = {
			{77, 150, 29},
			{-43, -85, 128},
			{128, -107, -21},
		},
		.offsets = {0, 2048, 2048},
	}, [V4L2_YCBCR_ENC_709] = {
		/* BT.709 full-range encoding - floating-point matrix:
		 *	[0.2126, 0.7152, 0.0722
		 *	 -0.1146, -0.3854, 0.5
		 *	 0.5, -0.4542, -0.0458]
		 */
		.matrix = {
			{54, 183, 18},
			{-29, -99, 128},
			{128, -116, -12},
		},
		.offsets = {0, 2048, 2048},
	}, [V4L2_YCBCR_ENC_DEFAULT] = {
		/* No encoding - used for RGB output formats */
		.matrix = {
			{256, 0, 0},
			{0, 256, 0},
			{0, 0, 256},
		},
		.offsets = {0, 0, 0},
	},
};

static const struct xfer_func xfer_lut[] = {
	[V4L2_XFER_FUNC_709] = {
		/* L' = 4.5L, for 0 <= L <= 0.018
		 * L' = 1.099L^0.45 - 0.099, for L >= 0.018
		 *    = 1.099 * (L^0.45 - (0.099 / 1.099)), for L >= 0.018
		 */
		.gain = 281,
		.blklvl_gain = 1152,
		.threshold = 1180,
		.gamma = 115,
		.gamma_offset = 369,
	}, [V4L2_XFER_FUNC_SRGB] = {
		/* L' = 12.92L, for 0 <= L <= 0.0031308
		 * L' = 1.055L^(1/2.4) - 0.055, for L >= 0.0031308
		 *    = 1.055 * (L^(1/2.4) - (0.055 / 1.055)), for L >= 0.0031308
		 */
		.gain = 270,
		.blklvl_gain = 3308,
		.threshold = 205,
		.gamma = 107,
		.gamma_offset = 214,
	}, [V4L2_XFER_FUNC_NONE] = {
		.gain = 256,
		.blklvl_gain = 0,
		.threshold = 0,
		.gamma = 256,
		.gamma_offset = 0,
	},
};

void neoisp_ctx_set_default_context(struct neoisp_context_s *context)
{
	memcpy(context, &def_context, sizeof(struct neoisp_context_s));
}

/*
 * Set pipe conf volatile settings (i.e. buffer addresses)
 */
void neoisp_ctx_update_buf_addr(struct neoisp_dev_s *neoispd)
{
	struct neoisp_job_s *job = &neoispd->queued_job;
	struct neoisp_pipe_conf_s *cfg = &neoispd->context->hw.pipe_conf;
	struct neoisp_buffer_s *buf_inp0 = job->buf[NEOISP_INPUT0_NODE];
	struct neoisp_buffer_s *buf_inp1 = job->buf[NEOISP_INPUT1_NODE];
	struct neoisp_buffer_s *buf_out = job->buf[NEOISP_FRAME_NODE];
	struct neoisp_buffer_s *buf_ir = job->buf[NEOISP_IR_NODE];
	struct neoisp_node_s *nd;
	u32 ibpp, inp0_stride, inp1_stride;
	dma_addr_t inp0_addr, inp1_addr;

	/* Input0 specific */
	nd = &neoispd->node[NEOISP_INPUT0_NODE];
	ibpp = (nd->neoisp_format->bit_depth + 7) / 8;
	inp0_stride = nd->format.fmt.pix_mp.plane_fmt[0].bytesperline;

	/* Input0 - Take crop into account if any */
	inp0_addr = get_addr(buf_inp0, 0) + (nd->crop.left * ibpp) + (nd->crop.top * inp0_stride);

	/* Input 1 specific */
	nd = &neoispd->node[NEOISP_INPUT1_NODE];
	ibpp = (nd->neoisp_format->bit_depth + 7) / 8;
	inp1_stride = nd->format.fmt.pix_mp.plane_fmt[0].bytesperline;

	/* Input1 - Take crop into account if any */
	inp1_addr = get_addr(buf_inp1, 0) +
		(nd->crop.left * ibpp) +
		(nd->crop.top * inp1_stride);

	cfg->img0_in_addr =
		FIELD_PREP(NEO_PIPE_CONF_IMG0_IN_ADDR_CAM0_ADDR,
			   NEO_PIPE_CONF_ADDR_CONVERT(inp0_addr));

	/* Handle hdr inputs */
	nd = &neoispd->node[NEOISP_INPUT1_NODE];
	if (neoisp_node_link_is_enabled(nd)) {
		cfg->img1_in_addr =
			FIELD_PREP(NEO_PIPE_CONF_IMG1_IN_ADDR_CAM0_ADDR,
				   NEO_PIPE_CONF_ADDR_CONVERT(inp1_addr));
	}

	nd = &neoispd->node[NEOISP_FRAME_NODE];
	if (neoisp_node_link_is_enabled(nd)) {
		/* Planar/multiplanar output image addresses */
		switch (nd->format.fmt.pix_mp.pixelformat) {
		case V4L2_PIX_FMT_GREY:
		case V4L2_PIX_FMT_Y10:
		case V4L2_PIX_FMT_Y12:
		case V4L2_PIX_FMT_Y16:
		case V4L2_PIX_FMT_Y16_BE:
			/* Monochrome formats: only output channel 0 is used */
			cfg->outch0_addr =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH0_ADDR_CAM0_ADDR,
					   NEO_PIPE_CONF_ADDR_CONVERT(get_addr(buf_out, 0)));
			break;
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV61:
			/* Semi-Planar formats: both output channels are used */
			cfg->outch0_addr =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH0_ADDR_CAM0_ADDR,
					   NEO_PIPE_CONF_ADDR_CONVERT(get_addr(buf_out, 0)));
			cfg->outch1_addr =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH1_ADDR_CAM0_ADDR,
					   NEO_PIPE_CONF_ADDR_CONVERT(get_addr(buf_out, 1)));
			break;
		default:
			/* Interleaved formats: only output channel 1 is used */
			cfg->outch1_addr =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH1_ADDR_CAM0_ADDR,
					   NEO_PIPE_CONF_ADDR_CONVERT(get_addr(buf_out, 0)));
			break;
		}
	}

	nd = &neoispd->node[NEOISP_IR_NODE];
	if (neoisp_node_link_is_enabled(nd))
		cfg->outir_addr =
			FIELD_PREP(NEO_PIPE_CONF_OUTIR_ADDR_CAM0_ADDR,
				   NEO_PIPE_CONF_ADDR_CONVERT(get_addr(buf_ir, 0)));
}

void neoisp_ctx_update_gcm(struct neoisp_dev_s *neoispd,
			   struct neoisp_context_s *context,
			   struct v4l2_pix_format_mplane *pix_mp,
			   enum v4l2_ycbcr_encoding enc)
{
	struct neoisp_gcm_s *gcm = &context->hw.gcm;
	enum v4l2_xfer_func xfer = pix_mp->xfer_func;
	enum v4l2_quantization quant = pix_mp->quantization;

	int i, j;
	s32 value, tmat[NEO_GAMMA_MATRIX_SIZE][NEO_GAMMA_MATRIX_SIZE];
	u32 tmp;

	/*
	 * Colorspaces definition are extracted from kernel documentation:
	 * https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/colorspaces-details.html
	 */

	/* Transfer function */
	gcm->lowth_ctrl01 =
		FIELD_PREP(NEO_GCM_LOWTH_CTRL01_CAM0_THRESHOLD0, xfer_lut[xfer].threshold) |
		FIELD_PREP(NEO_GCM_LOWTH_CTRL01_CAM0_THRESHOLD1, xfer_lut[xfer].threshold);
	gcm->lowth_ctrl2 =
		FIELD_PREP(NEO_GCM_LOWTH_CTRL2_CAM0_THRESHOLD2, xfer_lut[xfer].threshold);
	gcm->gamma0 =
		FIELD_PREP(NEO_GCM_GAMMA0_CAM0_GAMMA0, xfer_lut[xfer].gamma) |
		FIELD_PREP(NEO_GCM_GAMMA0_CAM0_OFFSET0, xfer_lut[xfer].gamma_offset);
	gcm->gamma1 =
		FIELD_PREP(NEO_GCM_GAMMA1_CAM0_GAMMA1, xfer_lut[xfer].gamma) |
		FIELD_PREP(NEO_GCM_GAMMA1_CAM0_OFFSET1, xfer_lut[xfer].gamma_offset);
	gcm->gamma2 =
		FIELD_PREP(NEO_GCM_GAMMA2_CAM0_GAMMA2, xfer_lut[xfer].gamma) |
		FIELD_PREP(NEO_GCM_GAMMA2_CAM0_OFFSET2, xfer_lut[xfer].gamma_offset);

	FIELD_MODIFY(NEO_GCM_BLKLVL0_CTRL_CAM0_GAIN0, &gcm->blklvl0_ctrl,
		     xfer_lut[xfer].blklvl_gain);
	FIELD_MODIFY(NEO_GCM_BLKLVL1_CTRL_CAM0_GAIN1, &gcm->blklvl1_ctrl,
		     xfer_lut[xfer].blklvl_gain);
	FIELD_MODIFY(NEO_GCM_BLKLVL2_CTRL_CAM0_GAIN2, &gcm->blklvl2_ctrl,
		     xfer_lut[xfer].blklvl_gain);

	/*
	 * Quantization
	 *
	 * The quantization is amended by transfer function gain.
	 * The default quantization is full-range for RGB formats and
	 * V4L2_COLORSPACE_JPEG.
	 *
	 * In limited range the offsets are defined by standard as follow: (16, 128, 128)
	 * for 8-bit range while ISP offsets are defined for 12-bit range.
	 * Hence, the offsets defined by standard should be multiplied by 2^4=16:
	 * (256, 2048, 2048) for 12-bit range
	 * The same quantization factors are applied to Y'CbCr for BT.601 and BT.709:
	 * (219*Y, 224*Pb, 224*Pr)
	 */
	tmp = (quant == V4L2_QUANTIZATION_LIM_RANGE) ?
		256 : enc_lut[enc].offsets[0];
	gcm->ooffset0 = FIELD_PREP(NEO_GCM_OOFFSET0_CAM0_OFFSET0, tmp);

	/* Chrominance has the same offset for full or limited range */
	gcm->ooffset1 = FIELD_PREP(NEO_GCM_OOFFSET1_CAM0_OFFSET1, enc_lut[enc].offsets[1]);
	gcm->ooffset2 = FIELD_PREP(NEO_GCM_OOFFSET2_CAM0_OFFSET2, enc_lut[enc].offsets[2]);
	for (i = 0; i < NEO_GAMMA_MATRIX_SIZE; i++) {
		s32 factor = (quant == V4L2_QUANTIZATION_LIM_RANGE) ?
			(i == 0 ? 219 : 224) : 256;
		for (j = 0; j < NEO_GAMMA_MATRIX_SIZE; j++) {
			value = ((s32)enc_lut[enc].matrix[i][j] * factor) / 256;
			value = ((s32)value * (s32)xfer_lut[xfer].gain) / 256;
			tmat[i][j] = (s16)value;
		}
	}
	gcm->omat0 =
		FIELD_PREP(NEO_GCM_OMAT0_CAM0_R0C0, tmat[0][0]) |
		FIELD_PREP(NEO_GCM_OMAT0_CAM0_R0C1, tmat[0][1]);
	gcm->omat1 =
		FIELD_PREP(NEO_GCM_OMAT1_CAM0_R0C2, tmat[0][2]);
	gcm->omat2 =
		FIELD_PREP(NEO_GCM_OMAT2_CAM0_R1C0, tmat[1][0]) |
		FIELD_PREP(NEO_GCM_OMAT2_CAM0_R1C1, tmat[1][1]);
	gcm->omat3 =
		FIELD_PREP(NEO_GCM_OMAT3_CAM0_R1C2, tmat[1][2]);
	gcm->omat4 =
		FIELD_PREP(NEO_GCM_OMAT4_CAM0_R2C0, tmat[2][0]) |
		FIELD_PREP(NEO_GCM_OMAT4_CAM0_R2C1, tmat[2][1]);
	gcm->omat5 =
		FIELD_PREP(NEO_GCM_OMAT5_CAM0_R2C2, tmat[2][2]);
}

void neoisp_ctx_update_hdr_mode(struct neoisp_dev_s *neoispd,
				struct neoisp_context_s *context)
{
	struct neoisp_hdr_merge_s *hmg = &context->hw.hdr_merge;
	struct neoisp_hdr_decompress1_s *hd1 = &context->hw.hdr_decompress1;

	FIELD_MODIFY(NEO_HDR_MERGE_CTRL_CAM0_ENABLE, &hmg->ctrl, 1);
	FIELD_MODIFY(NEO_HDR_DECOMPRESS1_CTRL_CAM0_ENABLE, &hd1->ctrl, 1);
}

/*
 * Set Head Color selection
 */
void neoisp_ctx_update_head_color(struct neoisp_dev_s *neoispd,
				  struct neoisp_context_s *context, u32 pixfmt)
{
	struct neoisp_hc_s *hc = &context->hw.hc;
	u8 hoffset, voffset;

	switch (pixfmt) {
	case (V4L2_PIX_FMT_SRGGB8):
	case (V4L2_PIX_FMT_SRGGB10):
	case (V4L2_PIX_FMT_SRGGB12):
	case (V4L2_PIX_FMT_SRGGB14):
	case (V4L2_PIX_FMT_SRGGB16):
		hoffset = 0;
		voffset = 0;
		break;
	case (V4L2_PIX_FMT_SGRBG8):
	case (V4L2_PIX_FMT_SGRBG10):
	case (V4L2_PIX_FMT_SGRBG12):
	case (V4L2_PIX_FMT_SGRBG14):
	case (V4L2_PIX_FMT_SGRBG16):
		hoffset = 1;
		voffset = 0;
		break;
	case (V4L2_PIX_FMT_SGBRG8):
	case (V4L2_PIX_FMT_SGBRG10):
	case (V4L2_PIX_FMT_SGBRG12):
	case (V4L2_PIX_FMT_SGBRG14):
	case (V4L2_PIX_FMT_SGBRG16):
		hoffset = 0;
		voffset = 1;
		break;
	case (V4L2_PIX_FMT_SBGGR8):
	case (V4L2_PIX_FMT_SBGGR10):
	case (V4L2_PIX_FMT_SBGGR12):
	case (V4L2_PIX_FMT_SBGGR14):
	case (V4L2_PIX_FMT_SBGGR16):
		hoffset = 1;
		voffset = 1;
		break;
	default:
		dev_dbg(neoispd->dev, "Unsupported pixel format %#x\n", pixfmt);
		return;
	}
	hc->ctrl =
		FIELD_PREP(NEO_HC_CTRL_CAM0_HOFFSET, hoffset) |
		FIELD_PREP(NEO_HC_CTRL_CAM0_VOFFSET, voffset);
}

/*
 * Update relevant IP parameters for monochrome sensors
 */
void neoisp_ctx_update_monochrome_fmt(struct neoisp_dev_s *neoispd,
				      struct neoisp_context_s *context, u32 pixfmt)
{
	struct neoisp_demosaic_s *dmsc;
	struct neoisp_bnr_s *bnr;

	dmsc = &context->hw.demosaic;
	bnr = &context->hw.bnr;

	if (format_is_monochrome(pixfmt)) {
		FIELD_MODIFY(NEO_DEMOSAIC_CTRL_CAM0_FMT, &dmsc->ctrl, 2); /* Monochrome format */
		FIELD_MODIFY(NEO_BNR_CTRL_CAM0_NHOOD, &bnr->ctrl, 1); /* 1-pixel Neighbourhood */
	} else {
		FIELD_MODIFY(NEO_DEMOSAIC_CTRL_CAM0_FMT, &dmsc->ctrl, 0); /* Bayer format */
		FIELD_MODIFY(NEO_BNR_CTRL_CAM0_NHOOD, &bnr->ctrl, 0); /* 2-pixel Neighbourhood */
	}
}

void neoisp_ctx_update_packetizer(struct neoisp_dev_s *neoispd)
{
	struct neoisp_node_s *nd = &neoispd->node[NEOISP_FRAME_NODE];
	struct neoisp_packetizer_s *pck = &neoispd->context->hw.packetizer;
	u8 obpp, lsa, rsa, type, order0, order1, order2, a0s, subsample;
	u32 pixfmt;

	if (neoisp_node_link_is_enabled(nd)) {
		pixfmt = nd->format.fmt.pix_mp.pixelformat;
		obpp = nd->neoisp_format->bpp_enc;
	} else {
		/* Force dummy buffer configuration to YUYV format */
		const struct neoisp_fmt_s *fmt =
			neoisp_find_video_capture_format(V4L2_PIX_FMT_YUYV);

		if (!fmt) {
			dev_err(neoispd->dev, "YUYV pixel format not found\n");
			return;
		}

		pixfmt = V4L2_PIX_FMT_YUYV;
		obpp = fmt->bpp_enc;
	}

	switch (pixfmt) {
	case V4L2_PIX_FMT_Y10:
		rsa = 2;
		lsa = 0;
		break;
	case V4L2_PIX_FMT_Y12:
		rsa = 0;
		lsa = 0;
		break;
	case V4L2_PIX_FMT_Y16:
		rsa = 0;
		lsa = 4;
		break;
	default:
		rsa = 4;
		lsa = 0;
		break;
	}

	switch (pixfmt) {
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_Y10:
	case V4L2_PIX_FMT_Y12:
	case V4L2_PIX_FMT_Y16:
	case V4L2_PIX_FMT_Y16_BE:
		type = 0;
		subsample = 2;
		/* Set channels orders */
		order0 = 2;
		order1 = 0;
		order2 = 1;
		/* Remove 0-padding */
		a0s = 0;
		break;
	case V4L2_PIX_FMT_NV21:
		type = 0;
		subsample = 2;
		/* Set channels orders */
		order0 = 2;
		order1 = 1;
		order2 = 0;
		/* Remove 0-padding */
		a0s = 0;
		break;
	case V4L2_PIX_FMT_NV16:
		type = 0;
		subsample = 1;
		/* Set channels orders */
		order0 = 2;
		order1 = 0;
		order2 = 1;
		/* Remove 0-padding */
		a0s = 0;
		break;
	case V4L2_PIX_FMT_NV61:
		type = 0;
		subsample = 1;
		/* Set channels orders */
		order0 = 2;
		order1 = 1;
		order2 = 0;
		/* Remove 0-padding */
		a0s = 0;
		break;
	case V4L2_PIX_FMT_YUYV:
		type = 1;
		subsample = 1;
		/* Set channels orders */
		order0 = 0;
		order1 = 1;
		order2 = 3;
		/* Remove 0-padding */
		a0s = 0;
		break;
	case V4L2_PIX_FMT_VYUY:
		type = 1;
		subsample = 1;
		/* Set channels orders */
		order0 = 1;
		order1 = 2;
		order2 = 0;
		/* Remove 0-padding */
		a0s = 0;
		break;
	case V4L2_PIX_FMT_UYVY:
		type = 1;
		subsample = 1;
		/* Set channels orders */
		order0 = 1;
		order1 = 0;
		order2 = 2;
		/* Remove 0-padding */
		a0s = 0;
		break;
	case V4L2_PIX_FMT_YUVX32:
		type = 1;
		subsample = 0;
		/* Set channels orders */
		order0 = 0;
		order1 = 1;
		order2 = 2;
		/* Add 0-padding */
		a0s = 8;
		break;
	case V4L2_PIX_FMT_VUYX32:
		type = 1;
		subsample = 0;
		/* Set channels orders */
		order0 = 2;
		order1 = 1;
		order2 = 0;
		/* Add 0-padding */
		a0s = 8;
		break;
	case V4L2_PIX_FMT_XBGR32:
		type = 1;
		subsample = 0;
		/* Set channels orders */
		order0 = 2;
		order1 = 1;
		order2 = 0;
		/* Add 0-padding */
		a0s = 8;
		break;
	case V4L2_PIX_FMT_RGBX32:
		type = 1;
		subsample = 0;
		/* Set channels orders */
		order0 = 0;
		order1 = 1;
		order2 = 2;
		/* Add 0-padding */
		a0s = 8;
		break;
	case V4L2_PIX_FMT_BGR24:
		type = 1;
		subsample = 0;
		/* Set channels orders */
		order0 = 2;
		order1 = 1;
		order2 = 0;
		/* Remove 0-padding */
		a0s = 0;
		break;
	default: /* All other pixel formats */
		type = 1;
		subsample = 0;
		/* Set channels orders */
		order0 = 0;
		order1 = 1;
		order2 = 2;
		/* Remove 0-padding */
		a0s = 0;
		break;
	}

	pck->ch0_ctrl =
		FIELD_PREP(NEO_PACKETIZER_CH0_CTRL_CAM0_OBPP, obpp) |
		FIELD_PREP(NEO_PACKETIZER_CH0_CTRL_CAM0_RSA, rsa) |
		FIELD_PREP(NEO_PACKETIZER_CH0_CTRL_CAM0_LSA, lsa);

	/* Keep same ch12 lsa/rsa config. */
	FIELD_MODIFY(NEO_PACKETIZER_CH12_CTRL_CAM0_OBPP, &pck->ch12_ctrl, obpp);
	FIELD_MODIFY(NEO_PACKETIZER_CH12_CTRL_CAM0_SUBSAMPLE, &pck->ch12_ctrl, subsample);

	pck->pack_ctrl =
		FIELD_PREP(NEO_PACKETIZER_PACK_CTRL_CAM0_TYPE, type) |
		FIELD_PREP(NEO_PACKETIZER_PACK_CTRL_CAM0_ORDER0, order0) |
		FIELD_PREP(NEO_PACKETIZER_PACK_CTRL_CAM0_ORDER1, order1) |
		FIELD_PREP(NEO_PACKETIZER_PACK_CTRL_CAM0_ORDER2, order2) |
		FIELD_PREP(NEO_PACKETIZER_PACK_CTRL_CAM0_A0S, a0s);
}

/*
 * Set pipe conf fixed settings: image size, bpp, line stride, and dummy
 * addresses.
 */
void neoisp_ctx_update_pipe_conf(struct neoisp_dev_s *neoispd)
{
	struct neoisp_pipe_conf_s *cfg = &neoispd->context->hw.pipe_conf;
	struct neoisp_node_s *nd;
	u32 width, height, obpp, irbpp, inp0_stride, inp1_stride;

	/* Input0 specific */
	nd = &neoispd->node[NEOISP_INPUT0_NODE];
	width = nd->crop.width;
	height = nd->crop.height;
	inp0_stride = nd->format.fmt.pix_mp.plane_fmt[0].bytesperline;

	FIELD_MODIFY(NEO_PIPE_CONF_IMG_CONF_CAM0_IBPP0,
		     &cfg->img_conf, nd->neoisp_format->bpp_enc);

	/* Input 1 specific */
	nd = &neoispd->node[NEOISP_INPUT1_NODE];
	inp1_stride = nd->format.fmt.pix_mp.plane_fmt[0].bytesperline;

	FIELD_MODIFY(NEO_PIPE_CONF_IMG_CONF_CAM0_IBPP1,
		     &cfg->img_conf, nd->neoisp_format->bpp_enc);

	/* Configure registers */
	cfg->img_size =
		FIELD_PREP(NEO_PIPE_CONF_IMG_SIZE_CAM0_WIDTH, width) |
		FIELD_PREP(NEO_PIPE_CONF_IMG_SIZE_CAM0_HEIGHT, height);
	cfg->img0_in_ls =
		FIELD_PREP(NEO_PIPE_CONF_IMG0_IN_LS_CAM0_LS, inp0_stride);

	/* Handle hdr inputs */
	nd = &neoispd->node[NEOISP_INPUT1_NODE];
	if (neoisp_node_link_is_enabled(nd)) {
		cfg->img1_in_ls =
			FIELD_PREP(NEO_PIPE_CONF_IMG1_IN_LS_CAM0_LS, inp1_stride);
	} else {
		cfg->img1_in_addr =
			FIELD_PREP(NEO_PIPE_CONF_IMG1_IN_ADDR_CAM0_ADDR, 0u);
		cfg->img1_in_ls =
			FIELD_PREP(NEO_PIPE_CONF_IMG1_IN_LS_CAM0_LS, 0u);
	}

	nd = &neoispd->node[NEOISP_FRAME_NODE];
	if (neoisp_node_link_is_enabled(nd)) {
		obpp = (nd->neoisp_format->bit_depth + 7) / 8;

		switch (nd->format.fmt.pix_mp.pixelformat) {
		case V4L2_PIX_FMT_GREY:
		case V4L2_PIX_FMT_Y10:
		case V4L2_PIX_FMT_Y12:
		case V4L2_PIX_FMT_Y16:
		case V4L2_PIX_FMT_Y16_BE:
			/*
			 * Monochrome formats:
			 * - output0 is used for Y component
			 * - output1 on dummy buffer
			 */
			cfg->outch1_addr =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH1_ADDR_CAM0_ADDR,
					   NEO_PIPE_CONF_ADDR_CONVERT(neoispd->dummy_dma));
			cfg->outch0_ls =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH0_LS_CAM0_LS, obpp * width);
			cfg->outch1_ls =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH1_LS_CAM0_LS, 0u);
			break;
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV61:
			/*
			 * Semi-Planar formats:
			 * - output0 is used for Y component
			 * - output1 is used for UV components
			 */
			cfg->outch1_ls =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH1_LS_CAM0_LS, obpp * width);
			cfg->outch0_ls =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH0_LS_CAM0_LS, obpp * width);
			break;
		default:
			/*
			 * Interleaved formats:
			 * - output0 is not used at all
			 * - output1 is used for YUV or RGB components
			 */
			cfg->outch0_addr =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH0_ADDR_CAM0_ADDR, 0u);
			cfg->outch0_ls =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH0_LS_CAM0_LS, 0u);
			cfg->outch1_ls =
				FIELD_PREP(NEO_PIPE_CONF_OUTCH1_LS_CAM0_LS, obpp * width);
			break;
		}
	} else {
		/* Default dummy pixelformat is set to YUYV */
		cfg->outch0_addr =
			FIELD_PREP(NEO_PIPE_CONF_OUTCH0_ADDR_CAM0_ADDR,
				   NEO_PIPE_CONF_ADDR_CONVERT(neoispd->dummy_dma));
		cfg->outch1_addr =
			FIELD_PREP(NEO_PIPE_CONF_OUTCH1_ADDR_CAM0_ADDR,
				   NEO_PIPE_CONF_ADDR_CONVERT(neoispd->dummy_dma));
		cfg->outch0_ls =
			FIELD_PREP(NEO_PIPE_CONF_OUTCH0_LS_CAM0_LS, 0u);
		cfg->outch1_ls =
			FIELD_PREP(NEO_PIPE_CONF_OUTCH1_LS_CAM0_LS, 0u);
	}

	nd = &neoispd->node[NEOISP_IR_NODE];
	if (neoisp_node_link_is_enabled(nd)) {
		irbpp = (nd->neoisp_format->bit_depth + 7) / 8;

		cfg->outir_ls =
			FIELD_PREP(NEO_PIPE_CONF_OUTIR_LS_CAM0_LS, irbpp * width);
	} else {
		cfg->outir_addr =
			FIELD_PREP(NEO_PIPE_CONF_OUTIR_ADDR_CAM0_ADDR,
				   NEO_PIPE_CONF_ADDR_CONVERT(neoispd->dummy_dma));
		cfg->outir_ls =
			FIELD_PREP(NEO_PIPE_CONF_OUTIR_LS_CAM0_LS, 0u);
	}
}

/*
 * neoisp_ctx_update_w_user_params is used to update the context of the
 * queued node with user space values.
 */
void neoisp_ctx_update_w_user_params(struct neoisp_dev_s *neoispd)
{
	struct neoisp_buffer_s *buf = neoispd->queued_job.buf[NEOISP_PARAMS_NODE];
	struct v4l2_isp_buffer *params;
	size_t block_offset = 0, max_offset;

	if (IS_ERR_OR_NULL(buf))
		return;

	params = (struct v4l2_isp_buffer *)get_vaddr(buf);

	if (!params || params->data_size == 0)
		/* No relevant parameters in this buffer */
		return;

	max_offset = params->data_size;

	/*
	 * Walk the list of parameter blocks and process them. No
	 * validation is done here, as the content of the parameters
	 * buffer is already checked when the buffer is queued.
	 */
	while (block_offset < max_offset) {
		const struct neoisp_block_handler_s *block_handler;
		const union neoisp_params_block_u *block;

		block = (const union neoisp_params_block_u *)
			&params->data[block_offset];
		block_offset += block->header.size;

		block_handler = &neoisp_block_handlers[block->header.type];
		block_handler->handler(neoispd->context, block);
	}
}

/*
 * neoisp_upload_context is used to write all parameters to registers and
 * memory.
 *
 * The register copy starts from PIPE_CONF.IMG_CONF offset, up to the latest
 * writable register in AF unit.
 *
 * The memory copy is performed by block, because base addresses of the LUT
 * are not continuous.
 */
void neoisp_ctx_upload_context(struct neoisp_dev_s *neoispd)
{
	struct neoisp_context_s *ctx = neoispd->context;
	u32 *src = (u32 *)&ctx->hw.pipe_conf.img_conf;

	reg_blk_write(neoispd, NEO_PIPE_CONF_IMG_CONF_CAM0, src,
		      NEO_AUTOFOCUS_ROI0_SUM0_CAM0 - NEO_PIPE_CONF_IMG_CONF_CAM0);

	local_mem_blk_write(neoispd, NEO_VIGNETTING_TABLE_MAP,
			    &ctx->vig.vignetting_table);
	local_mem_blk_write(neoispd, NEO_DRC_GLOBAL_TONEMAP_MAP,
			    &ctx->gtm.drc_global_tonemap);
	local_mem_blk_write(neoispd, NEO_DRC_LOCAL_TONEMAP_MAP,
			    &ctx->ltm.drc_local_tonemap);
}

#define NEOISP_STATS_BLOCK_INFO(block, type, ext) \
	[NEOISP_STATS_BLK_## block] = { \
		.size = sizeof(struct neoisp_ ## type ## _ ## ext ## _stats_es), \
	}

#define NEOISP_STATS_BLOCK_INFO_REG(block, type) \
	NEOISP_STATS_BLOCK_INFO(block, type, reg)

#define NEOISP_STATS_BLOCK_INFO_MEM(block, type) \
	NEOISP_STATS_BLOCK_INFO(block, type, mem)

static const struct
v4l2_isp_stats_block_type_info neoisp_stats_block_types_info[] = {
	NEOISP_STATS_BLOCK_INFO_REG(RCTEMP, ctemp),
	NEOISP_STATS_BLOCK_INFO_REG(RDRC, drc),
	NEOISP_STATS_BLOCK_INFO_REG(RAF, af),
	NEOISP_STATS_BLOCK_INFO_REG(RBNR, bnr),
	NEOISP_STATS_BLOCK_INFO_REG(RNR, nr),
	NEOISP_STATS_BLOCK_INFO_REG(REE, ee),
	NEOISP_STATS_BLOCK_INFO_REG(RDF, df),
	NEOISP_STATS_BLOCK_INFO_MEM(MCTEMP, ctemp),
	NEOISP_STATS_BLOCK_INFO_MEM(MRGBIR, rgbir),
	NEOISP_STATS_BLOCK_INFO_MEM(MHIST, hist),
	NEOISP_STATS_BLOCK_INFO_MEM(MDRC, drc),
};

static void neoisp_ctx_get_stats_blk(struct neoisp_dev_s *neoispd, u32 btype,
				     union neoisp_stats_block_u *blk)
{
	switch (btype) {
	case NEOISP_STATS_BLK_RCTEMP:
		reg_blk_read(neoispd, NEO_ALIAS_ALIAS_REG0,
			     (u32 *)&blk->rctemp.stat,
			     sizeof(struct neoisp_ctemp_reg_stats_s));
		break;
	case NEOISP_STATS_BLK_RDRC:
		reg_blk_read(neoispd, NEO_ALIAS_ALIAS_REG59,
			     (u32 *)&blk->rdrc.stat,
			     sizeof(struct neoisp_drc_reg_stats_s));
		break;
	case NEOISP_STATS_BLK_RAF:
		reg_blk_read(neoispd, NEO_ALIAS_ALIAS_REG61,
			     (u32 *)&blk->raf.stat,
			     sizeof(struct neoisp_af_reg_stats_s));
		break;
	case NEOISP_STATS_BLK_RBNR:
		reg_blk_read(neoispd, NEO_ALIAS_ALIAS_REG79,
			     (u32 *)&blk->rbnr.stat,
			     sizeof(struct neoisp_bnr_reg_stats_s));
		break;
	case NEOISP_STATS_BLK_RNR:
		reg_blk_read(neoispd, NEO_ALIAS_ALIAS_REG81,
			     (u32 *)&blk->rnr.stat,
			     sizeof(struct neoisp_nr_reg_stats_s));
		break;
	case NEOISP_STATS_BLK_REE:
		reg_blk_read(neoispd, NEO_ALIAS_ALIAS_REG82,
			     (u32 *)&blk->ree.stat,
			     sizeof(struct neoisp_ee_reg_stats_s));
		break;
	case NEOISP_STATS_BLK_RDF:
		reg_blk_read(neoispd, NEO_ALIAS_ALIAS_REG83,
			     (u32 *)&blk->rdf.stat,
			     sizeof(struct neoisp_df_reg_stats_s));
		break;
	case NEOISP_STATS_BLK_MCTEMP:
		/* Get ctemp stats from memory */
		local_mem_blk_read(neoispd, NEO_CTEMP_R_SUM_MAP,
				   &blk->mctemp.stat.ctemp_r_sum);

		local_mem_blk_read(neoispd, NEO_CTEMP_G_SUM_MAP,
				   &blk->mctemp.stat.ctemp_g_sum);

		local_mem_blk_read(neoispd, NEO_CTEMP_B_SUM_MAP,
				   &blk->mctemp.stat.ctemp_b_sum);

		local_mem_blk_read(neoispd, NEO_CTEMP_PIX_CNT_MAP,
				   &blk->mctemp.stat.ctemp_pix_cnt);
		break;
	case NEOISP_STATS_BLK_MRGBIR:
		/* Get rgbir stats from memory */
		local_mem_blk_read(neoispd, NEO_RGBIR_HIST_MAP,
				   &blk->mrgbir.stat);
		break;
	case NEOISP_STATS_BLK_MHIST:
		/* Get histograms stats from memory */
		local_mem_blk_read(neoispd, NEO_HIST_STAT_MAP,
				   &blk->mhist.stat);
		break;
	case NEOISP_STATS_BLK_MDRC:
		/* Get drc local sum stats from memory */
		local_mem_blk_read(neoispd, NEO_DRC_LOCAL_SUM_MAP,
				   &blk->mdrc.stat.drc_local_sum);

		/* Get drc hist roi0 stats from memory */
		local_mem_blk_read(neoispd, NEO_DRC_GLOBAL_HIST_ROI0_MAP,
				   &blk->mdrc.stat.drc_global_hist_roi0);

		/* Get drc hist roi1 stats from memory */
		local_mem_blk_read(neoispd, NEO_DRC_GLOBAL_HIST_ROI1_MAP,
				   &blk->mdrc.stat.drc_global_hist_roi1);
		break;
	default:
		dev_err(neoispd->dev, "Error: unknown stats block id (%u)\n", btype);
		return;
	}
}

static const __u32 neoisp_stats_blocks[] = {
	NEOISP_STATS_BLK_RCTEMP,
	NEOISP_STATS_BLK_RDRC,
	NEOISP_STATS_BLK_RAF,
	NEOISP_STATS_BLK_RBNR,
	NEOISP_STATS_BLK_RNR,
	NEOISP_STATS_BLK_REE,
	NEOISP_STATS_BLK_RDF,
	NEOISP_STATS_BLK_MCTEMP,
	NEOISP_STATS_BLK_MRGBIR,
	NEOISP_STATS_BLK_MHIST,
	NEOISP_STATS_BLK_MDRC,
};

void neoisp_ctx_get_stats(struct neoisp_dev_s *neoispd, struct neoisp_buffer_s *buf)
{
	struct neoisp_node_s *node = &neoispd->node[NEOISP_STATS_NODE];
	struct v4l2_isp_buffer *stats;
	u32 *blk_list, count;

	/* Check if stats node link is enabled */
	if (!neoisp_node_link_is_enabled(node))
		return;

	if (IS_ERR_OR_NULL(buf)) {
		dev_err(neoispd->dev, "Error: stats pointer\n");
		return;
	}

	stats = (struct v4l2_isp_buffer *)get_vaddr(buf);
	v4l2_isp_stats_init_buffer(stats, V4L2_ISP_VERSION_V1);

	blk_list = (u32 *)neoisp_stats_blocks;
	count = ARRAY_SIZE(neoisp_stats_blocks);
	for (int i = 0; i < count; i++) {
		struct v4l2_isp_block_header *header =
			v4l2_isp_stats_init_block(neoispd->dev, stats,
						  neoisp_stats_block_types_info,
						  ARRAY_SIZE(neoisp_stats_block_types_info),
						  blk_list[i], NEOISP_EXT_STATS_MAX_SIZE);
		if (IS_ERR(header)) {
			dev_err(neoispd->dev, "Error: initializing stats block %d\n", i);
			continue;
		}

		neoisp_ctx_get_stats_blk(neoispd, blk_list[i],
					 (union neoisp_stats_block_u *)header);
	}
}
