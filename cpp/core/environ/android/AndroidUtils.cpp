#include "AndroidUtils.h"
#include <minizip/unzip/unzip.h>
#include <zlib.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "tjs.h"
#include "MsgIntf.h"
#include "md5.h"
#include "DebugIntf.h"
#include <condition_variable>
#include <mutex>
#include "KrkrJniHelper.h"
#include <set>
#include <sstream>
#include "SysInitIntf.h"
#include "ConfigManager/LocaleConfigManager.h"
#include "Platform.h"
#include <EGL/egl.h>
#include <queue>

#ifndef KRKR2_ENABLE_COCOS_HOST
#define KRKR2_ENABLE_COCOS_HOST 1
#endif

#if KRKR2_ENABLE_COCOS_HOST
#include "base/CCDirector.h"
#include "base/CCScheduler.h"
#endif

#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <android/log.h>
#include "TickCount.h"
#include "StorageImpl.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "EventIntf.h"
#include "RenderManager.h"
#include <sys/stat.h>

using JniHelper = krkr::JniHelper;

#define KR2ActJavaPath "org/tvp/kirikiri2/KR2Activity"
// #define KR2EntryJavaPath "org/tvp/kirikiri2/Kirikiroid2"

extern unsigned int __page_size = getpagesize();

static bool TVPAndroidPathExists(const std::string &path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

void TVPPrintLog(const char *str) {
    __android_log_print(ANDROID_LOG_INFO, "kr2 debug info", "%s", str);
}

static tjs_uint32 _lastMemoryInfoQuery = 0;
static tjs_int _availMemory, usedMemory;
static void updateMemoryInfo() {
    if(TVPGetRoughTickCount32() - _lastMemoryInfoQuery > 3000) { // freq in 3s

        JniMethodInfo methodInfo;
        if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath,
                                          "updateMemoryInfo", "()V")) {
            methodInfo.env->CallStaticVoidMethod(methodInfo.classID,
                                                 methodInfo.methodID);
            methodInfo.env->DeleteLocalRef(methodInfo.classID);
        }

        if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath,
                                          "getAvailMemory", "()J")) {
            _availMemory = methodInfo.env->CallStaticLongMethod(
                               methodInfo.classID, methodInfo.methodID) /
                (1024 * 1024);
            methodInfo.env->DeleteLocalRef(methodInfo.classID);
        }

        if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath,
                                          "getUsedMemory", "()J")) {
            // in kB
            usedMemory = methodInfo.env->CallStaticLongMethod(
                             methodInfo.classID, methodInfo.methodID) /
                1024;
            methodInfo.env->DeleteLocalRef(methodInfo.classID);
        }

        _lastMemoryInfoQuery = TVPGetRoughTickCount32();
    }
}

tjs_int TVPGetSystemFreeMemory() {
    updateMemoryInfo();
    return _availMemory;
}

tjs_int TVPGetSelfUsedMemory() {
    updateMemoryInfo();
    return usedMemory;
}

#if defined(__GNUC__)
extern "C" bool TVPSDLAndroidConsumeExternalPresenterPostedFrame()
    __attribute__((weak));
extern "C" bool TVPSDLAndroidSwapExternalPresenterIfDirty()
    __attribute__((weak));
extern "C" bool TVPSDLAndroidIsExternalPresenterActive() __attribute__((weak));
extern "C" void TVPSDLRecordExternalPresenterPostedFrame()
    __attribute__((weak));
#else
extern "C" bool TVPSDLAndroidConsumeExternalPresenterPostedFrame();
extern "C" bool TVPSDLAndroidSwapExternalPresenterIfDirty();
extern "C" bool TVPSDLAndroidIsExternalPresenterActive();
extern "C" void TVPSDLRecordExternalPresenterPostedFrame();
#endif

void TVPForceSwapBuffer() {
    if(TVPSDLAndroidSwapExternalPresenterIfDirty &&
       TVPSDLAndroidSwapExternalPresenterIfDirty()) {
        if(TVPSDLRecordExternalPresenterPostedFrame)
            TVPSDLRecordExternalPresenterPostedFrame();
        return;
    }
    if(TVPSDLAndroidConsumeExternalPresenterPostedFrame &&
       TVPSDLAndroidConsumeExternalPresenterPostedFrame()) {
        return;
    }
    if(TVPSDLAndroidIsExternalPresenterActive &&
       TVPSDLAndroidIsExternalPresenterActive()) {
        return;
    }
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    if(display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE)
        return;
    eglSwapBuffers(display, surface);
}

std::string TVPGetDeviceID() {
    std::string ret;

    // use pure jni to avoid java code
    // 	jclass classID = pEnv->FindClass(KR2EntryJavaPath);
    // 	std::string strtmp("()L"); strtmp += KR2EntryJavaPath; strtmp
    // += ";"; 	jmethodID methodGetInstance =
    // pEnv->GetStaticMethodID(classID, "GetInstance",
    // strtmp.c_str()); 	jobject sInstance =
    // pEnv->CallStaticObjectMethod(classID, methodGetInstance);
    // jmethodID getSystemService = pEnv->GetMethodID(classID,
    // "getSystemService",
    // "(Ljava/lang/String;)Ljava/lang/Object;"); 	jstring jstrPhone
    // = pEnv->NewStringUTF("phone"); 	jobject telephonyManager =
    // pEnv->CallObjectMethod(sInstance, getSystemService, jstrPhone);
    // 	pEnv->DeleteLocalRef(jstrPhone);
    //
    // 	jclass clsTelephonyManager =
    // pEnv->FindClass("android/telephony/TelephonyManager");
    // jmethodID getDeviceId = pEnv->GetMethodID(clsTelephonyManager,
    // "getDeviceId",
    // "()Ljava/lang/String;"); 	jstring jstrDevID =
    // (jstring)pEnv->CallObjectMethod(telephonyManager, getDeviceId);
    // if (jstrDevID) { 		const char *p =
    // pEnv->GetStringUTFChars(jstrDevID, 0); if (p
    // && *p) { 			ret = "DevID="; 			ret +=
    // p; pEnv->ReleaseStringUTFChars(jstrDevID, p); 		} else {
    // if (p) {
    // pEnv->ReleaseStringUTFChars(jstrDevID, p);
    // 			}
    // 			jmethodID getContentResolver =
    // pEnv->GetMethodID(classID, "getContentResolver",
    // "()Landroid/content/ContentResolver;"); 			jobject
    // contentResolver = pEnv->CallObjectMethod(sInstance,
    // getContentResolver);
    //
    // 			jclass clsSecure =
    // pEnv->FindClass("android/provider/Settings/Secure");
    // if (clsSecure) { 				jmethodID Secure_getString =
    // pEnv->GetMethodID(clsSecure, "getString",
    // "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;");
    // 				jstring jastrAndroid_ID =
    // pEnv->NewStringUTF("android_id"); 				jstring
    // jstrAndroidID =
    // (jstring)pEnv->CallStaticObjectMethod(clsSecure,
    // Secure_getString, contentResolver, jastrAndroid_ID);
    // if (jstrAndroidID) { 					const char *p =
    // pEnv->GetStringUTFChars(jstrAndroidID, 0); if (p && strlen(p) >
    // 8 && strcmp(p, "9774d56d682e549c")) { ret = "AndroidID="; ret
    // += p;
    // 					}
    // 				}
    // 				pEnv->ReleaseStringUTFChars(jstrAndroidID, p);
    // 				pEnv->DeleteLocalRef(jastrAndroid_ID);
    // 			}
    // 		}
    // 	}
    // 	if (ret.empty())
    {
        JniMethodInfo methodInfo;
        if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath,
                                          "getDeviceId",
                                          "()Ljava/lang/String;")) {
            auto result = (jstring)methodInfo.env->CallStaticObjectMethod(
                methodInfo.classID, methodInfo.methodID);
            ret = JniHelper::jstring2string(result);
            methodInfo.env->DeleteLocalRef(result);
            methodInfo.env->DeleteLocalRef(methodInfo.classID);
            char *t = (char *)ret.c_str();
            while(*t) {
                if(*t == ':') {
                    *t = '=';
                    break;
                }
                t++;
            }
        }
    }

    return ret;
}

static jobject GetKR2ActInstance() {
    JniMethodInfo methodInfo;
    std::string strtmp("()L");
    strtmp += KR2ActJavaPath;
    strtmp += ";";
    if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath, "GetInstance",
                                      strtmp.c_str())) {
        jobject ret = methodInfo.env->CallStaticObjectMethod(
            methodInfo.classID, methodInfo.methodID);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    return 0;
}

static std::string GetApkStoragePath() {
    JniMethodInfo methodInfo;
    jobject sInstance = GetKR2ActInstance();
    if(!JniHelper::getMethodInfo(methodInfo, "android/content/Context",
                                 "getApplicationInfo",
                                 "()Landroid/content/pm/ApplicationInfo;")) {
        methodInfo.env->DeleteLocalRef(sInstance);
        return "";
    }
    jobject ApplicationInfo =
        methodInfo.env->CallObjectMethod(sInstance, methodInfo.methodID);
    jclass clsApplicationInfo =
        methodInfo.env->FindClass("android/content/pm/ApplicationInfo");
    jfieldID id_sourceDir = methodInfo.env->GetFieldID(
        clsApplicationInfo, "sourceDir", "Ljava/lang/String;");
    methodInfo.env->DeleteLocalRef(sInstance);
    return JniHelper::jstring2string(
        (jstring)methodInfo.env->GetObjectField(ApplicationInfo, id_sourceDir));
}

static std::string GetPackageName() {
    JniMethodInfo methodInfo;
    jobject sInstance = GetKR2ActInstance();
    if(!JniHelper::getMethodInfo(methodInfo, "android/content/ContextWrapper",
                                 "getPackageName", "()Ljava/lang/String;")) {
        methodInfo.env->DeleteLocalRef(sInstance);
        return "";
    }
    return JniHelper::jstring2string((jstring)methodInfo.env->CallObjectMethod(
        sInstance, methodInfo.methodID));
}

// from unzip.cpp
#define FLAG_UTF8 (1 << 11)
extern zlib_filefunc64_def TVPZlibFileFunc;
class ZipFile {
    unzFile uf;
    bool utf8{};

public:
    ZipFile() : uf(nullptr) {}
    ~ZipFile() {
        if(uf) {
            unzClose(uf);
            uf = nullptr;
        }
    }
    bool Open(const char *filename) {
        if((uf = unzOpen(filename)) == nullptr) {
            ttstr msg = filename;
            msg += TJS_W(" can't open.");
            TVPThrowExceptionMessage(msg.c_str());
            return false;
        }
        // UTF8¤Ê¥Õ¥¡¥¤¥ëÃû¤«¤É¤¦¤«¤ÎÅÐ¶¨¡£×î³õ¤Î¥Õ¥¡¥¤¥ë¤Ç›Q¤á¤ë
        unzGoToFirstFile(uf);
        unz_file_info file_info;
        if(unzGetCurrentFileInfo(uf, &file_info, nullptr, 0, nullptr, 0,
                                 nullptr, 0) == UNZ_OK) {
            utf8 = (file_info.flag & FLAG_UTF8) != 0;
            return true;
        }
        return false;
    }
    bool GetData(std::vector<unsigned char> &buff, const char *filename) {
        bool ret = false;
        if(unzLocateFile(uf, filename, 0) == UNZ_OK) {
            int result = unzOpenCurrentFile(uf);
            if(result == UNZ_OK) {
                unz_file_info info;
                unzGetCurrentFileInfo(uf, &info, nullptr, 0, nullptr, 0,
                                      nullptr, 0);
                buff.resize(info.uncompressed_size);
                unsigned int size =
                    unzReadCurrentFile(uf, &buff[0], info.uncompressed_size);
                if(size == info.uncompressed_size)
                    ret = true;
                unzCloseCurrentFile(uf);
            }
        }
        return ret;
    }
    tjs_int64 GetMD5InZip(const char *filename) {
        std::vector<unsigned char> buff;
        if(!GetData(buff, filename))
            return 0;
        md5_state_t state;
        md5_init(&state);
        md5_append(&state, (const md5_byte_t *)&buff[0], buff.size());
        union {
            tjs_int64 _s64[2];
            tjs_uint8 _u8[16];
        } digest{};
        md5_finish(&state, digest._u8);
        return digest._s64[0] ^ digest._s64[1];
    }

private:
    unzFile zipFile{};
};

std::string TVPGetDeviceLanguage() {
    // use pure jni to avoid java code
    JniMethodInfo methodInfo;
    if(!JniHelper::getStaticMethodInfo(methodInfo, "java/util/Locale",
                                       "getDefault", "()Ljava/util/Locale;"))
        return "";
    jobject LocaleObj = methodInfo.env->CallStaticObjectMethod(
        methodInfo.classID, methodInfo.methodID);
    if(!JniHelper::getMethodInfo(methodInfo, "java/util/Locale", "getLanguage",
                                 "()Ljava/lang/String;"))
        return "";
    jstring languageID = (jstring)methodInfo.env->CallObjectMethod(
        LocaleObj, methodInfo.methodID);
    methodInfo.env->DeleteLocalRef(methodInfo.classID);
    return JniHelper::jstring2string(languageID);
}

std::string TVPGetPackageVersionString() {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath, "GetVersion",
                                      "()Ljava/lang/String;")) {
        return JniHelper::jstring2string(
            (jstring)methodInfo.env->CallStaticObjectMethod(
                methodInfo.classID, methodInfo.methodID));
    }
    return "";
}

static std::vector<std::string> &split(const std::string &s, char delim,
                                       std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while(std::getline(ss, item, delim)) {
        elems.emplace_back(item);
    }
    return elems;
}

static std::string File_getAbsolutePath(jobject FileObj) {
    if(!FileObj)
        return "";
    JniMethodInfo methodInfo;
    if(!JniHelper::getMethodInfo(methodInfo, "java/io/File", "exists", "()Z"))
        return "";
    if(!methodInfo.env->CallBooleanMethod(FileObj, methodInfo.methodID))
        return "";
    if(!JniHelper::getMethodInfo(methodInfo, "java/io/File", "getAbsolutePath",
                                 "()Ljava/lang/String;"))
        return "";
    jstring path =
        (jstring)methodInfo.env->CallObjectMethod(FileObj, methodInfo.methodID);
    std::string ret = JniHelper::jstring2string(path);
    return ret;
}

static std::string GetInternalStoragePath() {
    jobject sInstance = GetKR2ActInstance();
    JniMethodInfo methodInfo;
    if(!JniHelper::getMethodInfo(methodInfo, "android/content/ContextWrapper",
                                 "getFilesDir", "()Ljava/io/File;")) {
        return "";
    }
    jobject FileObj =
        methodInfo.env->CallObjectMethod(sInstance, methodInfo.methodID);
    return File_getAbsolutePath(FileObj);
}

std::string Android_GetDumpStoragePath() {
    return GetInternalStoragePath() + "/dump";
}

static int InsertFilepathInto(JNIEnv *env, std::vector<std::string> &vec,
                              jobjectArray FileObjs) {
    int count = env->GetArrayLength(FileObjs);
    for(int i = 0; i < count; ++i) {
        jobject FileObj = env->GetObjectArrayElement(FileObjs, i);
        std::string path = File_getAbsolutePath(FileObj);
        if(!path.empty())
            vec.emplace_back(path);
    }
    return count;
}

static int GetExternalStoragePath(std::vector<std::string> &ret) {
    int count = 0;
    JniMethodInfo methodInfo;
    jobject sInstance = GetKR2ActInstance();
    // 	if (JniHelper::getMethodInfo(methodInfo,
    // "android/content/Context", "getExternalMediaDirs",
    // "()[Ljava/io/File;")) { 		jobjectArray FileObjs =
    // (jobjectArray)methodInfo.env->CallObjectMethod(sInstance,
    // methodInfo.methodID); 		if(FileObjs) count +=
    // InsertFilepathInto(methodInfo.env, ret, FileObjs);
    // 	}
    if(JniHelper::getMethodInfo(methodInfo, "android/content/Context",
                                "getExternalFilesDirs",
                                "(Ljava/lang/String;)[Ljava/io/File;")) {
        jobjectArray FileObjs = (jobjectArray)methodInfo.env->CallObjectMethod(
            sInstance, methodInfo.methodID, nullptr);
        if(FileObjs)
            count += InsertFilepathInto(methodInfo.env, ret, FileObjs);
    } else if(JniHelper::getMethodInfo(methodInfo, "android/content/Context",
                                       "getExternalFilesDir",
                                       "(Ljava/lang/String;)Ljava/io/File;")) {
        jobject FileObj = methodInfo.env->CallObjectMethod(
            sInstance, methodInfo.methodID, nullptr);
        if(FileObj) {
            ret.emplace_back(File_getAbsolutePath(FileObj));
            ++count;
        }
    }
    return count;
}

std::vector<std::string> TVPGetAppStoragePath() {
    std::vector<std::string> ret;
    ret.emplace_back(GetInternalStoragePath());
    GetExternalStoragePath(ret);
    return ret;
}

std::vector<std::string> TVPGetDriverPath() {
    std::vector<std::string> ret;
    jobject sInstance = GetKR2ActInstance();
    JniMethodInfo methodInfo;
    if(JniHelper::getMethodInfo(methodInfo, KR2ActJavaPath, "getStoragePath",
                                "()[Ljava/lang/String;")) {
        jobjectArray PathObjs = (jobjectArray)methodInfo.env->CallObjectMethod(
            sInstance, methodInfo.methodID);
        if(PathObjs) {
            int count = methodInfo.env->GetArrayLength(PathObjs);
            for(int i = 0; i < count; ++i) {
                jstring path =
                    (jstring)methodInfo.env->GetObjectArrayElement(PathObjs, i);
                if(path)
                    ret.emplace_back(JniHelper::jstring2string(path));
            }
        }
    }

    if(!ret.empty())
        return ret;

    char buffer[256] = { 0 };

    // enum all mounted storages
    FILE *fp = fopen("/proc/mounts", "r");
    std::set<std::string> mounted;
    while(fgets(buffer, sizeof(buffer), fp)) {
        std::vector<std::string> tabs;
        split(buffer, ' ', tabs);
        if(tabs.size() < 4)
            continue;
        if(mounted.find(tabs[0]) != mounted.end())
            continue;
        const std::string &path = tabs[1];
        if(tabs[3].find("rw,") != std::string::npos &&
           (tabs[2] == "vfat" || path.find("/mnt") != std::string::npos)) {
            if(path.find("/mnt/secure") != std::string::npos ||
               path.find("/mnt/asec") != std::string::npos ||
               path.find("/mnt/mapper") != std::string::npos ||
               path.find("/mnt/obb") != std::string::npos ||
               tabs[0] == "tmpfs" || tabs[2] == "tmpfs") {
                continue;
            }
            mounted.insert(tabs[0]);
            ret.emplace_back(path);
        }
    }

    return ret;
}

// extern "C" int TVPCheckValidate()
// {
//     JNIEnv *pEnv = 0;
//     bool ret = true;
//
//     if (! getEnv(&pEnv))
//     {
//         return false;
//     }
// 	{
// 		jclass classID = pEnv->FindClass(KR2EntryJavaPath);
// 		std::string strtmp("()L"); strtmp += KR2EntryJavaPath; strtmp
// +=
// ";"; 		jmethodID methodGetInstance =
// pEnv->GetStaticMethodID(classID, "GetInstance", strtmp.c_str());
// jobject sInstance = pEnv->CallStaticObjectMethod(classID,
// methodGetInstance);
//
// 		jclass clsPreferenceManager =
// pEnv->FindClass("android.preference.PreferenceManager"); jmethodID
// getDefaultSharedPreferences =
// pEnv->GetMethodID(clsPreferenceManager,
// "getDefaultSharedPreferences",
// "(Landroid/content/Context;)Landroid.preference.PreferenceManager;");
// jobject PreferenceManager =
// pEnv->CallStaticObjectMethod(clsPreferenceManager,
// getDefaultSharedPreferences, sInstance); 		jmethodID getString
// = pEnv->GetMethodID(clsPreferenceManager, "getString",
// "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
// jstring jstrConstAPPID = pEnv->NewStringUTF("APP_ID"); jstring
// jstrNull = pEnv->NewStringUTF(""); 		jstring jstrAPPID =
// (jstring)pEnv->CallObjectMethod(PreferenceManager, getString,
// jstrConstAPPID, jstrNull);
// pEnv->DeleteLocalRef(jstrConstAPPID);
// 		pEnv->DeleteLocalRef(jstrNull);
// 		const char *p = pEnv->GetStringUTFChars(jstrAPPID, 0);
// 		if(0x929e08af != adler32(0, (const Bytef*)p, strlen(p))) ret =
// false; 		pEnv->ReleaseStringUTFChars(jstrAPPID, p);
// 	}
//
//     return ret;
// }
namespace kr2android {
    std::condition_variable MessageBoxCond;
    std::mutex MessageBoxLock;
    int MsgBoxRet = -2;
    std::string MessageBoxRetText;
} // namespace kr2android
using namespace kr2android;

static const char *const kNativeFatalLogDirs[] = {
    "/storage/emulated/0/Android/data/org.github.krkr2/files",
    "/sdcard/Android/data/org.github.krkr2/files",
    "/sdcard",
    "/data/local/tmp",
    nullptr,
};

static int TVPGetCurrentNativeTid() {
    return static_cast<int>(syscall(SYS_gettid));
}

void TVPAppendNativeFatalBreadcrumb(const char *tag, const char *message) {
    for(int i = 0; kNativeFatalLogDirs[i]; ++i) {
        std::string path = kNativeFatalLogDirs[i];
        path += "/krkr2_native_fatal.log";
        FILE *fp = std::fopen(path.c_str(), "ab");
        if(!fp)
            continue;
        time_t now = time(nullptr);
        struct tm tmv;
        localtime_r(&now, &tmv);
        char ts[40] = { 0 };
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
        std::fprintf(fp,
                     "[%s] [breadcrumb] [%s] pid=%d tid=%d %s\n", ts,
                     tag ? tag : "native", getpid(), TVPGetCurrentNativeTid(),
                     message ? message : "");
        std::fclose(fp);
        __android_log_print(ANDROID_LOG_INFO, "KrKr2Breadcrumb", "[%s] %s",
                            tag ? tag : "native", message ? message : "");
        return;
    }
}

// krkr2-launcher: dump every messagebox text to a side file so OPPO logcat
// throttling cannot eat the very first crash dialog. Timestamp included so we
// can correlate with logcat windows.
static void TVPDumpFatalToFile(const char *pszText, const char *pszTitle) {
    // Try a few well-known external app-files directories. The launcher writes
    // its own log at /storage/emulated/0/Android/data/<pkg>/files/, so put our
    // fatal log next to it. Fall back to /sdcard and /data/local/tmp.
    for(int i = 0; kNativeFatalLogDirs[i]; ++i) {
        std::string path = kNativeFatalLogDirs[i];
        path += "/krkr2_native_fatal.log";
        FILE *fp = std::fopen(path.c_str(), "ab");
        if(!fp) continue;
        time_t now = time(nullptr);
        struct tm tmv;
        localtime_r(&now, &tmv);
        char ts[40] = {0};
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
        std::fprintf(fp, "[%s] ===== TVPShowSimpleMessageBox =====\n", ts);
        std::fprintf(fp, "Title: %s\n", pszTitle ? pszTitle : "(null)");
        std::fprintf(fp, "Text:\n%s\n", pszText ? pszText : "(null)");
        std::fprintf(fp, "----- end -----\n\n");
        std::fclose(fp);
        // Mirror to Android log too so the same line is recoverable from
        // logcat if quota allows.
        __android_log_print(ANDROID_LOG_FATAL, "KrKr2NativeFatal",
                            "MessageBox: %s\n%s", pszTitle ? pszTitle : "",
                            pszText ? pszText : "");
        return;
    }
}

int TVPShowSimpleMessageBox(const char *pszText, const char *pszTitle,
                            unsigned int nButton, const char **btnText) {
    // [krkr2-pro] Suppress duplicate fatal dialogs.
    //
    // Stock kirikiri raises "Member XXX does not exist" through a try/catch
    // boundary in KAGParser; KAG itself catches the throw, dispatches a
    // user-visible dialog, then returns to the same script position. When
    // the missing member is on a hot path (transition handler tables,
    // gesture probes, etc.) this loops at >50 Hz and produces hundreds of
    // identical message boxes that the user can't dismiss fast enough.
    //
    // We keep the *first* dialog of each unique (title|text) so the user
    // still sees the failure once. Subsequent identical raises within a
    // 5s window are coalesced and answered with "first button" without
    // touching JNI or the fatal log file. After 5s of silence the entry
    // expires so genuinely periodic failures still surface.
    {
        const char *titleKey = pszTitle ? pszTitle : "";
        const char *textKey = pszText ? pszText : "";
        std::string key;
        key.reserve(strlen(titleKey) + strlen(textKey) + 1);
        key.append(titleKey).append("\x01").append(textKey);

        static std::mutex s_dedupMutex;
        static std::map<std::string, std::chrono::steady_clock::time_point>
            s_dedupLast;
        constexpr auto kDedupWindow = std::chrono::seconds(5);

        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(s_dedupMutex);
        // Drop entries that have aged out so the map can't grow unbounded
        // when a long session triggers hundreds of distinct failures.
        for(auto it = s_dedupLast.begin(); it != s_dedupLast.end();) {
            if(now - it->second > kDedupWindow * 6)
                it = s_dedupLast.erase(it);
            else
                ++it;
        }
        auto it = s_dedupLast.find(key);
        if(it != s_dedupLast.end() && now - it->second < kDedupWindow) {
            it->second = now;
            __android_log_print(ANDROID_LOG_WARN, "KrKr2NativeFatal",
                                "MessageBox suppressed (dup within %lld ms): %s",
                                (long long)std::chrono::duration_cast<
                                    std::chrono::milliseconds>(now - it->second)
                                    .count(),
                                pszText ? pszText : "");
            return 0; // pretend user clicked the first button
        }
        s_dedupLast[key] = now;
    }

    // Always dump first so even if the JNI dialog deadlocks the main thread,
    // the user can pull the message text from the file.
    TVPDumpFatalToFile(pszText, pszTitle);

    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(
           methodInfo, "org/tvp/kirikiri2/KR2Activity", "ShowMessageBox",
           "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/"
           "String;)V")) {
        jstring jstrTitle = methodInfo.env->NewStringUTF(pszTitle);
        jstring jstrText = methodInfo.env->NewStringUTF(pszText);
        jclass strcls = methodInfo.env->FindClass("java/lang/String");
        jobjectArray btns =
            methodInfo.env->NewObjectArray(nButton, strcls, nullptr);
        for(unsigned int i = 0; i < nButton; ++i) {
            jstring jstrBtn = methodInfo.env->NewStringUTF(btnText[i]);
            methodInfo.env->SetObjectArrayElement(btns, i, jstrBtn);
            methodInfo.env->DeleteLocalRef(jstrBtn);
        }

        methodInfo.env->CallStaticVoidMethod(
            methodInfo.classID, methodInfo.methodID, jstrTitle, jstrText, btns);

        methodInfo.env->DeleteLocalRef(jstrTitle);
        methodInfo.env->DeleteLocalRef(jstrText);
        methodInfo.env->DeleteLocalRef(btns);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);

        // Cap the wait at 30s so the GLThread cannot stay blocked long enough
        // to ANR the main thread (5s input dispatch timeout) while it is
        // mid-layout. If the dialog is dismissed earlier we exit immediately;
        // if not, we treat it as the default "OK"/first button and continue
        // (or, in fatal contexts, the engine will keep cascading errors that
        // the launcher log already captured).
        constexpr int kWaitTimeoutMs = 30000;
        constexpr int kStepMs = 200;
        int elapsedMs = 0;
        std::unique_lock<std::mutex> lk(MessageBoxLock);
        while(MsgBoxRet == -2 && elapsedMs < kWaitTimeoutMs) {
            MessageBoxCond.wait_for(lk, std::chrono::milliseconds(kStepMs));
            if(MsgBoxRet == -2) {
                TVPForceSwapBuffer(); // update opengl events
                elapsedMs += kStepMs;
            }
        }
        if(MsgBoxRet == -2) {
            __android_log_print(ANDROID_LOG_WARN, "KrKr2NativeFatal",
                                "MessageBox timed out after %d ms; assuming OK",
                                kWaitTimeoutMs);
            return 0; // pretend user clicked the first button
        }
        return MsgBoxRet;
    }
    return -1;
}

int TVPShowSimpleMessageBox(const ttstr &text, const ttstr &caption,
                            const std::vector<ttstr> &vecButtons) {
    tTJSNarrowStringHolder pszText(text.c_str());
    tTJSNarrowStringHolder pszTitle(caption.c_str());
    std::vector<const char *> btnText;
    btnText.reserve(vecButtons.size());
    std::vector<std::string> btnTextHold;
    btnTextHold.reserve(vecButtons.size());
    for(const ttstr &btn : vecButtons) {
        btnTextHold.emplace_back(btn.AsStdString());
        btnText.emplace_back(btnTextHold.back().c_str());
    }
    return TVPShowSimpleMessageBox(pszText, pszTitle, btnText.size(),
                                   &btnText[0]);
}

int TVPShowSimpleInputBox(ttstr &text, const ttstr &caption,
                          const ttstr &prompt,
                          const std::vector<ttstr> &vecButtons) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(
           methodInfo, "org/tvp/kirikiri2/KR2Activity", "ShowInputBox",
           "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/"
           "String;[Ljava/"
           "lang/"
           "String;)V")) {
        jstring jstrTitle =
            methodInfo.env->NewStringUTF(caption.AsStdString().c_str());
        jstring jstrText =
            methodInfo.env->NewStringUTF(text.AsStdString().c_str());
        jstring jstrPrompt =
            methodInfo.env->NewStringUTF(prompt.AsStdString().c_str());
        jclass strcls = methodInfo.env->FindClass("java/lang/String");
        jobjectArray btns =
            methodInfo.env->NewObjectArray(vecButtons.size(), strcls, nullptr);
        for(unsigned int i = 0; i < vecButtons.size(); ++i) {
            jstring jstrBtn = methodInfo.env->NewStringUTF(
                vecButtons[i].AsStdString().c_str());
            methodInfo.env->SetObjectArrayElement(btns, i, jstrBtn);
            methodInfo.env->DeleteLocalRef(jstrBtn);
        }

        MsgBoxRet = -2;
        methodInfo.env->CallStaticVoidMethod(methodInfo.classID,
                                             methodInfo.methodID, jstrTitle,
                                             jstrPrompt, jstrText, btns);

        methodInfo.env->DeleteLocalRef(jstrTitle);
        methodInfo.env->DeleteLocalRef(jstrText);
        methodInfo.env->DeleteLocalRef(jstrPrompt);
        methodInfo.env->DeleteLocalRef(btns);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);

        std::unique_lock<std::mutex> lk(MessageBoxLock);
        while(MsgBoxRet == -2) {
            MessageBoxCond.wait_for(lk, std::chrono::milliseconds(200));
            if(MsgBoxRet == -2) {
                TVPForceSwapBuffer(); // update opengl events
            }
        }
        text = MessageBoxRetText;
        return MsgBoxRet;
    }
    return -1;
}

extern std::string Android_ShowInputDialog(const char *pszTitle,
                                           const char *pszInitText);
extern std::string Android_ShowFileBrowser(const char *pszTitle,
                                           const char *pszInitDir,
                                           bool showEditor);
extern ttstr TVPGetAppPath();
extern ttstr TVPGetLocallyAccessibleName(const ttstr &name);

std::vector<ttstr> Android_GetExternalStoragePath() {
    static std::vector<ttstr> ret;
    if(ret.empty()) {
        std::vector<std::string> pathlist;
        GetExternalStoragePath(pathlist);
        for(const std::string &path : pathlist) {
            std::string strPath = "file://.";
            strPath += path;
            ret.emplace_back(strPath);
        }
    }
    return ret;
}

ttstr Android_GetInternalStoragePath() {
    static ttstr strPath;
    if(strPath.IsEmpty()) {
        strPath = "file://.";
        strPath += GetInternalStoragePath();
    }
    return strPath;
}

ttstr Android_GetApkStoragePath() {
    static ttstr strPath;
    if(strPath.IsEmpty()) {
        strPath = "file://.";
        strPath += GetApkStoragePath();
    }
    return strPath;
}

struct _eventQueueNode {
    std::function<void()> func;
    _eventQueueNode *prev;
    _eventQueueNode *next;
};

static std::atomic<_eventQueueNode *> _lastQueuedEvents(nullptr);
static void _processEvents(float) {
    _eventQueueNode *q = _lastQueuedEvents.exchange(nullptr);
    if(q) {
        q->next = nullptr;
        while(q->prev) {
            q->prev->next = q;
            q = q->prev;
        }
    }
    while(q) {
        q->func();
        _eventQueueNode *nq = q->next;
        delete q;
        q = nq;
    }
}

void Android_PushEvents(const std::function<void()> &func) {
    _eventQueueNode *node = new _eventQueueNode;
    node->func = func;
    node->next = nullptr;
    node->prev = nullptr;
    while(!_lastQueuedEvents.compare_exchange_weak(node->prev, node)) {
    }
}

std::string Android_GetLaunchGamePath() {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath,
                                      "getLaunchGamePath",
                                      "()Ljava/lang/String;")) {
        auto result = (jstring)methodInfo.env->CallStaticObjectMethod(
            methodInfo.classID, methodInfo.methodID);
        std::string ret = result ? JniHelper::jstring2string(result) : "";
        if(result)
            methodInfo.env->DeleteLocalRef(result);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    return "";
}

std::string TVPGetLaunchGamePath() { return Android_GetLaunchGamePath(); }

std::string TVPGetLaunchGameDir() {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath,
                                      "getLaunchGameDir",
                                      "()Ljava/lang/String;")) {
        auto result = (jstring)methodInfo.env->CallStaticObjectMethod(
            methodInfo.classID, methodInfo.methodID);
        std::string ret = result ? JniHelper::jstring2string(result) : "";
        if(result)
            methodInfo.env->DeleteLocalRef(result);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    return "";
}

void TVPCheckAndSendDumps(const std::string &dumpdir,
                          const std::string &packageName,
                          const std::string &versionStr);
bool TVPCheckStartupArg() {
    // check dump
    TVPCheckAndSendDumps(Android_GetDumpStoragePath(), GetPackageName(),
                         TVPGetPackageVersionString());

#if KRKR2_ENABLE_COCOS_HOST
    // register event dispatcher
    cocos2d::Director *director = cocos2d::Director::getInstance();
    class HackForScheduler : public cocos2d::Scheduler {
    public:
        void regProcessEvents() {
            schedulePerFrame(_processEvents, &_lastQueuedEvents, -1, false);
        }
    };
    static_cast<HackForScheduler *>(director->getScheduler())
        ->regProcessEvents();
#endif

    return false;
}

void TVPControlAdDialog(int adType, int arg1, int arg2) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo,
                                      "org/tvp/kirikiri2/KR2Activity",
                                      "MessageController", "(III)V")) {
        methodInfo.env->CallStaticVoidMethod(
            methodInfo.classID, methodInfo.methodID, adType, arg1, arg2);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
    }
}

static int _GetAndroidSDKVersion() {
    JNIEnv *pEnv = JniHelper::getEnv();
    jclass classID = pEnv->FindClass("android/os/Build$VERSION");
    jfieldID idSDK_INT = pEnv->GetStaticFieldID(classID, "SDK_INT", "I");
    return pEnv->GetStaticIntField(classID, idSDK_INT);
}
static int GetAndroidSDKVersion() {
    static int result = _GetAndroidSDKVersion();
    return result;
}

static bool IsLollipop() { return GetAndroidSDKVersion() >= 21; }

static bool IsOreo() { return GetAndroidSDKVersion() >= 26; }

// 这里的编码就要使用locale编码了
// 因为调用了tjstr参数的函数 tjstr处理不了utf-8编码
bool TVPCheckStartupPath(const std::string &path) {
    // check writing permission first
    size_t pos = path.find_last_of('/');
    if(pos == std::string::npos)
        return false;
    std::string parent = path.substr(0, pos);
    JniMethodInfo methodInfo;
    bool success = false;
    if(JniHelper::getStaticMethodInfo(
           methodInfo, "org/tvp/kirikiri2/KR2Activity", "isWritableNormalOrSaf",
           "(Ljava/lang/String;)Z")) {
        jstring jstrPath = methodInfo.env->NewStringUTF(parent.c_str());
        success = methodInfo.env->CallStaticBooleanMethod(
            methodInfo.classID, methodInfo.methodID, jstrPath);
        methodInfo.env->DeleteLocalRef(jstrPath);
        if(success) {
            parent += "/savedata";
            if(!TVPCheckExistentLocalFolder(parent)) {
                TVPCreateFolders(parent);
            }
            jstrPath = methodInfo.env->NewStringUTF(parent.c_str());
            success = methodInfo.env->CallStaticBooleanMethod(
                methodInfo.classID, methodInfo.methodID, jstrPath);
            methodInfo.env->DeleteLocalRef(jstrPath);
        }
    }

    if(!success) {
        std::vector<std::string> paths;
        paths.emplace_back(GetInternalStoragePath());
        GetExternalStoragePath(paths);
        std::string msg =
            LocaleConfigManager::GetInstance()->GetText("use_internal_path");
        if(!paths.empty()) {
            pos = msg.find("%1");
            if(pos != std::string::npos) {
                msg = msg.replace(msg.begin() + pos, msg.begin() + pos + 2,
                                  paths.back());
            }
        }
        std::vector<ttstr> btns;
        btns.emplace_back(
            LocaleConfigManager::GetInstance()->GetText("continue_run"));
        bool isLOLLIPOP = IsLollipop();
        if(isLOLLIPOP)
            btns.emplace_back(LocaleConfigManager::GetInstance()->GetText(
                "get_sdcard_permission"));
        else
            btns.emplace_back(
                LocaleConfigManager::GetInstance()->GetText("cancel"));
        int result = TVPShowSimpleMessageBox(
            msg,
            LocaleConfigManager::GetInstance()->GetText("readonly_storage"),
            btns);

        if(result != 0)
            return false;
    }

    // check adreno GPU issue
    // 	if
    // (IndividualConfigManager::GetInstance()->GetValue<std::string>("renderer",
    // "software") == "opengl") {
    // TVPOnOpenGLRendererSelected(false);
    // 	}
    return true;
}

bool TVPCreateFolders(const ttstr &folder) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(
           methodInfo, "org/tvp/kirikiri2/KR2Activity", "CreateFolders",
           "(Ljava/lang/String;)Z")) {
        jstring jstr =
            methodInfo.env->NewStringUTF(folder.AsStdString().c_str());
        bool ret = methodInfo.env->CallStaticBooleanMethod(
            methodInfo.classID, methodInfo.methodID, jstr);
        methodInfo.env->DeleteLocalRef(jstr);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    return false;
}

static bool TVPWriteDataToFileJava(const std::string &filename,
                                   const void *data, unsigned int size) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo,
                                      "org/tvp/kirikiri2/KR2Activity",
                                      "WriteFile", "(Ljava/lang/String;[B)Z")) {
        bool ret = false;
        int retry = 3;
        do {
            jstring jstr = methodInfo.env->NewStringUTF(filename.c_str());
            jbyteArray arr = methodInfo.env->NewByteArray(size);
            methodInfo.env->SetByteArrayRegion(arr, 0, size, (jbyte *)data);
            ret = methodInfo.env->CallStaticBooleanMethod(
                methodInfo.classID, methodInfo.methodID, jstr, arr);
            methodInfo.env->DeleteLocalRef(arr);
            methodInfo.env->DeleteLocalRef(jstr);
        } while(!TVPAndroidPathExists(filename) && --retry);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    return false;
}

bool TVPWriteDataToFile(const ttstr &filepath, const void *data,
                        unsigned int size) {
    std::string filename = filepath.AsStdString();
    while(TVPAndroidPathExists(filename)) {
        // for number filename suffix issue
        time_t t = time(nullptr);
        std::vector<char> buffer;
        buffer.resize(filename.size() + 32);
        sprintf(&buffer.front(), "%s.%d.bak", filename.c_str(), (int)t);
        std::string tempname = &buffer.front();
        if(rename(filename.c_str(), tempname.c_str()) == 0) {
            // file api is OK
            FILE *fp = fopen(filename.c_str(), "wb");
            if(fp) {
                bool ret = fwrite(data, 1, size, fp) == size;
                fclose(fp);
                remove(tempname.c_str());
                return ret;
            }
        }
        bool ret = TVPWriteDataToFileJava(filename, data, size);
        if(TVPAndroidPathExists(tempname)) {
            TVPDeleteFile(tempname);
        }
        return ret;
    }
    FILE *fp = fopen(filename.c_str(), "wb");
    if(fp) {
        // file api is OK
        int writed = fwrite(data, 1, size, fp);
        fclose(fp);
        return writed == size;
    }
    return TVPWriteDataToFileJava(filename, data, size);
}

std::string TVPGetCurrentLanguage() {
    JniMethodInfo t;
    std::string ret("");

    if(JniHelper::getStaticMethodInfo(t, "org/tvp/kirikiri2/KR2Activity",
                                      "getLocaleName",
                                      "()Ljava/lang/String;")) {
        jstring str =
            (jstring)t.env->CallStaticObjectMethod(t.classID, t.methodID);
        t.env->DeleteLocalRef(t.classID);
        ret = JniHelper::jstring2string(str);
        t.env->DeleteLocalRef(str);
    }

    return ret;
}

void TVPExitApplication(int code) {
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
    if(!TVPIsSoftwareRenderManager())
        iTVPTexture2D::RecycleProcess();
    JniMethodInfo t;
    if(JniHelper::getStaticMethodInfo(t, "org/tvp/kirikiri2/KR2Activity",
                                      "exit", "()V")) {
        t.env->CallStaticVoidMethod(t.classID, t.methodID);
        t.env->DeleteLocalRef(t.classID);
    }
    exit(code);
}

void TVPHideIME() {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo,
                                      "org/tvp/kirikiri2/KR2Activity",
                                      "hideTextInput", "()V")) {
        methodInfo.env->CallStaticVoidMethod(methodInfo.classID,
                                             methodInfo.methodID);
    }
}

void TVPShowIME(int x, int y, int w, int h) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo,
                                      "org/tvp/kirikiri2/KR2Activity",
                                      "showTextInput", "(IIII)V")) {
        methodInfo.env->CallStaticVoidMethod(methodInfo.classID,
                                             methodInfo.methodID, x, y, w, h);
    }
}

void TVPProcessInputEvents() {}

bool TVPDeleteFile(const std::string &filename) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo,
                                      "org/tvp/kirikiri2/KR2Activity",
                                      "DeleteFile", "(Ljava/lang/String;)Z")) {
        jstring jstr = methodInfo.env->NewStringUTF(filename.c_str());
        bool ret = methodInfo.env->CallStaticBooleanMethod(
            methodInfo.classID, methodInfo.methodID, jstr);
        methodInfo.env->DeleteLocalRef(jstr);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    return false;
}

bool TVPRenameFile(const std::string &from, const std::string &to) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(
           methodInfo, "org/tvp/kirikiri2/KR2Activity", "RenameFile",
           "(Ljava/lang/String;Ljava/lang/String;)Z")) {
        jstring jstr = methodInfo.env->NewStringUTF(from.c_str());
        jstring jstr2 = methodInfo.env->NewStringUTF(to.c_str());
        bool ret = methodInfo.env->CallStaticBooleanMethod(
            methodInfo.classID, methodInfo.methodID, jstr, jstr2);
        methodInfo.env->DeleteLocalRef(jstr);
        methodInfo.env->DeleteLocalRef(jstr2);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    return false;
}

tjs_uint32 TVPGetRoughTickCount32() {
    tjs_uint32 uptime = 0;
    struct timespec on;
    if(clock_gettime(CLOCK_MONOTONIC, &on) == 0)
        uptime = on.tv_sec * 1000 + on.tv_nsec / 1000000;
    return uptime;
}

bool TVP_stat(const tjs_char *name, tTVP_stat &s) {
    tTJSNarrowStringHolder holder(name);
    return TVP_stat(holder, s);
}

#undef st_atime
#undef st_ctime
#undef st_mtime
// int stat64(const char* __path, struct stat64* __buf)
// __INTRODUCED_IN(21); // force link it !
bool TVP_stat(const char *name, tTVP_stat &s) {
    struct stat t;
    // static_assert(sizeof(t.st_size) == 4, "");
    static_assert(sizeof(t.st_size) == 8, "");
    bool ret = !stat(name, &t);
    s.st_mode = t.st_mode;
    s.st_size = t.st_size;
    s.st_atime = t.st_atim.tv_sec;
    s.st_mtime = t.st_mtim.tv_sec;
    s.st_ctime = t.st_ctim.tv_sec;
    return ret;
}

void TVPSendToOtherApp(const std::string &filename) {}
