package org.github.krkr2

import android.app.Activity
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.WindowManager
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.libsdl.app.SDLAudioManager
import org.tvp.kirikiri2.KR2Activity

class MainActivity : KR2Activity() {

    companion object {
        const val EXTRA_GAME_DIR = "extra_game_dir"
        const val EXTRA_GAME_TITLE = "extra_game_title"
        const val EXTRA_LAUNCH_FILE = "extra_launch_file"
    }

    private var sessionStartedAt = 0L
    private var launchRecorded = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.setEnableVirtualButton(false)
        super.onCreate(savedInstanceState)

        if (!isTaskRoot) {
            return
        }

        val lp = window.attributes
        lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        window.attributes = lp

        requestedOrientation = if (LauncherPrefs.getForceLandscape(this)) {
            ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        } else {
            ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }

        if (!checkStoragePermission()) {
            requestStoragePermission()
        }

        LauncherPrefs.writeLauncherLog(
            this,
            "MainActivity.onCreate gameDir=${intent?.getStringExtra(EXTRA_GAME_DIR).orEmpty()} launchFile=${intent?.getStringExtra(EXTRA_LAUNCH_FILE).orEmpty()} taskRoot=$isTaskRoot"
        )

        SDLAudioManager.nativeSetupJNI()
        SDLAudioManager.initialize()
        SDLAudioManager.setContext(getContext())
    }

    override fun onStart() {
        super.onStart()
        val gameDir = intent?.getStringExtra(EXTRA_GAME_DIR)
        if (!gameDir.isNullOrEmpty()) {
            LauncherPrefs.writeLauncherLog(this, "MainActivity.onStart gameDir=$gameDir launchFile=${intent?.getStringExtra(EXTRA_LAUNCH_FILE).orEmpty()}")
            LauncherPrefs.setLastGamePath(this, gameDir)
            if (!launchRecorded) {
                LauncherPrefs.recordLaunch(this, gameDir)
                launchRecorded = true
            }
        }
    }

    override fun onResume() {
        super.onResume()
        sessionStartedAt = System.currentTimeMillis()
    }

    override fun onPause() {
        recordSessionTime()
        super.onPause()
    }

    override fun onDestroy() {
        recordSessionTime()
        super.onDestroy()
        SDLAudioManager.release(this)
    }

    private fun recordSessionTime() {
        val gameDir = intent?.getStringExtra(EXTRA_GAME_DIR) ?: return
        if (sessionStartedAt > 0L) {
            val delta = System.currentTimeMillis() - sessionStartedAt
            if (delta > 0L) LauncherPrefs.recordPlayTime(this, gameDir, delta)
            sessionStartedAt = 0L
        }
    }

    private fun checkStoragePermission(): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager()
        }
        return ContextCompat.checkSelfPermission(
            this, android.Manifest.permission.WRITE_EXTERNAL_STORAGE
        ) == PackageManager.PERMISSION_GRANTED
    }

    private fun requestStoragePermission(): Boolean {
        var granted = false

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            registerForActivityResult(
                ActivityResultContracts.RequestPermission()
            ) { result -> granted = result }
                .launch(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
            return granted
        }

        val startForResult = registerForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) { result ->
            granted = result.resultCode == Activity.RESULT_OK && checkStoragePermission()
        }

        MaterialAlertDialogBuilder(this)
            .setTitle(getString(org.github.krkr2.R.string.request_storage_permission_title))
            .setMessage(getString(org.github.krkr2.R.string.request_storage_permission))
            .setPositiveButton(getString(org.github.krkr2.R.string.ok)) { _, _ ->
                startForResult.launch(
                    Intent(
                        Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        android.net.Uri.fromParts("package", packageName, null)
                    )
                )
            }
            .setNegativeButton(getString(org.github.krkr2.R.string.cancel), null)
            .show()

        return granted
    }
}