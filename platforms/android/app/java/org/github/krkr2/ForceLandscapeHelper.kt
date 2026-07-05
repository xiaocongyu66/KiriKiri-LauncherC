package org.github.krkr2

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.util.Log
import android.view.OrientationEventListener
import android.view.WindowManager

/**
 * Drives "Force landscape" behaviour for the engine activity.
 *
 * Android's `screenOrientation="landscape"` is a request, not a guarantee:
 *  - OEM auto-rotation locks can still flip the surface back to portrait.
 *  - Some vivo/OPPO/MIUI builds re-evaluate orientation on every config
 *    change and ignore the manifest hint when the user has rotation lock on.
 *  - SDL itself rewrites requestedOrientation when its hint table changes.
 *
 * To make the lock actually stick, we do three things:
 *  1. Force `requestedOrientation = SCREEN_ORIENTATION_LANDSCAPE` from
 *     `onCreate`, `onResume`, and any time the device reports a rotation
 *     change so we override SDL/system overrides immediately.
 *  2. If the caller has `WRITE_SETTINGS` permission (granted via the system
 *     "Modify system settings" page), temporarily disable
 *     `ACCELEROMETER_ROTATION` while the activity is in front and restore
 *     it on exit. This bypasses the system rotation-lock UI entirely.
 *  3. If we don't yet have `WRITE_SETTINGS`, expose [requestPermission] so
 *     the launcher's settings screen can prompt for it.
 */
object ForceLandscapeHelper {
    private const val TAG = "ForceLandscape"

    /**
     * Whether the calling app currently holds the runtime "Modify system
     * settings" permission. On API 23+ this permission is special and only
     * granted via [Settings.ACTION_MANAGE_WRITE_SETTINGS], so we have to ask
     * the user via that screen rather than a normal runtime grant.
     */
    fun canWriteSystemSettings(context: Context): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            Settings.System.canWrite(context)
        } else {
            true
        }
    }

    /**
     * Open the system "Modify system settings" page for our package so the
     * user can grant `WRITE_SETTINGS`. Best-effort: if the system intent is
     * unavailable (rare, but some Android Go builds), we silently no-op.
     */
    fun requestPermission(context: Context) {
        runCatching {
            val intent = Intent(Settings.ACTION_MANAGE_WRITE_SETTINGS).apply {
                data = Uri.parse("package:${context.packageName}")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
        }.onFailure { Log.w(TAG, "Cannot launch WRITE_SETTINGS page", it) }
    }

    /**
     * Apply landscape orientation to [activity], also flipping the system
     * auto-rotation flag off when [forceLandscape] is true and we hold
     * `WRITE_SETTINGS`. The previous rotation flag is captured so [release]
     * can restore it on exit.
     *
     * Returns true if the orientation request was issued (regardless of
     * whether the system bypass succeeded).
     */
    fun apply(activity: Activity, forceLandscape: Boolean): Boolean {
        if (!forceLandscape) {
            activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            return false
        }
        // Use a fixed landscape request so Android's display and screen
        // recording metadata agree with the game surface orientation.
        activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE

        // Keep the screen on while the engine is rendering — without this
        // the rotation-lock cycle below can interact poorly with screen-off.
        activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        if (canWriteSystemSettings(activity)) {
            runCatching {
                val resolver = activity.contentResolver
                // Persist the prior value the first time we touch it so the
                // user's preference survives a crash where release() is never
                // called.
                val prior = Settings.System.getInt(
                    resolver, Settings.System.ACCELEROMETER_ROTATION, 1
                )
                LauncherPrefs.setSavedAccelerometerRotation(activity, prior)
                Settings.System.putInt(
                    resolver, Settings.System.ACCELEROMETER_ROTATION, 0
                )
                // USER_ROTATION 0=portrait, 1=landscape, 2=reverse-portrait,
                // 3=reverse-landscape. Pinning to 1 forces the surface to
                // the natural landscape orientation matching the manifest.
                Settings.System.putInt(
                    resolver, Settings.System.USER_ROTATION, 1
                )
            }.onFailure { Log.w(TAG, "Failed to flip ACCELEROMETER_ROTATION", it) }
        } else {
            Log.i(TAG, "Force landscape is on but WRITE_SETTINGS is not granted; relying on activity-level lock only.")
        }
        return true
    }

    /**
     * Heuristic for "is this a tablet-class device". We look at the smallest
     * screen width in dp — this is the standard Android signal that
     * material/responsive layouts use, and 600dp is the conventional
     * "small tablet" boundary documented in the Android UI guide.
     */
    fun isTabletClass(activity: Activity): Boolean {
        val sw = activity.resources.configuration.smallestScreenWidthDp
        return sw >= 600
    }

    /**
     * Whether the launcher should currently use the landscape (Row-based)
     * layout. We say yes when:
     *  - the device is tablet class (sw >= 600dp), OR
     *  - the current configuration is already landscape (rotation),
     *  - and (additionally) we're not on a phone in portrait mode.
     */
    fun shouldUseLandscapeLayout(activity: Activity): Boolean {
        val cfg = activity.resources.configuration
        val landscapeNow = cfg.screenWidthDp >= cfg.screenHeightDp
        return isTabletClass(activity) || landscapeNow
    }

    /**
     * Restore system auto-rotation to whatever it was before [apply] flipped
     * it. Safe to call from `onDestroy` even if [apply] was never called or
     * we never had `WRITE_SETTINGS`.
     */
    fun release(activity: Activity) {
        if (!canWriteSystemSettings(activity)) return
        runCatching {
            val prior = LauncherPrefs.getSavedAccelerometerRotation(activity)
            if (prior >= 0) {
                Settings.System.putInt(
                    activity.contentResolver,
                    Settings.System.ACCELEROMETER_ROTATION,
                    prior
                )
                LauncherPrefs.setSavedAccelerometerRotation(activity, -1)
            }
        }.onFailure { Log.w(TAG, "Failed to restore rotation flag", it) }
    }

    /**
     * Build a sticky orientation listener that re-asserts landscape every
     * time the sensor reports a rotation. Some OEMs (notably vivo and
     * Xiaomi) flip orientation on accelerometer events even when
     * `requestedOrientation` is already SCREEN_ORIENTATION_LANDSCAPE; this
     * listener catches those events and counter-pins the activity.
     *
     * Caller is responsible for `enable()` in onResume and `disable()` in
     * onPause.
     */
    fun stickyListener(activity: Activity): OrientationEventListener {
        return object : OrientationEventListener(activity) {
            override fun onOrientationChanged(orientation: Int) {
                // Snap back if the activity ever leaves a landscape mode while
                // force-landscape is on. Both LANDSCAPE and SENSOR_LANDSCAPE
                // (left vs right landscape) count as acceptable.
                if (!LauncherPrefs.getForceLandscape(activity) &&
                    !isEngineLaunch(activity)) return
                val current = activity.requestedOrientation
                val isLandscape = current == ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE ||
                    current == ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE ||
                    current == ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE ||
                    current == ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE
                if (!isLandscape) {
                    activity.requestedOrientation =
                        ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                }
            }
        }
    }

    private fun isEngineLaunch(activity: Activity): Boolean {
        return (activity is MainActivity || activity is SdlRuntimeActivity) &&
            !activity.intent?.getStringExtra(SdlRuntimeActivity.EXTRA_GAME_DIR).isNullOrEmpty()
    }
}
