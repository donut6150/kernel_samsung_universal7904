/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



/**
 * Maximum frequency GPU will be clocked at. Given in kHz.
 * This must be specified as there is no default value.
 *
 * Attached value: number in kHz
 * Default value: NA
 */
#define GPU_FREQ_KHZ_MAX (1300000)
/**
 * Minimum frequency GPU will be clocked at. Given in kHz.
 * This must be specified as there is no default value.
 *
 * Attached value: number in kHz
 * Default value: NA
 */
#define GPU_FREQ_KHZ_MIN (1001000)

/**
 * CPU_SPEED_FUNC - A pointer to a function that calculates the CPU clock
 *
 * CPU clock speed of the platform is in MHz - see kbase_cpu_clk_speed_func
 * for the function prototype.
 *
 * Attached value: A kbase_cpu_clk_speed_func.
 * Default Value:  NA
 */
#define CPU_SPEED_FUNC (NULL)

/**
 * GPU_SPEED_FUNC - A pointer to a function that calculates the GPU clock
 *
 * GPU clock speed of the platform in MHz - see kbase_gpu_clk_speed_func
 * for the function prototype.
 *
 * Attached value: A kbase_gpu_clk_speed_func.
 * Default Value:  NA
 */
#define GPU_SPEED_FUNC (NULL)

/**
 * Power management configuration
 *
 * Attached value: pointer to @ref kbase_pm_callback_conf
 * Default value: See @ref kbase_pm_callback_conf
 */
#define POWER_MANAGEMENT_CALLBACKS (&pm_callbacks)

/**
 * Platform specific configuration functions
 *
 * Attached value: pointer to @ref kbase_platform_funcs_conf
 * Default value: See @ref kbase_platform_funcs_conf
 */
/* MALI_SEC_INTEGRATION */
#define PLATFORM_FUNCS (&platform_funcs)

/** Power model for IPA
 *
 * Attached value: pointer to @ref mali_pa_model_ops
 */
#define POWER_MODEL_CALLBACKS (NULL)

extern struct kbase_pm_callback_conf pm_callbacks;
extern struct kbase_platform_funcs_conf platform_funcs;

/**
 * Secure mode switch
 *
 * Attached value: pointer to @ref kbase_secure_ops
 */
#if IS_ENABLED(CONFIG_MALI_EXYNOS_SECURE_RENDERING_LEGACY)
#define PROTECTED_CALLBACKS (&exynos_protected_ops)
extern struct protected_mode_ops exynos_protected_ops;
#elif IS_ENABLED(CONFIG_MALI_EXYNOS_SECURE_RENDERING_ARM)
#define PROTECTED_CALLBACKS (&exynos_protected_ops_arm)
extern struct protected_mode_ops exynos_protected_ops_arm;
#endif

#define CLK_RATE_TRACE_OPS (&clk_rate_trace_ops)
extern struct kbase_clk_rate_trace_op_conf clk_rate_trace_ops;
