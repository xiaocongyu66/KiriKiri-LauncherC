package org.github.krkr2;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;

import java.io.File;
import java.util.HashMap;
import java.util.Map;

import androidx.annotation.NonNull;

import io.flutter.embedding.android.FlutterActivity;
import io.flutter.embedding.engine.FlutterEngine;
import io.flutter.plugin.common.MethodChannel;

public class LauncherHostActivity extends FlutterActivity {
    private static final String CHANNEL = "org.github.krkr2/platform";
    private static final String DEFAULT_GAME_ROOT = "/storage/emulated/0/krkr2pro";

    @Override
    public void configureFlutterEngine(@NonNull FlutterEngine flutterEngine) {
        super.configureFlutterEngine(flutterEngine);
        new MethodChannel(flutterEngine.getDartExecutor().getBinaryMessenger(), CHANNEL)
            .setMethodCallHandler((call, result) -> {
                switch(call.method) {
                    case "hasFileManagementPermission":
                        result.success(hasFileManagementPermission());
                        break;
                    case "requestFileManagementPermission":
                        requestFileManagementPermission();
                        result.success(null);
                        break;
                    case "pickGameRoot":
                        pickGameRoot();
                        result.success(null);
                        break;
                    case "launchGame":
                        launchGame(call.argument("gameDir"), call.argument("launchFile"),
                                   call.argument("title"));
                        result.success(null);
                        break;
                    case "getGameOverrides":
                        result.success(getGameOverrides(call.argument("gameDir")));
                        break;
                    case "updateGameOverride":
                        updateGameOverride(call.argument("gameDir"), call.argument("key"),
                                           call.argument("value"));
                        result.success(null);
                        break;
                    case "clearGameOverrides":
                        LauncherPrefs.INSTANCE.clearGameEnginePrefs(
                            this, stringValue(call.argument("gameDir")));
                        LauncherPrefs.INSTANCE.setCustomLaunchFile(
                            this, stringValue(call.argument("gameDir")), "");
                        result.success(null);
                        break;
                    case "getGameRoot":
                        result.success(LauncherPrefs.INSTANCE.getGameRoot(this));
                        break;
                    case "setGameRoot": {
                        String path = call.argument("path");
                        LauncherPrefs.INSTANCE.setGameRoot(
                            this, path == null || path.trim().isEmpty() ? DEFAULT_GAME_ROOT
                                                                        : path.trim());
                        result.success(null);
                        break;
                    }
                    case "getScanDepth": {
                        int depth = LauncherPrefs.INSTANCE.getScanDepth(this);
                        result.success(Math.max(1, Math.min(10, depth)));
                        break;
                    }
                    case "getDiagnosticsInfo":
                        result.success(getDiagnosticsInfo());
                        break;
                    case "getLauncherSettings":
                        result.success(getLauncherSettings());
                        break;
                    case "updateLauncherSetting":
                        updateLauncherSetting(call.argument("key"), call.argument("value"));
                        result.success(null);
                        break;
                    case "getEngineSettings":
                        result.success(getEngineSettings());
                        break;
                    case "updateEngineSetting":
                        updateEngineSetting(call.argument("key"), call.argument("value"));
                        result.success(null);
                        break;
                    case "resetEngineSettings":
                        KrkrPrefsStore.INSTANCE.resetToDefaults(this);
                        result.success(null);
                        break;
                    case "launchOriginalEngine":
                        launchGame("", "", "");
                        result.success(null);
                        break;
                    case "openSettings":
                        openApplicationSettings();
                        result.success(null);
                        break;
                    case "openDiagnostics":
                        result.success(null);
                        break;
                    default:
                        result.notImplemented();
                        break;
                }
            });
    }


    private boolean hasFileManagementPermission() {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.R ||
            Environment.isExternalStorageManager();
    }

    private void requestFileManagementPermission() {
        Intent intent;
        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            intent = new Intent(
                Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.fromParts("package", getPackageName(), null));
        } else {
            intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                                Uri.fromParts("package", getPackageName(), null));
        }
        startActivity(intent);
    }

    private void pickGameRoot() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, 1001);
    }

    private void openApplicationSettings() {
        Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                                   Uri.fromParts("package", getPackageName(), null));
        startActivity(intent);
    }

    private void launchGame(String gameDir, String launchFile, String title) {
        String normalizedGameDir = gameDir == null ? "" : gameDir.trim();
        if(!normalizedGameDir.isEmpty()) {
            LauncherPrefs.INSTANCE.applyGameEngineOverrides(this, normalizedGameDir);
        }
        Intent intent = new Intent(this, SdlRuntimeActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        intent.putExtra(SdlRuntimeActivity.EXTRA_GAME_DIR, normalizedGameDir);
        intent.putExtra(SdlRuntimeActivity.EXTRA_GAME_TITLE, title == null ? "" : title);
        if(launchFile != null && !launchFile.trim().isEmpty()) {
            intent.putExtra(SdlRuntimeActivity.EXTRA_LAUNCH_FILE, launchFile.trim());
        }
        startActivity(intent);
    }

    private Map<String, Object> getGameOverrides(String gameDir) {
        String dir = stringValue(gameDir);
        Map<String, Object> values = new HashMap<>();
        values.put("customLaunch", LauncherPrefs.INSTANCE.getCustomLaunchFile(this, dir));
        String rawRenderer = LauncherPrefs.INSTANCE.getGameEnginePref(this, dir, "renderer");
        String rawGraphicsBackend =
            LauncherPrefs.INSTANCE.getGameEnginePref(this, dir, "graphics_backend");
        values.put("renderer", rawRenderer == null || rawRenderer.trim().isEmpty()
                   ? ""
                   : LauncherPrefs.INSTANCE.normalizeRendererPreference(rawRenderer));
        values.put("graphics_backend",
                   rawGraphicsBackend == null || rawGraphicsBackend.trim().isEmpty()
                   ? (isLegacyVulkanRenderer(rawRenderer) ? "vulkan" : "")
                   : LauncherPrefs.INSTANCE.normalizeGraphicsBackendPreference(
                       rawGraphicsBackend));
        values.put("fps_limit",
                   nullToEmpty(LauncherPrefs.INSTANCE.getGameEnginePref(this, dir, "fps_limit")));
        values.put("showfps",
                   "1".equals(LauncherPrefs.INSTANCE.getGameEnginePref(this, dir, "showfps")));
        values.put("ogl_accurate_render",
                   "1".equals(LauncherPrefs.INSTANCE.getGameEnginePref(
                       this, dir, "ogl_accurate_render")));
        return values;
    }

    private void updateGameOverride(String gameDir, String key, Object value) {
        String dir = stringValue(gameDir);
        if(dir.isEmpty() || key == null)
            return;
        switch(key) {
            case "customLaunch":
                LauncherPrefs.INSTANCE.setCustomLaunchFile(this, dir, stringValue(value));
                break;
            case "renderer":
                String renderer = stringValue(value);
                LauncherPrefs.INSTANCE.setGameEnginePref(
                    this, dir, "renderer",
                    renderer.isEmpty()
                    ? ""
                    : LauncherPrefs.INSTANCE.normalizeRendererPreference(renderer));
                if(isLegacyVulkanRenderer(renderer)) {
                    LauncherPrefs.INSTANCE.setGameEnginePref(
                        this, dir, "graphics_backend", "vulkan");
                }
                break;
            case "graphics_backend":
                String backend = stringValue(value);
                LauncherPrefs.INSTANCE.setGameEnginePref(
                    this, dir, "graphics_backend",
                    backend.isEmpty()
                    ? ""
                    : LauncherPrefs.INSTANCE.normalizeGraphicsBackendPreference(backend));
                break;
            case "fps_limit":
                LauncherPrefs.INSTANCE.setGameEnginePref(this, dir, "fps_limit", stringValue(value));
                break;
            case "showfps":
                LauncherPrefs.INSTANCE.setGameEnginePref(this, dir, "showfps",
                                                         boolValue(value) ? "1" : "");
                break;
            case "ogl_accurate_render":
                LauncherPrefs.INSTANCE.setGameEnginePref(this, dir, "ogl_accurate_render",
                                                         boolValue(value) ? "1" : "");
                break;
            default:
                break;
        }
    }

    private Map<String, Object> getDiagnosticsInfo() {
        Map<String, Object> info = new HashMap<>();
        File latest = LauncherPrefs.INSTANCE.latestUnifiedLogFile(this);
        info.put("platform", "Android");
        info.put("platformVersion", Build.VERSION.RELEASE + " (SDK " + Build.VERSION.SDK_INT + ")");
        info.put("device", Build.MANUFACTURER + " " + Build.MODEL);
        info.put("packageName", getPackageName());
        info.put("fileManagementGranted", hasFileManagementPermission());
        info.put("gameRoot", LauncherPrefs.INSTANCE.getGameRoot(this));
        info.put("scanDepth", Math.max(1, Math.min(10, LauncherPrefs.INSTANCE.getScanDepth(this))));
        info.put("logDir", LauncherPrefs.INSTANCE.getLogDir(this));
        info.put("latestLog", latest == null ? "" : latest.getAbsolutePath());
        info.put("fileLogEnabled", LauncherPrefs.INSTANCE.getFileLogEnabled(this));
        info.put("nativeLogConfigured", latest != null && latest.exists());
        return info;
    }

    private Map<String, Object> getLauncherSettings() {
        Map<String, Object> settings = new HashMap<>();
        settings.put("language", LauncherPrefs.INSTANCE.getLanguage(this));
        settings.put("forceLandscape", LauncherPrefs.INSTANCE.getForceLandscape(this));
        settings.put("useFfmpegImageDecoder",
                     LauncherPrefs.INSTANCE.getUseFfmpegImageDecoder(this));
        settings.put("useSdlRuntimeActivity",
                     LauncherPrefs.INSTANCE.getUseSdlRuntimeActivity(this));
        settings.put("ffmpegDecodeMode", LauncherPrefs.INSTANCE.getFfmpegDecodeMode(this));
        settings.put("fileLogEnabled", LauncherPrefs.INSTANCE.getFileLogEnabled(this));
        settings.put("fileLogAutoCleanup", LauncherPrefs.INSTANCE.getFileLogAutoCleanup(this));
        settings.put("fileLogRetentionDays", LauncherPrefs.INSTANCE.getFileLogRetentionDays(this));
        settings.put("scanDepth", LauncherPrefs.INSTANCE.getScanDepth(this));
        return settings;
    }

    private Map<String, Object> getEngineSettings() {
        Map<String, Object> settings = new HashMap<>();
        String renderer = KrkrPrefsStore.INSTANCE.getString(this, "renderer", "software");
        String graphicsBackend =
            KrkrPrefsStore.INSTANCE.getString(this, "graphics_backend", "opengl");
        settings.put("renderer",
                     LauncherPrefs.INSTANCE.normalizeRendererPreference(renderer));
        settings.put("graphics_backend",
                     isLegacyVulkanRenderer(renderer) &&
                     (graphicsBackend == null || graphicsBackend.trim().isEmpty())
                     ? "vulkan"
                     : LauncherPrefs.INSTANCE.normalizeGraphicsBackendPreference(graphicsBackend));
        settings.put("fps_limit", KrkrPrefsStore.INSTANCE.getString(this, "fps_limit", "60"));
        settings.put("showfps", KrkrPrefsStore.INSTANCE.getBool(this, "showfps", false));
        settings.put("outputlog", KrkrPrefsStore.INSTANCE.getBool(this, "outputlog", false));
        settings.put("ogl_accurate_render",
                     KrkrPrefsStore.INSTANCE.getBool(this, "ogl_accurate_render", false));
        settings.put("ffmpeg_image_decoder",
                     KrkrPrefsStore.INSTANCE.getBool(this, "ffmpeg_image_decoder", false));
        settings.put("ffmpeg_decode_mode",
                     KrkrPrefsStore.INSTANCE.getString(this, "ffmpeg_decode_mode", "software"));
        settings.put("software_draw_thread",
                     KrkrPrefsStore.INSTANCE.getString(this, "software_draw_thread", "0"));
        settings.put("software_compress_tex",
                     KrkrPrefsStore.INSTANCE.getString(this, "software_compress_tex", "none"));
        settings.put("ogl_max_texsize",
                     KrkrPrefsStore.INSTANCE.getString(this, "ogl_max_texsize", "0"));
        settings.put("ogl_compress_tex",
                     KrkrPrefsStore.INSTANCE.getString(this, "ogl_compress_tex", "none"));
        settings.put("menu_handler_opa",
                     KrkrPrefsStore.INSTANCE.getFloat(this, "menu_handler_opa", 0.15f));
        settings.put("vcursor_scale", KrkrPrefsStore.INSTANCE.getFloat(this, "vcursor_scale", 0.5f));
        return settings;
    }

    private void updateEngineSetting(String key, Object value) {
        if(key == null)
            return;
        switch(key) {
            case "showfps":
            case "outputlog":
            case "ogl_accurate_render":
            case "ffmpeg_image_decoder":
                KrkrPrefsStore.INSTANCE.setBool(this, key, boolValue(value));
                if("ffmpeg_image_decoder".equals(key)) {
                    LauncherPrefs.INSTANCE.setUseFfmpegImageDecoder(this, boolValue(value));
                }
                break;
            case "renderer":
                String renderer = stringValue(value);
                KrkrPrefsStore.INSTANCE.setString(
                    this, key, LauncherPrefs.INSTANCE.normalizeRendererPreference(renderer));
                if(isLegacyVulkanRenderer(renderer)) {
                    KrkrPrefsStore.INSTANCE.setString(this, "graphics_backend", "vulkan");
                }
                break;
            case "graphics_backend":
                KrkrPrefsStore.INSTANCE.setString(
                    this, key,
                    LauncherPrefs.INSTANCE.normalizeGraphicsBackendPreference(stringValue(value)));
                break;
            case "menu_handler_opa":
            case "vcursor_scale":
                KrkrPrefsStore.INSTANCE.setFloat(
                    this, key, floatValue(value, "menu_handler_opa".equals(key) ? 0.15f : 0.5f));
                break;
            case "ffmpeg_decode_mode":
                LauncherPrefs.INSTANCE.setFfmpegDecodeMode(this, stringValue(value));
                break;
            default:
                KrkrPrefsStore.INSTANCE.setString(this, key, stringValue(value));
                break;
        }
    }

    private void updateLauncherSetting(String key, Object value) {
        if(key == null)
            return;
        switch(key) {
            case "language":
                LauncherPrefs.INSTANCE.setLanguage(this, value == null ? "en" : value.toString());
                break;
            case "forceLandscape":
                LauncherPrefs.INSTANCE.setForceLandscape(this, Boolean.TRUE.equals(value));
                break;
            case "useFfmpegImageDecoder":
                LauncherPrefs.INSTANCE.setUseFfmpegImageDecoder(this, Boolean.TRUE.equals(value));
                break;
            case "useSdlRuntimeActivity":
                LauncherPrefs.INSTANCE.setUseSdlRuntimeActivity(this, Boolean.TRUE.equals(value));
                break;
            case "ffmpegDecodeMode":
                LauncherPrefs.INSTANCE.setFfmpegDecodeMode(this,
                                                           value == null ? "software" : value.toString());
                break;
            case "fileLogEnabled":
                LauncherPrefs.INSTANCE.setFileLogEnabled(this, Boolean.TRUE.equals(value));
                break;
            case "fileLogAutoCleanup":
                LauncherPrefs.INSTANCE.setFileLogAutoCleanup(this, Boolean.TRUE.equals(value));
                break;
            case "fileLogRetentionDays":
                LauncherPrefs.INSTANCE.setFileLogRetentionDays(
                    this, toInt(value, LauncherPrefs.FILE_LOG_RETENTION_DEFAULT_DAYS));
                break;
            case "scanDepth":
                LauncherPrefs.INSTANCE.setScanDepth(this,
                                                    toInt(value, LauncherPrefs.SCAN_DEPTH_DEFAULT));
                break;
            default:
                break;
        }
    }

    private int toInt(Object value, int fallback) {
        if(value instanceof Number)
            return ((Number)value).intValue();
        if(value instanceof String) {
            try {
                return Integer.parseInt((String)value);
            } catch(NumberFormatException ignored) {
                return fallback;
            }
        }
        return fallback;
    }

    private String stringValue(Object value) {
        return value == null ? "" : value.toString().trim();
    }

    private String nullToEmpty(String value) {
        return value == null ? "" : value;
    }

    private boolean isLegacyVulkanRenderer(String value) {
        String raw = value == null ? "" : value.trim();
        return "vulkan".equals(raw) || "vk".equals(raw);
    }

    private boolean boolValue(Object value) {
        if(value instanceof Boolean)
            return (Boolean)value;
        if(value instanceof Number)
            return ((Number)value).intValue() != 0;
        if(value instanceof String) {
            String raw = ((String)value).trim();
            return "1".equals(raw) || "true".equalsIgnoreCase(raw);
        }
        return false;
    }

    private float floatValue(Object value, float fallback) {
        if(value instanceof Number)
            return ((Number)value).floatValue();
        if(value instanceof String) {
            try {
                return Float.parseFloat((String)value);
            } catch(NumberFormatException ignored) {
                return fallback;
            }
        }
        return fallback;
    }
}
