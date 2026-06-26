#pragma once

#ifdef __ANDROID__

#include <jni.h>
#include <string>

namespace krkr {

class JniHelper {
public:
    static void setJavaVM(JavaVM *vm);
    static JavaVM *getJavaVM();
    static JNIEnv *getEnv();
    static std::string jstring2string(jstring str);

    struct MethodInfo {
        JNIEnv *env = nullptr;
        jclass classID = nullptr;
        jmethodID methodID = nullptr;
    };

    static bool getStaticMethodInfo(MethodInfo &info, const char *className,
                                    const char *methodName,
                                    const char *signature);
    static bool getMethodInfo(MethodInfo &info, const char *className,
                              const char *methodName,
                              const char *signature);
};

} // namespace krkr

using JniMethodInfo = krkr::JniHelper::MethodInfo;

#endif // __ANDROID__
