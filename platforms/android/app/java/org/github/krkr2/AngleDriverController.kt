package org.github.krkr2

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.provider.Settings
import android.system.Os
import android.util.Log

object AngleDriverController {
    private const val TAG = "KR2Angle"
    private const val RENDERER_ANGLE = "angle"
    private const val RENDERER_ANGLE_VK = "angle-vk"
    private const val DRIVER_ANGLE = "angle"
    private const val DRIVER_NATIVE = "native"
    private const val KEY_PKGS = "angle_gl_driver_selection_pkgs"
    private const val KEY_VALUES = "angle_gl_driver_selection_values"
    private const val KEY_DEBUG_PACKAGE = "angle_debug_package"
    private const val DEFAULT_ANGLE_PACKAGE = "org.chromium.angle"

    @SuppressLint("ObsoleteSdkInt")
    fun configureBeforeGl(context: Context) {
        val renderer = KrkrPrefsStore.getString(context, "renderer", "software").trim()
        val useAngle = renderer == RENDERER_ANGLE || renderer == RENDERER_ANGLE_VK
        val useAngleVk = renderer == RENDERER_ANGLE_VK
        configureAngleBackendEnv(context, useAngleVk)

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            if (useAngle) {
                log(context, "${angleModeName(useAngleVk)} requested but Android ${Build.VERSION.SDK_INT} is below Q; using native GLES")
            }
            return
        }

        val packageName = context.packageName
        runCatching {
            val resolver = context.contentResolver
            val pkgs = Settings.Global.getString(resolver, KEY_PKGS).orEmpty()
            val values = Settings.Global.getString(resolver, KEY_VALUES).orEmpty()
            if (!useAngle && !pkgs.split(',').any { it.trim() == packageName }) {
                return
            }

            val desiredDriver = if (useAngle) DRIVER_ANGLE else DRIVER_NATIVE
            val updated = mergeDriverSelection(pkgs, values, packageName, desiredDriver)
            if (!canWriteGlobalSettings(context)) {
                if (useAngle) {
                    log(context, "${angleModeName(useAngleVk)} requested but WRITE_SECURE_SETTINGS is not granted; Android only allows per-app ANGLE changes from system/developer settings or adb-granted debug installs, so platform GLES will be used unless ANGLE is already enabled for ${context.packageName}")
                } else {
                    log(context, "ANGLE renderer disabled but WRITE_SECURE_SETTINGS is not granted; leaving system driver selection unchanged")
                }
                return
            }
            if (useAngle && isPackageInstalled(context, DEFAULT_ANGLE_PACKAGE)) {
                Settings.Global.putString(resolver, KEY_DEBUG_PACKAGE, DEFAULT_ANGLE_PACKAGE)
            }
            Settings.Global.putString(resolver, KEY_PKGS, updated.first)
            Settings.Global.putString(resolver, KEY_VALUES, updated.second)
            if (useAngle) {
                log(context, "${angleModeName(useAngleVk)} requested; system driver selection set for $packageName")
            } else {
                log(context, "ANGLE renderer disabled; system driver selection restored to native for $packageName")
            }
        }.onFailure { error ->
            if (useAngle) {
                logSettingsWriteFailure(context, "${angleModeName(useAngleVk)} requested but system driver selection was not writable; using platform GLES unless ANGLE is already enabled", error)
            } else {
                logSettingsWriteFailure(context, "ANGLE renderer disabled but system driver selection was not writable", error)
            }
        }
    }

    fun mergeDriverSelection(
        packageCsv: String,
        valueCsv: String,
        packageName: String,
        driver: String,
    ): Pair<String, String> {
        val packages = packageCsv.split(',')
            .map { it.trim() }
            .filter { it.isNotEmpty() }
            .toMutableList()
        val values = valueCsv.split(',')
            .map { it.trim() }
            .filter { it.isNotEmpty() }
            .toMutableList()

        while (values.size < packages.size) values.add(DRIVER_NATIVE)
        if (values.size > packages.size) values.subList(packages.size, values.size).clear()

        val index = packages.indexOf(packageName)
        if (index >= 0) {
            values[index] = driver
        } else {
            packages.add(packageName)
            values.add(driver)
        }
        return packages.joinToString(",") to values.joinToString(",")
    }

    @Suppress("DEPRECATION")
    private fun isPackageInstalled(context: Context, packageName: String): Boolean =
        runCatching {
            context.packageManager.getPackageInfo(packageName, 0)
        }.isSuccess

    private fun angleModeName(vulkanBackend: Boolean): String =
        if (vulkanBackend) "ANGLE-VK renderer" else "ANGLE renderer"

    private fun canWriteGlobalSettings(context: Context): Boolean =
        context.checkCallingOrSelfPermission(Manifest.permission.WRITE_SECURE_SETTINGS) == PackageManager.PERMISSION_GRANTED

    private fun configureAngleBackendEnv(context: Context, vulkanBackend: Boolean) {
        runCatching {
            if (vulkanBackend) {
                Os.setenv("ANGLE_DEFAULT_PLATFORM", "vulkan", true)
            } else {
                Os.unsetenv("ANGLE_DEFAULT_PLATFORM")
            }
        }.onFailure { error ->
            if (vulkanBackend) {
                log(context, "ANGLE-VK backend env setup failed", error)
            }
        }
    }

    private fun logSettingsWriteFailure(context: Context, message: String, throwable: Throwable) {
        if (throwable is SecurityException) {
            log(context, "$message: WRITE_SECURE_SETTINGS is not granted; Android restricts per-app ANGLE control to system/developer settings or adb-granted debug installs")
        } else {
            log(context, message, throwable)
        }
    }

    private fun log(context: Context, message: String, throwable: Throwable? = null) {
        Log.i(TAG, message, throwable)
        runCatching { LauncherPrefs.writeLauncherLog(context, message, throwable) }
    }
}
