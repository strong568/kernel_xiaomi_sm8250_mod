// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, The Linux Foundation. All rights reserved.
 * KCAL - Advanced Color Control for Qualcomm SDE DSPP
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <uapi/drm/msm_drm_pp.h>

#include "sde_kms.h"
#include "sde_crtc.h"
#include "sde_hw_dspp.h"
#include "sde_hw_ctl.h"
#include "sde_hw_lm.h"
#include "sde_hw_mdss.h"
#include "sde_connector.h"
#include "sde_kcal.h"

#define KCAL_UNITY_GAIN 32768U

static DEFINE_MUTEX(kcal_mutex);

static bool kcal_enabled;
static bool kcal_dirty;
static bool kcal_restored = true;

static u32 kcal_red = 256;
static u32 kcal_green = 256;
static u32 kcal_blue = 256;
static u32 kcal_min = 35;
static u32 kcal_sat = 256;
static u32 kcal_val = 256;
static u32 kcal_cont = 256;
static u32 kcal_hue;
static u32 kcal_invert;

static struct drm_msm_pcc kcal_pcc_cfg;

static inline struct sde_kms *kcal_get_kms(struct drm_crtc *crtc)
{
	struct msm_drm_private *priv;

	if (!crtc || !crtc->dev || !crtc->dev->dev_private)
		return NULL;
	priv = crtc->dev->dev_private;
	if (!priv || !priv->kms)
		return NULL;
	return to_sde_kms(priv->kms);
}

static void kcal_recompute_pcc_locked(void)
{
	u32 scale_r, scale_g, scale_b;
	s32 s, c_val, c_cont;
	/* Rec. 709 luminance weights scaled by KCAL_UNITY_GAIN */
	u32 pr = 9798, pg = 19235, pb = 3735;
	s32 crr, crg, crb, cgr, cgg, cgb, cbr, cbg, cbb;

	u32 r = clamp_t(u32, kcal_red, kcal_min, 256);
	u32 g = clamp_t(u32, kcal_green, kcal_min, 256);
	u32 b = clamp_t(u32, kcal_blue, kcal_min, 256);
	u32 sat = clamp_t(u32, kcal_sat, 128, 383);
	u32 val = clamp_t(u32, kcal_val, 128, 383);
	u32 cont = clamp_t(u32, kcal_cont, 128, 383);

	memset(&kcal_pcc_cfg, 0, sizeof(kcal_pcc_cfg));

	/* Scale RGB (0..256 -> 0..32768) */
	scale_r = (r * KCAL_UNITY_GAIN) / 256;
	scale_g = (g * KCAL_UNITY_GAIN) / 256;
	scale_b = (b * KCAL_UNITY_GAIN) / 256;

	if (sat == 256) {
		crr = KCAL_UNITY_GAIN; crg = 0; crb = 0;
		cgr = 0; cgg = KCAL_UNITY_GAIN; cgb = 0;
		cbr = 0; cbg = 0; cbb = KCAL_UNITY_GAIN;
	} else {
		/* 128: Grayscale (0.0), 256: Normal (1.0), 383: Boost (~2.0) */
		s = (s32)sat - 128;
		crr = ((128 - s) * (s32)pr + s * (s32)KCAL_UNITY_GAIN) / 128;
		crg = ((128 - s) * (s32)pg) / 128;
		crb = ((128 - s) * (s32)pb) / 128;

		cgr = ((128 - s) * (s32)pr) / 128;
		cgg = ((128 - s) * (s32)pg + s * (s32)KCAL_UNITY_GAIN) / 128;
		cgb = ((128 - s) * (s32)pb) / 128;

		cbr = ((128 - s) * (s32)pr) / 128;
		cbg = ((128 - s) * (s32)pg) / 128;
		cbb = ((128 - s) * (s32)pb + s * (s32)KCAL_UNITY_GAIN) / 128;
	}

	/* Multiply color matrix with RGB scale */
	kcal_pcc_cfg.r.r = clamp_val((crr * (s32)scale_r) / (s32)KCAL_UNITY_GAIN, 0, 65535);
	kcal_pcc_cfg.r.g = clamp_val((crg * (s32)scale_r) / (s32)KCAL_UNITY_GAIN, 0, 65535);
	kcal_pcc_cfg.r.b = clamp_val((crb * (s32)scale_r) / (s32)KCAL_UNITY_GAIN, 0, 65535);

	kcal_pcc_cfg.g.r = clamp_val((cgr * (s32)scale_g) / (s32)KCAL_UNITY_GAIN, 0, 65535);
	kcal_pcc_cfg.g.g = clamp_val((cgg * (s32)scale_g) / (s32)KCAL_UNITY_GAIN, 0, 65535);
	kcal_pcc_cfg.g.b = clamp_val((cgb * (s32)scale_g) / (s32)KCAL_UNITY_GAIN, 0, 65535);

	kcal_pcc_cfg.b.r = clamp_val((cbr * (s32)scale_b) / (s32)KCAL_UNITY_GAIN, 0, 65535);
	kcal_pcc_cfg.b.g = clamp_val((cbg * (s32)scale_b) / (s32)KCAL_UNITY_GAIN, 0, 65535);
	kcal_pcc_cfg.b.b = clamp_val((cbb * (s32)scale_b) / (s32)KCAL_UNITY_GAIN, 0, 65535);

	/* Contrast adjustment (128..383, 256 is 1.0) */
	if (cont != 256) {
		c_cont = (s32)cont;
		kcal_pcc_cfg.r.r = clamp_val((kcal_pcc_cfg.r.r * c_cont) / 256, 0, 65535);
		kcal_pcc_cfg.g.g = clamp_val((kcal_pcc_cfg.g.g * c_cont) / 256, 0, 65535);
		kcal_pcc_cfg.b.b = clamp_val((kcal_pcc_cfg.b.b * c_cont) / 256, 0, 65535);
	}

	/* Value / Brightness offset (128..383, 256 is 0) */
	if (val != 256) {
		c_val = (((s32)val - 256) * (s32)KCAL_UNITY_GAIN) / 256;
		kcal_pcc_cfg.r.c = (u32)clamp_val(c_val, -32768, 32767);
		kcal_pcc_cfg.g.c = (u32)clamp_val(c_val, -32768, 32767);
		kcal_pcc_cfg.b.c = (u32)clamp_val(c_val, -32768, 32767);
	}

	if (kcal_invert) {
		kcal_pcc_cfg.r.r = 65535 - kcal_pcc_cfg.r.r;
		kcal_pcc_cfg.g.g = 65535 - kcal_pcc_cfg.g.g;
		kcal_pcc_cfg.b.b = 65535 - kcal_pcc_cfg.b.b;
		kcal_pcc_cfg.r.c = (u32)clamp_val((s32)kcal_pcc_cfg.r.c + 32767, -32768, 32767);
		kcal_pcc_cfg.g.c = (u32)clamp_val((s32)kcal_pcc_cfg.g.c + 32767, -32768, 32767);
		kcal_pcc_cfg.b.c = (u32)clamp_val((s32)kcal_pcc_cfg.b.c + 32767, -32768, 32767);
	}
}

void sde_kcal_notify_dirty(void)
{
	mutex_lock(&kcal_mutex);
	if (kcal_enabled)
		kcal_dirty = true;
	mutex_unlock(&kcal_mutex);
}

void sde_kcal_apply(struct drm_crtc *crtc)
{
	struct sde_crtc *sde_crtc;
	struct sde_hw_cp_cfg hw_cfg;
	struct drm_msm_pcc identity_pcc;
	struct sde_kms *kms;
	struct sde_mdss_cfg *catalog;
	u32 i, num_mixers;
	bool apply_identity = false;

	if (!crtc)
		return;

	sde_crtc = to_sde_crtc(crtc);
	if (!sde_crtc || !sde_crtc->enabled)
		return;

	/* Safety Guard 1: Never alter display while FOD fingerprint illumination is active */
	if (sde_crtc->mi_dimlayer_type & MI_DIMLAYER_FOD_HBM_OVERLAY) {
		if (kcal_enabled)
			kcal_dirty = true;
		return;
	}

	mutex_lock(&kcal_mutex);

	/* Safety Guard 2: If disabled, only restore identity once if needed, then skip */
	if (!kcal_enabled) {
		if (kcal_restored) {
			mutex_unlock(&kcal_mutex);
			return;
		}
		apply_identity = true;
	} else if (!kcal_dirty) {
		/* KCAL is enabled but values have not changed: do not overwrite native Android color/night-light */
		mutex_unlock(&kcal_mutex);
		return;
	}

	kms = kcal_get_kms(crtc);
	if (!kms || !kms->catalog) {
		mutex_unlock(&kcal_mutex);
		return;
	}
	catalog = kms->catalog;

	num_mixers = sde_crtc->num_mixers;
	if (!num_mixers) {
		mutex_unlock(&kcal_mutex);
		return;
	}

	memset(&hw_cfg, 0, sizeof(hw_cfg));
	hw_cfg.len = sizeof(struct drm_msm_pcc);
	hw_cfg.num_of_mixers = num_mixers;
	hw_cfg.broadcast_disabled = catalog->dma_cfg.broadcast_disabled;

	for (i = 0; i < num_mixers; i++) {
		if (i >= DSPP_MAX)
			break;
		hw_cfg.dspp[i] = sde_crtc->mixers[i].hw_dspp;
	}

	if (apply_identity) {
		memset(&identity_pcc, 0, sizeof(identity_pcc));
		identity_pcc.r.r = KCAL_UNITY_GAIN;
		identity_pcc.g.g = KCAL_UNITY_GAIN;
		identity_pcc.b.b = KCAL_UNITY_GAIN;
		hw_cfg.payload = &identity_pcc;
	} else {
		hw_cfg.payload = &kcal_pcc_cfg;
	}

	for (i = 0; i < num_mixers; i++) {
		struct sde_hw_dspp *hw_dspp = sde_crtc->mixers[i].hw_dspp;
		struct sde_hw_ctl *ctl = sde_crtc->mixers[i].hw_ctl;
		struct sde_hw_mixer *hw_lm = sde_crtc->mixers[i].hw_lm;

		if (!hw_dspp || !hw_dspp->ops.setup_pcc || !ctl || !hw_lm)
			continue;

		hw_cfg.ctl = ctl;
		hw_cfg.mixer_info = hw_lm;
		hw_cfg.displayh = num_mixers * hw_lm->cfg.out_width;
		hw_cfg.displayv = hw_lm->cfg.out_height;
		hw_cfg.mi_dimlayer_type = sde_crtc->mi_dimlayer_type;

		hw_dspp->ops.setup_pcc(hw_dspp, &hw_cfg);

		if (ctl->ops.update_bitmask_dspp)
			ctl->ops.update_bitmask_dspp(ctl, hw_dspp->idx, 1);
	}

	if (apply_identity) {
		kcal_restored = true;
		pr_info("[KCAL] reset to stock identity matrix\n");
	} else if (kcal_dirty) {
		kcal_restored = false;
		pr_info_ratelimited("[KCAL] applied: R=%u G=%u B=%u sat=%u val=%u cont=%u\n",
			kcal_red, kcal_green, kcal_blue, kcal_sat, kcal_val, kcal_cont);
	}

	kcal_dirty = false;
	mutex_unlock(&kcal_mutex);
}

void sde_kcal_modify_pcc(struct sde_crtc *sde_crtc, struct sde_hw_cp_cfg *hw_cfg)
{
	struct drm_msm_pcc *pcc;

	if (!sde_crtc || !hw_cfg)
		return;

	/* Never alter display during FOD fingerprint illumination */
	if (sde_crtc->mi_dimlayer_type & MI_DIMLAYER_FOD_HBM_OVERLAY)
		return;

	mutex_lock(&kcal_mutex);
	if (!kcal_enabled) {
		mutex_unlock(&kcal_mutex);
		return;
	}

	/*
	 * When KCAL is enabled, blend/scale with the incoming Android color matrix
	 * (e.g. Night Light / LiveDisplay) instead of dropping it.
	 */
	if (hw_cfg->payload) {
		struct drm_msm_pcc in_pcc;

		pcc = (struct drm_msm_pcc *)hw_cfg->payload;
		in_pcc = *pcc;

		pcc->r.r = clamp_val(((s64)kcal_pcc_cfg.r.r * in_pcc.r.r + (s64)kcal_pcc_cfg.r.g * in_pcc.g.r + (s64)kcal_pcc_cfg.r.b * in_pcc.b.r) / KCAL_UNITY_GAIN, 0, 65535);
		pcc->r.g = clamp_val(((s64)kcal_pcc_cfg.r.r * in_pcc.r.g + (s64)kcal_pcc_cfg.r.g * in_pcc.g.g + (s64)kcal_pcc_cfg.r.b * in_pcc.b.g) / KCAL_UNITY_GAIN, 0, 65535);
		pcc->r.b = clamp_val(((s64)kcal_pcc_cfg.r.r * in_pcc.r.b + (s64)kcal_pcc_cfg.r.g * in_pcc.g.b + (s64)kcal_pcc_cfg.r.b * in_pcc.b.b) / KCAL_UNITY_GAIN, 0, 65535);

		pcc->g.r = clamp_val(((s64)kcal_pcc_cfg.g.r * in_pcc.r.r + (s64)kcal_pcc_cfg.g.g * in_pcc.g.r + (s64)kcal_pcc_cfg.g.b * in_pcc.b.r) / KCAL_UNITY_GAIN, 0, 65535);
		pcc->g.g = clamp_val(((s64)kcal_pcc_cfg.g.r * in_pcc.r.g + (s64)kcal_pcc_cfg.g.g * in_pcc.g.g + (s64)kcal_pcc_cfg.g.b * in_pcc.b.g) / KCAL_UNITY_GAIN, 0, 65535);
		pcc->g.b = clamp_val(((s64)kcal_pcc_cfg.g.r * in_pcc.r.b + (s64)kcal_pcc_cfg.g.g * in_pcc.g.b + (s64)kcal_pcc_cfg.g.b * in_pcc.b.b) / KCAL_UNITY_GAIN, 0, 65535);

		pcc->b.r = clamp_val(((s64)kcal_pcc_cfg.b.r * in_pcc.r.r + (s64)kcal_pcc_cfg.b.g * in_pcc.g.r + (s64)kcal_pcc_cfg.b.b * in_pcc.b.r) / KCAL_UNITY_GAIN, 0, 65535);
		pcc->b.g = clamp_val(((s64)kcal_pcc_cfg.b.r * in_pcc.r.g + (s64)kcal_pcc_cfg.b.g * in_pcc.g.g + (s64)kcal_pcc_cfg.b.b * in_pcc.b.g) / KCAL_UNITY_GAIN, 0, 65535);
		pcc->b.b = clamp_val(((s64)kcal_pcc_cfg.b.r * in_pcc.r.b + (s64)kcal_pcc_cfg.b.g * in_pcc.g.b + (s64)kcal_pcc_cfg.b.b * in_pcc.b.b) / KCAL_UNITY_GAIN, 0, 65535);

		pcc->r.c = (u32)clamp_val((s64)(s32)in_pcc.r.c + (s32)kcal_pcc_cfg.r.c, -32768, 32767);
		pcc->g.c = (u32)clamp_val((s64)(s32)in_pcc.g.c + (s32)kcal_pcc_cfg.g.c, -32768, 32767);
		pcc->b.c = (u32)clamp_val((s64)(s32)in_pcc.b.c + (s32)kcal_pcc_cfg.b.c, -32768, 32767);
	} else {
		hw_cfg->payload = &kcal_pcc_cfg;
	}
	hw_cfg->payload_clear = NULL;
	mutex_unlock(&kcal_mutex);
}

/* Sysfs interface matching standard KCAL apps */
static ssize_t kcal_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u %u %u\n", kcal_red, kcal_green, kcal_blue);
}

static ssize_t kcal_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 r, g, b;

	if (sscanf(buf, "%u %u %u", &r, &g, &b) != 3)
		return -EINVAL;

	mutex_lock(&kcal_mutex);
	kcal_red = clamp_t(u32, r, kcal_min, 256);
	kcal_green = clamp_t(u32, g, kcal_min, 256);
	kcal_blue = clamp_t(u32, b, kcal_min, 256);
	kcal_recompute_pcc_locked();
	kcal_dirty = true;
	kcal_enabled = true;
	kcal_restored = false;
	mutex_unlock(&kcal_mutex);

	return count;
}
static DEVICE_ATTR_RW(kcal);

static ssize_t kcal_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", kcal_enabled ? 1 : 0);
}

static ssize_t kcal_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	bool enable;

	if (strtobool(buf, &enable) < 0) {
		u32 val;
		if (kstrtou32(buf, 0, &val))
			return -EINVAL;
		enable = (val != 0);
	}

	mutex_lock(&kcal_mutex);
	if (kcal_enabled != enable) {
		kcal_enabled = enable;
		if (kcal_enabled) {
			kcal_recompute_pcc_locked();
			kcal_dirty = true;
			kcal_restored = false;
		} else {
			kcal_dirty = true;
			kcal_restored = false;
		}
	}
	mutex_unlock(&kcal_mutex);

	return count;
}
static DEVICE_ATTR_RW(kcal_enable);

static ssize_t kcal_min_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", kcal_min);
}

static ssize_t kcal_min_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	mutex_lock(&kcal_mutex);
	kcal_min = clamp_t(u32, val, 0, 256);
	mutex_unlock(&kcal_mutex);

	return count;
}
static DEVICE_ATTR_RW(kcal_min);

static ssize_t kcal_sat_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", kcal_sat);
}

static ssize_t kcal_sat_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	mutex_lock(&kcal_mutex);
	kcal_sat = clamp_t(u32, val, 128, 383);
	kcal_recompute_pcc_locked();
	kcal_dirty = true;
	kcal_enabled = true;
	kcal_restored = false;
	mutex_unlock(&kcal_mutex);

	return count;
}
static DEVICE_ATTR_RW(kcal_sat);

static ssize_t kcal_val_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", kcal_val);
}

static ssize_t kcal_val_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	mutex_lock(&kcal_mutex);
	kcal_val = clamp_t(u32, val, 128, 383);
	kcal_recompute_pcc_locked();
	kcal_dirty = true;
	kcal_enabled = true;
	kcal_restored = false;
	mutex_unlock(&kcal_mutex);

	return count;
}
static DEVICE_ATTR_RW(kcal_val);

static ssize_t kcal_cont_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", kcal_cont);
}

static ssize_t kcal_cont_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	mutex_lock(&kcal_mutex);
	kcal_cont = clamp_t(u32, val, 128, 383);
	kcal_recompute_pcc_locked();
	kcal_dirty = true;
	kcal_enabled = true;
	kcal_restored = false;
	mutex_unlock(&kcal_mutex);

	return count;
}
static DEVICE_ATTR_RW(kcal_cont);

static ssize_t kcal_hue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", kcal_hue);
}

static ssize_t kcal_hue_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	mutex_lock(&kcal_mutex);
	kcal_hue = clamp_t(u32, val, 0, 1536);
	kcal_dirty = true;
	kcal_enabled = true;
	kcal_restored = false;
	mutex_unlock(&kcal_mutex);

	return count;
}
static DEVICE_ATTR_RW(kcal_hue);

static ssize_t kcal_invert_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", kcal_invert);
}

static ssize_t kcal_invert_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	bool val;

	if (strtobool(buf, &val) < 0)
		return -EINVAL;

	mutex_lock(&kcal_mutex);
	kcal_invert = val ? 1 : 0;
	kcal_recompute_pcc_locked();
	kcal_dirty = true;
	kcal_enabled = true;
	kcal_restored = false;
	mutex_unlock(&kcal_mutex);

	return count;
}
static DEVICE_ATTR_RW(kcal_invert);

static struct attribute *kcal_attrs[] = {
	&dev_attr_kcal.attr,
	&dev_attr_kcal_enable.attr,
	&dev_attr_kcal_min.attr,
	&dev_attr_kcal_sat.attr,
	&dev_attr_kcal_val.attr,
	&dev_attr_kcal_cont.attr,
	&dev_attr_kcal_hue.attr,
	&dev_attr_kcal_invert.attr,
	NULL,
};

static umode_t kcal_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	if (attr == &dev_attr_kcal_min.attr)
		return 0444;
	return 0664;
}

static const struct attribute_group kcal_attr_group = {
	.is_visible = kcal_is_visible,
	.attrs = kcal_attrs,
};

static struct platform_device kcal_device = {
	.name = "kcal_ctrl",
	.id = 0,
};

static int __init sde_kcal_init(void)
{
	int ret;

	ret = platform_device_register(&kcal_device);
	if (ret) {
		pr_err("[KCAL] failed to register platform device: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&kcal_device.dev.kobj, &kcal_attr_group);
	if (ret) {
		pr_err("[KCAL] failed to create sysfs group: %d\n", ret);
		platform_device_unregister(&kcal_device);
		return ret;
	}

	mutex_lock(&kcal_mutex);
	kcal_recompute_pcc_locked();
	mutex_unlock(&kcal_mutex);

	pr_info("[KCAL] driver initialized successfully (default: enabled=%d, 256 256 256)\n",
		kcal_enabled);
	return 0;
}
late_initcall(sde_kcal_init);

MODULE_DESCRIPTION("KCAL Color Calibration for Qualcomm SDE");
MODULE_LICENSE("GPL v2");
