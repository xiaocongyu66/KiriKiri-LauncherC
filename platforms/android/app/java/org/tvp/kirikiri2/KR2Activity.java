package org.tvp.kirikiri2;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.os.Debug;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import android.view.View;

import androidx.annotation.NonNull;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Objects;

public final class KR2Activity {
    private static ActivityManager mActivityManager = null;
    private static final ActivityManager.MemoryInfo memoryInfo = new ActivityManager.MemoryInfo();
    private static final Debug.MemoryInfo mDbgMemoryInfo = new Debug.MemoryInfo();
    private static final Handler msgHandler = new Handler(Looper.getMainLooper(), KR2Activity::handleMessage);
    static String[] _extSdPaths;

    private KR2Activity() {
    }

    public static void updateMemoryInfo() {
        Activity activity = NativeUiHost.getActivityContextOrNull();
        if (activity == null) return;
        if (mActivityManager == null) {
            mActivityManager = (ActivityManager) activity.getSystemService(Activity.ACTIVITY_SERVICE);
        }
        if (mActivityManager != null) mActivityManager.getMemoryInfo(memoryInfo);
        Debug.getMemoryInfo(mDbgMemoryInfo);
    }

    public static String GetVersion() {
        Activity activity = NativeUiHost.getActivityContextOrNull();
        Context context = activity != null ? activity.getApplicationContext() : null;
        if (context == null) return null;
        try {
            return context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException ignored) {
            return null;
        }
    }

    public static long getAvailMemory() {
        return memoryInfo.availMem;
    }

    public static long getUsedMemory() {
        return mDbgMemoryInfo.getTotalPss();
    }

    public static Context requireApplicationContext() {
        return NativeUiHost.requireApplicationContext();
    }

    public static boolean handleMessage(Message msg) {
        return true;
    }

    public static void showTextInput(int x, int y, int w, int h) {
        NativeUiHost.showTextInput(x, y, w, h);
    }

    public static void hideTextInput() {
        NativeUiHost.hideTextInput();
    }

    public static void exit() {
        System.exit(0);
    }

    static final int ORIENT_VERTICAL = 1;
    static final int ORIENT_HORIZONTAL = 2;

    public static void setOrientation(int orient) {
        Activity activity = NativeUiHost.getActivityContextOrNull();
        if (activity == null) return;
        if (orient == ORIENT_VERTICAL) {
            activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
        } else if (orient == ORIENT_HORIZONTAL) {
            activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        }
    }

    public static void ShowMessageBox(final String title, final String text, final String[] Buttons) {
        NativeUiHost.showMessageBox(title, text, Buttons);
    }

    public static void ShowInputBox(final String title, final String prompt, final String text, final String[] Buttons) {
        NativeUiHost.showInputBox(title, prompt, text, Buttons);
    }

    public static void ShowPluginToast(final String title, final String text) {
        NativeUiHost.showPluginToast(title, text);
    }

    public static void MessageController(int what, int arg1, int arg2) {
        Message msg = msgHandler.obtainMessage();
        msg.what = what;
        msg.arg1 = arg1;
        msg.arg2 = arg2;
        msgHandler.sendMessage(msg);
    }

    public static String getLaunchGamePath() {
        String resolved = resolveLaunchGamePath();
        Log.i("KR2-Launch", "getLaunchGamePath -> " + resolved);
        return resolved;
    }

    public static String getLaunchGameDir() {
        Activity activity = NativeUiHost.getActivityContextOrNull();
        if (activity == null || activity.getIntent() == null) return "";
        String path = activity.getIntent().getStringExtra("extra_game_dir");
        return path == null ? "" : path;
    }

    private static boolean isLaunchExtension(String extension) {
        return extension.equals("xp3") || extension.equals("tjs") || extension.equals("ks");
    }

    private static String extensionLower(File file) {
        String name = file.getName();
        int pos = name.lastIndexOf('.');
        if (pos < 0 || pos + 1 >= name.length()) return "";
        return name.substring(pos + 1).toLowerCase(Locale.ROOT);
    }

    private static String baseLower(File file) {
        String name = file.getName();
        int pos = name.lastIndexOf('.');
        if (pos >= 0) name = name.substring(0, pos);
        return name.toLowerCase(Locale.ROOT);
    }

    private static int preferredLaunchIndex(String nameLower) {
        switch (nameLower) {
            case "startup.tjs": return 0;
            case "start.tjs": return 1;
            case "data.xp3": return 2;
            case "startup.xp3": return 3;
            case "start.xp3": return 4;
            case "main.xp3": return 5;
            case "game.xp3": return 6;
            case "first.ks": return 7;
            case "scenario.ks": return 8;
            default: return -1;
        }
    }

    private static boolean isAssetArchiveBase(String base) {
        return base.equals("patch") || base.startsWith("patch") || base.equals("bg") ||
                base.startsWith("bg") || base.contains("image") || base.contains("voice") ||
                base.contains("sound") || base.contains("audio") || base.contains("music") ||
                base.contains("movie") || base.contains("video") || base.contains("effect");
    }

    private static int launchRank(File file) {
        String name = file.getName().toLowerCase(Locale.ROOT);
        int preferred = preferredLaunchIndex(name);
        if (preferred >= 0) return preferred;
        String extension = extensionLower(file);
        String base = baseLower(file);
        if (extension.equals("xp3")) {
            if (base.equals("boot")) return 20;
            if (base.equals("main") || base.equals("game") || base.equals("scenario") || base.equals("script")) return 30;
            if (base.startsWith("data")) return 40;
            if (isAssetArchiveBase(base)) return 300;
            return 80;
        }
        if (extension.equals("tjs")) return base.equals("main") || base.equals("boot") || base.equals("game") ? 60 : 90;
        if (extension.equals("ks")) return base.equals("first") || base.equals("scenario") ? 70 : 100;
        return 500;
    }

    private static String resolveLaunchGamePath() {
        Activity activity = NativeUiHost.getActivityContextOrNull();
        if (activity == null || activity.getIntent() == null) return "";
        String launchFile = activity.getIntent().getStringExtra("extra_launch_file");
        if (launchFile != null && !launchFile.isEmpty()) {
            File file = new File(launchFile);
            if (file.exists() && file.isFile()) return file.getAbsolutePath();
        }
        String path = activity.getIntent().getStringExtra("extra_game_dir");
        if (path == null || path.isEmpty()) return "";
        File dir = new File(path);
        if (!dir.exists()) return "";
        if (dir.isFile()) return dir.getAbsolutePath();
        if (!dir.isDirectory()) return "";
        File startup = new File(dir, "startup.tjs");
        if (startup.exists() && startup.isFile()) return startup.getAbsolutePath();
        File start = new File(dir, "start.tjs");
        if (start.exists() && start.isFile()) return start.getAbsolutePath();
        File data = new File(dir, "data.xp3");
        if (data.exists() && data.isFile()) return data.getAbsolutePath();
        File[] files = dir.listFiles();
        if (files == null) return "";
        ArrayList<File> candidates = new ArrayList<>();
        for (File f : files) {
            if (f.isFile() && isLaunchExtension(extensionLower(f))) candidates.add(f);
        }
        candidates.sort((lhs, rhs) -> {
            int rank = Integer.compare(launchRank(lhs), launchRank(rhs));
            if (rank != 0) return rank;
            return lhs.getName().compareToIgnoreCase(rhs.getName());
        });
        return candidates.isEmpty() ? "" : candidates.get(0).getAbsolutePath();
    }

    public static String[] GetDataPath() {
        return new String[]{Environment.getExternalStorageDirectory().getAbsolutePath()};
    }

    public static String[] getStoragePath() {
        return GetDataPath();
    }

    public static String getLocaleName() {
        return Locale.getDefault().getLanguage();
    }

    private static String[] getExtSdCardPaths(Context context) {
        List<String> paths = new ArrayList<>();
        File[] files = context != null ? context.getExternalFilesDirs("external") : new File[0];
        for (File file : files) {
            if (file != null && !file.equals(context.getExternalFilesDir("external"))) {
                int index = file.getAbsolutePath().lastIndexOf("/Android/data");
                if (index >= 0) {
                    String path = file.getAbsolutePath().substring(0, index);
                    try {
                        path = new File(path).getCanonicalPath();
                    } catch (IOException ignored) {
                    }
                    paths.add(path);
                }
            }
        }
        return paths.toArray(new String[0]);
    }

    public static String getExtSdCardFolder(final File file, Context context) {
        if (_extSdPaths == null) _extSdPaths = getExtSdCardPaths(context);
        try {
            for (String extSdPath : _extSdPaths) {
                if (file.getCanonicalPath().startsWith(extSdPath)) return extSdPath;
            }
        } catch (IOException ignored) {
        }
        return null;
    }

    public static boolean isOnExtSdCard(final File file, Context c) {
        return getExtSdCardFolder(file, c) != null;
    }

    public static boolean RenameFile(String from, String to) {
        File file = new File(from);
        File target = new File(to);
        if (!file.exists()) return false;
        if (target.exists() && !DeleteFile(target.getAbsolutePath())) return false;
        File parent = target.getParentFile();
        if (parent != null && !parent.exists() && !CreateFolders(parent.getAbsolutePath())) return false;
        return file.renameTo(target);
    }

    public static boolean deleteFilesInFolder(final File folder, Context context) {
        boolean totalSuccess = true;
        if (folder == null) return false;
        if (folder.isDirectory()) {
            File[] children = folder.listFiles();
            if (children != null) {
                for (File child : children) deleteFilesInFolder(child, context);
            }
        }
        if (!folder.delete()) totalSuccess = false;
        return totalSuccess;
    }

    public static boolean DeleteFile(String path) {
        File file = new File(path);
        boolean fileDelete = deleteFilesInFolder(file, NativeUiHost.getActivityContextOrNull());
        return file.delete() || fileDelete || !file.exists();
    }

    public static OutputStream getOutputStream(@NonNull final File target, Context context, long s) throws Exception {
        try {
            if (Files.isWritable(target.toPath())) return new FileOutputStream(target);
        } catch (Exception e) {
            Log.e("FileUtils", "Error when copying file from " + target.getAbsolutePath(), e);
        }
        return null;
    }

    public static boolean WriteFile(String path, byte[] data) {
        File target = new File(path);
        if (target.exists()) {
            DeleteFile(target.getAbsolutePath());
        } else {
            File parent = target.getParentFile();
            if (parent != null && !parent.exists()) CreateFolders(parent.getAbsolutePath());
        }
        try (FileOutputStream out = new FileOutputStream(target)) {
            out.write(data);
            return true;
        } catch (IOException e) {
            Log.e("FileUtils", "WriteFile failed: " + path, e);
            return false;
        }
    }

    public static boolean CreateFolders(String path) {
        File file = new File(path);
        return file.exists() || file.mkdirs();
    }

    static boolean isWritableNormalOrSaf(final String path) {
        File folder = new File(path);
        return folder.exists() && folder.isDirectory();
    }

    @SuppressLint("StaticFieldLeak")
    public static View mTextEdit = null;

    public static native boolean nativeLauncherLog(String message, String throwableText);
    public static native void setUseFFmpegImageDecoder(boolean enabled);
    public static native void setFFmpegDecodeMode(int mode);
    public static native void configureFileLogging(boolean enabled, String logFilePath);
    private static native void nativeLifecycleEvent(String eventName, String detail);
}
