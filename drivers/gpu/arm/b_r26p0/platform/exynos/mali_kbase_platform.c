/* drivers/gpu/arm/.../platform/mali_kbase_platform.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali-T Series platform-dependent codes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_platform.c
 * Platform-dependent init.
 */

#include <mali_kbase.h>

#include "mali_kbase_platform.h"
#include "gpu_custom_interface.h"
#include "gpu_dvfs_handler.h"
#include "gpu_notifier.h"
#include "gpu_dvfs_governor.h"
#include "gpu_control.h"

#ifdef CONFIG_OF
#include <linux/of.h>
#endif
#ifdef CONFIG_MALI_DVFS
#ifdef CONFIG_CAL_IF
#include <soc/samsung/cal-if.h>
#endif
static gpu_dvfs_info gpu_dvfs_table_default[DVFS_TABLE_ROW_MAX];
#endif

#include <linux/of_platform.h>

#ifdef CONFIG_EXYNOS9630_BTS
#include <soc/samsung/bts.h>
#endif

struct kbase_device *pkbdev;
static int gpu_debug_level;
static int gpu_trace_level;

struct kbase_device *gpu_get_device_structure(void)
{
	return pkbdev;
}

void gpu_set_debug_level(int level)
{
	gpu_debug_level = level;
}

int gpu_get_debug_level(void)
{
	return gpu_debug_level;
}

#ifdef CONFIG_MALI_EXYNOS_TRACE
struct kbase_ktrace_msg exynos_trace_rbuf[KBASE_KTRACE_SIZE];
extern const struct file_operations kbasep_ktrace_debugfs_fops;
static int gpu_trace_init(struct kbase_device *kbdev)
{
	kbdev->ktrace.rbuf = exynos_trace_rbuf;

	spin_lock_init(&kbdev->ktrace.lock);

/* below work : register entry from making debugfs create file to trace_dentry
 * is same work as kbasep_trace_debugfs_init */
#ifdef MALI_SEC_INTEGRATION
	kbdev->trace_dentry = debugfs_create_file("mali_trace", S_IRUGO,
			kbdev->mali_debugfs_directory, kbdev,
			&kbasep_ktrace_debugfs_fops);
#endif /* MALI_SEC_INTEGRATION */
	return 0;
}

void gpu_set_trace_level(int level)
{
	int i;

	if (level == TRACE_ALL) {
		for (i = TRACE_NONE + 1; i < TRACE_ALL; i++)
			gpu_trace_level |= (1U << i);
	} else if (level == TRACE_NONE) {
		gpu_trace_level = TRACE_NONE;
	} else {
		gpu_trace_level |= (1U << level);
	}
}

bool gpu_check_trace_level(int level)
{
	if (gpu_trace_level & (1U << level))
		return true;
	return false;
}

bool gpu_check_trace_code(int code)
{
	int level;
	switch (code) {
	case KBASE_KTRACE_CODE(DUMMY):
		return false;
	case KBASE_KTRACE_CODE(LSI_CLOCK_VALUE):
	case KBASE_KTRACE_CODE(LSI_GPU_MAX_LOCK):
	case KBASE_KTRACE_CODE(LSI_GPU_MIN_LOCK):
	case KBASE_KTRACE_CODE(LSI_SECURE_WORLD_ENTER):
	case KBASE_KTRACE_CODE(LSI_SECURE_WORLD_EXIT):
	case KBASE_KTRACE_CODE(LSI_SECURE_CACHE):
	case KBASE_KTRACE_CODE(LSI_SECURE_CACHE_END):
	case KBASE_KTRACE_CODE(LSI_KBASE_PM_INIT_HW):
	case KBASE_KTRACE_CODE(LSI_RESUME_CHECK):
	case KBASE_KTRACE_CODE(LSI_RESUME_FREQ):
	case KBASE_KTRACE_CODE(LSI_IFPM_POWER_ON):
	case KBASE_KTRACE_CODE(LSI_IFPM_POWER_OFF):
		level = TRACE_CLK;
		break;
		level = TRACE_VOL;
		break;
	case KBASE_KTRACE_CODE(LSI_GPU_ON):
	case KBASE_KTRACE_CODE(LSI_GPU_OFF):
	case KBASE_KTRACE_CODE(LSI_RESET_GPU_EARLY_DUPE):
	case KBASE_KTRACE_CODE(LSI_RESET_RACE_DETECTED_EARLY_OUT):
	case KBASE_KTRACE_CODE(LSI_PM_SUSPEND):
	case KBASE_KTRACE_CODE(LSI_RESUME):
	case KBASE_KTRACE_CODE(LSI_GPU_RPM_RESUME_API):
	case KBASE_KTRACE_CODE(LSI_GPU_RPM_SUSPEND_API):
	case KBASE_KTRACE_CODE(LSI_SUSPEND_CALLBACK):
	case KBASE_KTRACE_CODE(KBASE_DEVICE_SUSPEND):
	case KBASE_KTRACE_CODE(KBASE_DEVICE_RESUME):
	case KBASE_KTRACE_CODE(KBASE_DEVICE_PM_WAIT_WQ_RUN):
	case KBASE_KTRACE_CODE(KBASE_DEVICE_PM_WAIT_WQ_QUEUE_WORK):
	case KBASE_KTRACE_CODE(LSI_TMU_VALUE):
		level = TRACE_NOTIFIER;
		break;
	case KBASE_KTRACE_CODE(LSI_REGISTER_DUMP):
		level = TRACE_DUMP;
		break;
	default:
		level = TRACE_DEFAULT;
		break;
	}
	return gpu_check_trace_level(level);
}
#endif /* CONFIG_MALI_EXYNOS_TRACE */

void gpu_update_config_data_bool(struct device_node *np, const char *of_string, bool *of_data)
{
	int of_data_int;

	if (!of_string || !of_data) {
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "NULL: failed to get item from dt\n");
		return;
	}

	if (of_property_read_u32(np, of_string, &of_data_int)) {
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "%s: failed to get item from dt. Data will be set to 0.\n", of_string);
		of_data_int = 0;
	} else {
		GPU_LOG(DVFS_INFO, DUMMY, 0u, 0u, "%s: %d\n", of_string, of_data_int);
	}

	*of_data = (bool)of_data_int;

	return;
}

void gpu_update_config_data_int(struct device_node *np, const char *of_string, int *of_data)
{
	if (!of_string || !of_data) {
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "NULL: failed to get item from dt\n");
		return;
	}

	if (of_property_read_u32(np, of_string, of_data)) {
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "%s: failed to get item from dt. Data will be set to 0.\n", of_string);
		*of_data = 0;
	} else {
		GPU_LOG(DVFS_INFO, DUMMY, 0u, 0u, "%s: %d\n", of_string, *of_data);
	}
}

void gpu_update_config_data_string(struct device_node *np, const char *of_string, const char **of_data)
{
	if (!of_string || !of_data) {
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "NULL: failed to get item from dt\n");
		return;
	}

	if (of_property_read_string(np, of_string, of_data)) {
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "%s: failed to get item from dt. Data will be set to NULL.\n", of_string);
		*of_data = NULL;
	} else {
		GPU_LOG(DVFS_INFO, DUMMY, 0u, 0u, "%s: %s\n", of_string, *of_data);
	}
}

void gpu_update_config_data_int_array(struct device_node *np, const char *of_string, int *of_data, int sz)
{
	int i;

	if (!of_string || !of_data) {
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "NULL: failed to get item from dt\n");
		return;
	}

	if (sz > OF_DATA_NUM_MAX) {
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "size overflow(%d): failed to get item from dt\n", sz);
		return;
	}

	if (of_property_read_u32_array(np, of_string, of_data, sz)) {
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "%s: failed to get item from dt\n", of_string);
	} else {
		GPU_LOG(DVFS_INFO, DUMMY, 0u, 0u, "[%s]", of_string);
		for (i = 0; i < sz; i++) {
			if (i % 7 == 0)
				GPU_LOG(DVFS_INFO, DUMMY, 0u, 0u, "\n");
			GPU_LOG(DVFS_INFO, DUMMY, 0u, 0u, "%d\t\n", of_data[i]);
		}
		GPU_LOG(DVFS_INFO, DUMMY, 0u, 0u, "\n");
	}
}
static int gpu_dvfs_update_config_data_from_dt(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_DVFS
	int i;
	int of_data_int_array[OF_DATA_NUM_MAX];
	int of_data_int;
	const char *of_string;
#endif
	struct device_node *np = kbdev->dev->of_node;
	struct exynos_context *platform = (struct exynos_context *) kbdev->platform_context;

	gpu_update_config_data_int(np, "gpu_debug_level", &gpu_debug_level);

#ifdef CONFIG_MALI_EXYNOS_TRACE
	gpu_update_config_data_int(np, "gpu_trace_level", &gpu_trace_level);
	gpu_set_trace_level(gpu_trace_level);
#endif

#ifdef CONFIG_MALI_DVFS
	gpu_update_config_data_int(np, "g3d_cmu_cal_id", &platform->g3d_cmu_cal_id);
	gpu_update_config_data_string(np, "governor", &of_string);
	if (!strncmp("interactive", of_string, strlen("interactive"))) {
		platform->governor_type = G3D_DVFS_GOVERNOR_INTERACTIVE;
		gpu_update_config_data_int_array(np, "interactive_info", of_data_int_array, 3);
		platform->interactive.highspeed_clock = of_data_int_array[0] == 0 ? 500 : (u32) of_data_int_array[0];
		platform->interactive.highspeed_load  = of_data_int_array[1] == 0 ? 100 : (u32) of_data_int_array[1];
		platform->interactive.highspeed_delay = of_data_int_array[2] == 0 ? 0 : (u32) of_data_int_array[2];
	} else if (!strncmp("static", of_string, strlen("static"))) {
		platform->governor_type = G3D_DVFS_GOVERNOR_STATIC;
	} else if (!strncmp("booster", of_string, strlen("booster"))) {
		platform->governor_type = G3D_DVFS_GOVERNOR_BOOSTER;
	} else if (!strncmp("dynamic", of_string, strlen("dynamic"))) {
		platform->governor_type = G3D_DVFS_GOVERNOR_DYNAMIC;
	} else {
		platform->governor_type = G3D_DVFS_GOVERNOR_DEFAULT;
	}

#ifdef CONFIG_CAL_IF
	platform->gpu_dvfs_start_clock = cal_dfs_get_boot_freq(platform->g3d_cmu_cal_id);
	GPU_LOG(DVFS_INFO, DUMMY, 0u, 0u, "get g3d start clock from ect : %d\n", platform->gpu_dvfs_start_clock);
#else
	gpu_update_config_data_int(np, "gpu_dvfs_start_clock", &platform->gpu_dvfs_start_clock);
#endif
	gpu_update_config_data_int_array(np, "gpu_dvfs_table_size", of_data_int_array, 2);
	for (i = 0; i < G3D_MAX_GOVERNOR_NUM; i++) {
		gpu_dvfs_update_start_clk(i, platform->gpu_dvfs_start_clock);
		gpu_dvfs_update_table(i, gpu_dvfs_table_default);
		gpu_dvfs_update_table_size(i, of_data_int_array[0]);
	}

	gpu_update_config_data_int(np, "gpu_pmqos_cpu_cluster_num", &platform->gpu_pmqos_cpu_cluster_num);
	gpu_update_config_data_int(np, "gpu_max_clock", &platform->gpu_max_clock);
#ifdef CONFIG_CAL_IF
	platform->gpu_max_clock_limit = (int)cal_dfs_get_max_freq(platform->g3d_cmu_cal_id);
	if (platform->gpu_max_clock_limit < 1300000||platform->gpu_max_clock < 1300000)
	platform->gpu_max_clock=platform->gpu_max_clock_limit=1300000;
        platform->interactive.highspeed_clock = 1100000;
        platform->interactive.highspeed_load = 75;
#else
	gpu_update_config_data_int(np, "gpu_max_clock_limit", &platform->gpu_max_clock_limit);
#endif
	gpu_update_config_data_int(np, "gpu_min_clock", &platform->gpu_min_clock);
        gpu_update_config_data_int(np, "gpu_min_clock_limit", &platform->gpu_min_clock_limit);
	gpu_update_config_data_int(np, "gpu_dvfs_bl_config_clock", &platform->gpu_dvfs_config_clock);
	gpu_update_config_data_int(np, "gpu_default_voltage", &platform->gpu_default_vol);
	gpu_update_config_data_int(np, "gpu_cold_minimum_vol", &platform->cold_min_vol);
	gpu_update_config_data_int(np, "gpu_voltage_offset_margin", &platform->gpu_default_vol_margin);
	gpu_update_config_data_bool(np, "gpu_tmu_control", &platform->tmu_status);
	if (platform->tmu_status==1)
		platform->tmu_status=1;
	gpu_update_config_data_int(np, "gpu_temp_throttling_level_num", &of_data_int);
	if (of_data_int == TMU_LOCK_CLK_END)
		gpu_update_config_data_int_array(np, "gpu_temp_throttling", platform->tmu_lock_clk, TMU_LOCK_CLK_END);
	else
		GPU_LOG(DVFS_WARNING, DUMMY, 0u, 0u, "mismatch tmu lock table size: %d, %d\n",
				of_data_int, TMU_LOCK_CLK_END);
	platform->tmu_lock_clk[0]=1300000;
	platform->tmu_lock_clk[1]=1200000;
	platform->tmu_lock_clk[2]=1200000;
	platform->tmu_lock_clk[3]=1100000;
	platform->tmu_lock_clk[4]=1001000;
#ifdef CONFIG_CPU_THERMAL_IPA
	gpu_update_config_data_int(np, "gpu_power_coeff", &platform->ipa_power_coeff_gpu);
	gpu_update_config_data_int(np, "gpu_dvfs_time_interval", &platform->gpu_dvfs_time_interval);
#endif /* CONFIG_CPU_THERMAL_IPA */
	gpu_update_config_data_bool(np, "gpu_default_wakeup_lock", &platform->wakeup_lock);
	gpu_update_config_data_bool(np, "gpu_dynamic_abb", &platform->dynamic_abb_status);
	gpu_update_config_data_int(np, "gpu_dvfs_polling_time", &platform->polling_speed);
	gpu_update_config_data_bool(np, "gpu_pmqos_int_disable", &platform->pmqos_int_disable);
	gpu_update_config_data_int(np, "gpu_pmqos_mif_max_clock", &platform->pmqos_mif_max_clock);
	gpu_update_config_data_int(np, "gpu_pmqos_mif_max_clock_base", &platform->pmqos_mif_max_clock_base);
	gpu_update_config_data_int(np, "gpu_cl_dvfs_start_base", &platform->cl_dvfs_start_base);
#endif /* CONFIG_MALI_DVFS */
	gpu_update_config_data_bool(np, "gpu_early_clk_gating", &platform->early_clk_gating_status);
#ifdef CONFIG_MALI_RT_PM
	gpu_update_config_data_bool(np, "gpu_dvs", &platform->dvs_status);
	gpu_update_config_data_bool(np, "gpu_inter_frame_pm", &platform->inter_frame_pm_feature);
#else
	platform->dvs_status = 0;
	platform->inter_frame_pm_feature = 0;
#endif
	gpu_update_config_data_int(np, "gpu_runtime_pm_delay_time", &platform->runtime_pm_delay_time);

#ifdef CONFIG_EXYNOS_BTS
	gpu_update_config_data_int(np, "gpu_mo_min_clock", &platform->mo_min_clock);
#ifdef CONFIG_EXYNOS9630_BTS
	platform->bts_scen_idx = bts_get_scenindex("g3d_performance");
#endif
#ifdef CONFIG_MALI_CAMERA_EXT_BTS
	platform->bts_camera_ext_idx= bts_get_scenindex("camera_ext");
	platform->is_set_bts_camera_ext= 0;
#endif
#endif
	gpu_update_config_data_int(np, "gpu_boost_gpu_min_lock", &platform->boost_gpu_min_lock);
	gpu_update_config_data_int(np, "gpu_boost_egl_min_lock", &platform->boost_egl_min_lock);
#ifdef CONFIG_MALI_SEC_VK_BOOST
	gpu_update_config_data_int(np, "gpu_vk_boost_max_lock", &platform->gpu_vk_boost_max_clk_lock);
	gpu_update_config_data_int(np, "gpu_vk_boost_mif_min_lock", &platform->gpu_vk_boost_mif_min_clk_lock);
#endif

#ifdef CONFIG_MALI_SUSTAINABLE_OPT
	gpu_update_config_data_int_array(np, "gpu_sustainable_info", of_data_int_array, 5);
	for (i = 0; i < 5; i++) {
		platform->sustainable.info_array[i] = of_data_int_array[i] == 0 ? 0 : (u32) of_data_int_array[i];
	}
#endif
	gpu_update_config_data_bool(np, "gpu_bts_support", &platform->gpu_bts_support);
	gpu_update_config_data_int(np, "gpu_set_pmu_duration_reg", &platform->gpu_set_pmu_duration_reg);
	gpu_update_config_data_int(np, "gpu_set_pmu_duration_val", &platform->gpu_set_pmu_duration_val);

#ifdef CONFIG_MALI_DVFS
	gpu_update_config_data_string(np, "g3d_genpd_name", &of_string);
	if (of_string)
		strncpy(platform->g3d_genpd_name, of_string, sizeof(platform->g3d_genpd_name));
#endif
	platform->gpu_dss_freq_id = 0;
	gpu_update_config_data_int(np, "gpu_ess_id_type", &platform->gpu_dss_freq_id);

	return 0;
}

#ifdef CONFIG_MALI_DVFS
static int gpu_dvfs_update_asv_table(struct kbase_device *kbdev)
{
	struct exynos_context *platform = kbdev->platform_context;
	gpu_dvfs_info *dvfs_table;
	struct dvfs_rate_volt g3d_rate_volt[48];
	int cal_get_dvfs_lv_num;
	int cal_table_size;
	int of_data_int_array[OF_DATA_NUM_MAX];
	int dvfs_table_row_num = 0, dvfs_table_col_num = 0;
	int dvfs_table_size = 0;
	int table_idx;
	struct device_node *np;
	int i, j, cal_freq, cal_vol;

	np = kbdev->dev->of_node;
	gpu_update_config_data_int_array(np, "gpu_dvfs_table_size", of_data_int_array, 2);

	dvfs_table_row_num = of_data_int_array[0];
	dvfs_table_col_num = of_data_int_array[1];
	dvfs_table_size = dvfs_table_row_num * dvfs_table_col_num;

	if (dvfs_table_size > OF_DATA_NUM_MAX) {
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "dvfs_table size is not enough\n");
		return -1;
	}
	dvfs_table = gpu_dvfs_table_default;

	cal_get_dvfs_lv_num = cal_dfs_get_lv_num(platform->g3d_cmu_cal_id);
	cal_table_size = cal_dfs_get_rate_asv_table(platform->g3d_cmu_cal_id, g3d_rate_volt);
	if (!cal_table_size)
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "Failed to get G3D ASV table\n");

	GPU_LOG(DVFS_WARNING, DUMMY, 0u, 0u, "ECT table(%d) and gpu driver(%d)\n",
			cal_get_dvfs_lv_num, dvfs_table_row_num);

	gpu_update_config_data_int_array(np, "gpu_dvfs_table", of_data_int_array, dvfs_table_size);

	for (i = 0; i < cal_get_dvfs_lv_num; i++) {
		cal_freq = g3d_rate_volt[i].rate;
		cal_vol = g3d_rate_volt[i].volt;
		if (cal_freq <= platform->gpu_max_clock && cal_freq >= platform->gpu_min_clock) {
			for (j = 0; j < dvfs_table_row_num; j++) {
				table_idx = j * dvfs_table_col_num;
				// Compare cal_freq with DVFS table freq
				if (cal_freq == of_data_int_array[table_idx]) {
					dvfs_table[j].clock = cal_freq;
					dvfs_table[j].voltage = cal_vol;
					dvfs_table[j].min_threshold = of_data_int_array[table_idx+1];
					dvfs_table[j].max_threshold = of_data_int_array[table_idx+2];
					dvfs_table[j].down_staycount = of_data_int_array[table_idx+3];
					dvfs_table[j].mem_freq = of_data_int_array[table_idx+4];
					dvfs_table[j].cpu_little_min_freq = of_data_int_array[table_idx+5];
					GPU_LOG(DVFS_WARNING, DUMMY, 0u, 0u, "G3D %7dKhz ASV is %duV\n", cal_freq, cal_vol);
					if (platform->gpu_pmqos_cpu_cluster_num == 3) {
						dvfs_table[j].cpu_middle_min_freq = of_data_int_array[table_idx+6];
						dvfs_table[j].cpu_big_max_freq = (of_data_int_array[table_idx+7] ? of_data_int_array[table_idx+7]:CPU_MAX);
					GPU_LOG(DVFS_INFO, DUMMY, 0u, 0u, "up [%d] down [%d] staycnt [%d] mif [%d] lit [%d] mid [%d] big [%d]\n",
							dvfs_table[j].max_threshold, dvfs_table[j].min_threshold, dvfs_table[j].down_staycount,
							dvfs_table[j].mem_freq, dvfs_table[j].cpu_little_min_freq, dvfs_table[j].cpu_middle_min_freq,
							dvfs_table[j].cpu_big_max_freq);
					} else {
						//Assuming cpu cluster number is 2
						dvfs_table[j].cpu_big_max_freq = (of_data_int_array[table_idx+6] ? of_data_int_array[table_idx+6]:CPU_MAX);
						GPU_LOG(DVFS_INFO, DUMMY, 0u, 0u, "up [%d] down [%d] staycnt [%d] mif [%d] lit [%d] big [%d]\n",
								dvfs_table[j].max_threshold, dvfs_table[j].min_threshold, dvfs_table[j].down_staycount,
								dvfs_table[j].mem_freq, dvfs_table[j].cpu_little_min_freq, dvfs_table[j].cpu_big_max_freq);

					}

					if (dvfs_table[j].clock==1300000){//
						dvfs_table[j].min_threshold =96;
						dvfs_table[j].max_threshold =100;
						dvfs_table[j].mem_freq =1794000;
						dvfs_table[j].cpu_little_min_freq=1794000;
					}
					if (dvfs_table[j].clock==1200000){
						dvfs_table[j].min_threshold =80;
						dvfs_table[j].max_threshold =96;
						dvfs_table[j].mem_freq =1794000;
						dvfs_table[j].down_staycount=1;
						dvfs_table[j].cpu_little_min_freq=1794000;
					}
					if (dvfs_table[j].clock==1100000){//
						dvfs_table[j].min_threshold =85;
						dvfs_table[j].max_threshold =90;
						dvfs_table[j].mem_freq =1794000;
						dvfs_table[j].down_staycount=1;
						dvfs_table[j].cpu_little_min_freq=1794000;
					}
					if (dvfs_table[j].clock==1001000){
						dvfs_table[j].min_threshold =85;//90
						dvfs_table[j].max_threshold =90;//95
						dvfs_table[j].mem_freq =1794000;
						dvfs_table[j].down_staycount=1;
						dvfs_table[j].cpu_little_min_freq=1794000;
					}
					// if (dvfs_table[j].clock==845000){//
					// 	dvfs_table[j].min_threshold =85;//90//
					// 	dvfs_table[j].max_threshold =90;//95
					// 	dvfs_table[j].mem_freq =1794000;
					// 	dvfs_table[j].down_staycount=3;
					// 	dvfs_table[j].cpu_little_min_freq=1794000;
					// }
					// if (dvfs_table[j].clock==676000){
					// 	dvfs_table[j].min_threshold =80;//90
					// 	dvfs_table[j].max_threshold =85;//95
					// 	dvfs_table[j].mem_freq =1794000;
					// 	dvfs_table[j].down_staycount=2;
					// 	dvfs_table[j].cpu_little_min_freq=1690000;
					// }
					// if (dvfs_table[j].clock==545000){
					// 	dvfs_table[j].min_threshold =78;//90
					// 	dvfs_table[j].max_threshold =85;//95
					// 	dvfs_table[j].mem_freq =1539000; //676
					// 	dvfs_table[j].down_staycount=1;
					// 	dvfs_table[j].cpu_little_min_freq=1482000;
					// }
					// if (dvfs_table[j].clock==450000){
					// 	dvfs_table[j].min_threshold =75;//85
					// 	dvfs_table[j].max_threshold =85;//95
					// 	dvfs_table[j].mem_freq =1352000; //546
					// 	dvfs_table[j].down_staycount=1;
					// 	dvfs_table[j].cpu_little_min_freq=1144000;
					// }
					// if (dvfs_table[j].clock==343000){
					// 	dvfs_table[j].min_threshold =70;//70
					// 	dvfs_table[j].max_threshold =85;//90
					// 	dvfs_table[j].mem_freq =845000; //420
					// 	dvfs_table[j].down_staycount=1;
					// 	dvfs_table[j].cpu_little_min_freq=728000;
					// }

				}
			}
		}
	}
	return 0;
}
#endif

static int gpu_context_init(struct kbase_device *kbdev)
{
	struct exynos_context *platform;
	struct mali_base_gpu_core_props *core_props;

	platform = kmalloc(sizeof(struct exynos_context), GFP_KERNEL);

	if (platform == NULL)
		return -1;

	memset(platform, 0, sizeof(struct exynos_context));
	kbdev->platform_context = (void *) platform;
	pkbdev = kbdev;

	mutex_init(&platform->gpu_clock_lock);
	mutex_init(&platform->gpu_dvfs_handler_lock);
	spin_lock_init(&platform->gpu_dvfs_spinlock);

#if (defined(CONFIG_SCHED_EMS) || defined(CONFIG_SCHED_EHMP) || defined(CONFIG_SCHED_HMP))
	mutex_init(&platform->gpu_sched_hmp_lock);
	platform->ctx_need_qos = false;
#endif

#ifdef CONFIG_MALI_SEC_VK_BOOST
	mutex_init(&platform->gpu_vk_boost_lock);
	platform->ctx_vk_need_qos = false;
#endif

	gpu_dvfs_update_config_data_from_dt(kbdev);
#ifdef CONFIG_MALI_DVFS
	gpu_dvfs_update_asv_table(kbdev);
#endif

	core_props = &(kbdev->gpu_props.props.core_props);
	core_props->gpu_freq_khz_max = platform->gpu_max_clock * 1000;

#if MALI_SEC_PROBE_TEST != 1
	kbdev->vendor_callbacks = (struct kbase_vendor_callbacks *)gpu_get_callbacks();
#endif

#ifdef CONFIG_MALI_EXYNOS_TRACE
	if (gpu_trace_init(kbdev) != 0)
		return -1;
#endif

#ifdef CONFIG_MALI_ASV_CALIBRATION_SUPPORT
	platform->gpu_auto_cali_status = false;
#endif

	platform->inter_frame_pm_status = platform->inter_frame_pm_feature;

#if IS_ENABLED(CONFIG_MALI_EXYNOS_SECURE_RENDERING_LEGACY) || IS_ENABLED(CONFIG_MALI_EXYNOS_SECURE_RENDERING_ARM)
	spin_lock_init(&platform->exynos_smc_lock);
#endif

	return 0;
}

#ifdef CONFIG_MALI_GPU_CORE_MASK_SELECTION
static void gpu_core_mask_set(struct kbase_device *kbdev)
{
	u64 default_core_mask = 0x0;
	void __iomem *core_fused_reg;
	u64 temp, core_info;
	u64 val;
	u64 core_stack[8] = {0, };
	int i = 0;
	void __iomem *lotid_fused_reg;
	u64 lotid_val, lotid_info;

	lotid_fused_reg = ioremap(0x10000004, SZ_8K);
	lotid_val = __raw_readl(lotid_fused_reg);
	lotid_info = lotid_val & 0xFFFFF;

	if (lotid_info == 0x3A8D3) {    /* core mask code for KC first lot */
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] first lot!!!\n");
		core_fused_reg = ioremap(0x1000903c, SZ_8K);	/* GPU DEAD CORE Info */
		val = __raw_readl(core_fused_reg);

		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] core fused reg info, Addr[0x%llx], Data[0x%llx]\n", (unsigned long long)core_fused_reg, val);
		core_info = (val >> 8) & 0xFFFFF;

		if (core_info) {	/* has dead core more 1-core */
			temp = (~core_info) & 0xFFFFF;

			GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] core last info = 0x%llx\n", temp);
			core_stack[0] = temp & 0xF;	            /* core 0, 1, 2, 3 */
			core_stack[1] = (temp & 0x70) >> 4;	    /* core 4, 5, 6 */
			core_stack[2] = (temp & 0x380) >> 7;    /* core 7, 8, 9 */
			core_stack[4] = (temp & 0x3C00) >> 10;  /* core 10, 11, 12, 13 */
			core_stack[5] = (temp & 0x1C000) >> 14; /* core 14, 15, 16 */
			core_stack[6] = (temp & 0xE0000) >> 17; /* core 17, 18, 19 */

			for (i = 0; i < 8; i++) {
				if (i == 3 || i == 7)
					continue;

				GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] before core stack[%d] = 0x%llx\n", i, core_stack[i]);
				if (core_stack[i] == 0xb)
					core_stack[i] = 3;	/* 0b1011 */
				if (core_stack[i] == 0xd)
					core_stack[i] = 1;	/* 0b1101 */
				if (core_stack[i] == 0x9)
					core_stack[i] = 1;	/* 0b1001 */
				if (core_stack[i] == 0x5)
					core_stack[i] = 1;	/* 0b101  */
				if (!(core_stack[i] == 0x1 || core_stack[i] == 0x3 || core_stack[i] == 0x7 || core_stack[i] == 0xf))
					core_stack[i] = 0;
				GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] after core stack[%d] = 0x%llx\n", i, core_stack[i]);

				if (i < 4) {
					default_core_mask |= (((core_stack[i] >> 0) & 0x1) << (0 + i));
					default_core_mask |= (((core_stack[i] >> 1) & 0x1) << (4 + i));
					default_core_mask |= (((core_stack[i] >> 2) & 0x1) << (8 + i));
					default_core_mask |= (((core_stack[i] >> 3) & 0x1) << (12 + i));
				} else {
					default_core_mask |= (((core_stack[i] >> 0) & 0x1) << (16 + i - 4));
					default_core_mask |= (((core_stack[i] >> 1) & 0x1) << (20 + i - 4));
					default_core_mask |= (((core_stack[i] >> 2) & 0x1) << (24 + i - 4));
					default_core_mask |= (((core_stack[i] >> 3) & 0x1) << (28 + i - 4));
				}
			}
			kbdev->pm.debug_core_mask_info = default_core_mask;
			GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] has dead core!, normal core mask = 0x%llx\n", default_core_mask);
		} else {
			kbdev->pm.debug_core_mask_info = 0x17771777;
		}
	} else {	/* Have to use this code since 'KC second lot' release */
		core_fused_reg = ioremap(0x1000A024, SZ_1K);    /* GPU DEAD CORE Info */
		val = __raw_readl(core_fused_reg);

		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] core fused reg info, Addr[0x%llx], Data[0x%llx]\n", (unsigned long long)core_fused_reg, val);
		core_info = val;
		GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] core shift info = 0x%llx\n", core_info);

		if (core_info) {        /* has dead core more 1-core */
			temp = (~core_info) & 0x17771777;

			GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] core last info = 0x%llx\n", temp);
			core_stack[0] = temp & 0x1111;          /* core 0, 1, 2, 3 */
			core_stack[1] = (temp & 0x222);         /* core 4, 5, 6    */
			core_stack[2] = (temp & 0x444);         /* core 7, 8, 9    */
			core_stack[4] = (temp & 0x11110000) >> 16;      /* core 10, 11, 12, 13 */
			core_stack[5] = (temp & 0x2220000) >> 16;       /* core 14, 15, 16     */
			core_stack[6] = (temp & 0x4440000) >> 16;       /* core 17, 18, 19     */

			for (i = 0; i < 8; i++) {
				if (i == 3 || i == 7)
					continue;

				GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] before core stack[%d] = 0x%llx\n", i, core_stack[i]);
				if(i==1 || i==5) core_stack[i] = core_stack[i] >> 1;
				if(i==2 || i==6) core_stack[i] = core_stack[i] >> 2;
				if (core_stack[i] == 0x1011)
					core_stack[i] = 0x0011;         /* 0b1011 */
				if (core_stack[i] == 0x1101)
					core_stack[i] = 0x0001;         /* 0b1101 */
				if (core_stack[i] == 0x1001)
					core_stack[i] = 0x0001;         /* 0b1001 */
				if (core_stack[i] == 0x101)
					core_stack[i] = 0x0001;		/* 0b101 */
				if (!(core_stack[i] == 0x1 || core_stack[i] == 0x11 || core_stack[i] == 0x111 || core_stack[i] == 0x1111))
					core_stack[i] = 0;
				if (i == 1 || i == 5)
					core_stack[i] = core_stack[i] << 1;
				if (i == 2 || i == 6)
					core_stack[i] = core_stack[i] << 2;
				GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] after core stack[%d] = 0x%llx\n", i, core_stack[i]);

				if (i < 4) {
					default_core_mask |= core_stack[i];
				} else {
					default_core_mask |= (core_stack[i]<<16);
				}
			}
			kbdev->pm.debug_core_mask_info = default_core_mask;
			GPU_LOG(DVFS_ERROR, DUMMY, 0u, 0u, "[GPU] has dead core!, normal core mask = 0x%llx\n", default_core_mask);
		} else {
			kbdev->pm.debug_core_mask_info = 0x17771777;
		}
	}
	iounmap(core_fused_reg);
	iounmap(lotid_fused_reg);
}
#endif

/**
 ** Exynos5 hardware specific initialization
 **/
static int kbase_platform_exynos5_init(struct kbase_device *kbdev)
{
	/* gpu context init */
	if (gpu_context_init(kbdev) < 0)
		goto init_fail;

#if defined(CONFIG_SOC_EXYNOS7420) || defined(CONFIG_SOC_EXYNOS7890)
	if (gpu_device_specific_init(kbdev) < 0)
		goto init_fail;
#endif
	/* gpu control module init */
	if (gpu_control_module_init(kbdev) < 0)
		goto init_fail;

	/* gpu notifier init */
	if (gpu_notifier_init(kbdev) < 0)
		goto init_fail;

#ifdef CONFIG_MALI_DVFS
	/* gpu utilization moduel init */
	gpu_dvfs_utilization_init(kbdev);

	/* dvfs governor init */
	gpu_dvfs_governor_init(kbdev);

	/* dvfs handler init */
	gpu_dvfs_handler_init(kbdev);
#endif /* CONFIG_MALI_DVFS */

#ifdef CONFIG_MALI_DEBUG_SYS
	/* gpu sysfs file init */
	if (gpu_create_sysfs_file(kbdev->dev) < 0)
		goto init_fail;
#endif /* CONFIG_MALI_DEBUG_SYS */
	/* MALI_SEC_INTEGRATION */
#ifdef CONFIG_MALI_GPU_CORE_MASK_SELECTION
	gpu_core_mask_set(kbdev);
#endif

	return 0;

init_fail:
	kfree(kbdev->platform_context);

	return -1;
}

/**
 ** Exynos5 hardware specific termination
 **/
static void kbase_platform_exynos5_term(struct kbase_device *kbdev)
{
	struct exynos_context *platform;
	platform = (struct exynos_context *) kbdev->platform_context;

	gpu_notifier_term();

#ifdef CONFIG_MALI_DVFS
	gpu_dvfs_handler_deinit(kbdev);
#endif /* CONFIG_MALI_DVFS */

	gpu_dvfs_utilization_deinit(kbdev);

	gpu_control_module_term(kbdev);

	kfree(kbdev->platform_context);
	kbdev->platform_context = 0;

#ifdef CONFIG_MALI_DEBUG_SYS
	gpu_remove_sysfs_file(kbdev->dev);
#endif /* CONFIG_MALI_DEBUG_SYS */
}

struct kbase_platform_funcs_conf platform_funcs = {
	.platform_init_func = &kbase_platform_exynos5_init,
	.platform_term_func = &kbase_platform_exynos5_term,
};

int kbase_platform_early_init(void)
{
	return 0;
}
