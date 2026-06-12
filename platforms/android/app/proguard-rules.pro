# Add project specific ProGuard rules here.
# By default, the flags in this file are appended to flags specified
# in <SDK>/tools/proguard/proguard-android.txt
# You can edit the include path and order by changing the proguardFiles
# directive in build.gradle.

############################################################
# KRKR2 / cocos2d-x / SDL3 — native FindClass / JNI bindings
#
# These classes are looked up from native code via
# JNIEnv::FindClass / GetMethodID / RegisterNatives, so R8 cannot
# see the references and would strip / rename them, causing
#   "ClassNotFoundException: org.libsdl.app.SDLActivity"
#   "UnsatisfiedLinkError: No implementation found for ..."
# in release builds.
############################################################

# SDL3 — referenced from libSDL3.so JNI_OnLoad (FindClass "org/libsdl/app/SDLActivity")
-keep class org.libsdl.app.** { *; }
-keepnames class org.libsdl.app.**
-keep interface org.libsdl.app.** { *; }

# cocos2d-x — referenced from libkrkr2.so (Cocos2dxActivity, Cocos2dxHelper, Cocos2dxRenderer, etc.)
-keep class org.cocos2dx.lib.** { *; }
-keepnames class org.cocos2dx.lib.**
-keep interface org.cocos2dx.lib.** { *; }

# KR2Activity (kirikiri2 native bridge) — native methods + JNI callbacks
-keep class org.tvp.kirikiri2.** { *; }
-keepnames class org.tvp.kirikiri2.**

# Application package — keep activities/services referenced from manifest + JNI helpers
-keep class org.github.krkr2.** { *; }
-keepnames class org.github.krkr2.**

# Generic: keep every native method and the class that declares it
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}

# Keep any class with @Keep annotation
-keep @androidx.annotation.Keep class * { *; }
-keepclassmembers class * {
    @androidx.annotation.Keep <fields>;
    @androidx.annotation.Keep <methods>;
}

# Compose / Kotlin reflection metadata sanity
-keepattributes *Annotation*, Signature, InnerClasses, EnclosingMethod
-dontwarn org.libsdl.app.**
-dontwarn org.cocos2dx.lib.**
-dontwarn org.tvp.kirikiri2.**

############################################################
# Vendor SDKs bundled by cocos2d-x (libcocos2dx/java/libs/oppoSDK.jar)
#
# OPPO's OifaceGameEngineManager references hidden Android APIs
# (android.os.ServiceManager / android.util.Slog) that are not in
# the public SDK. R8 in AGP 8 treats these as missing-class errors
# and fails minifyReleaseWithR8. The runtime call paths are guarded
# by SDK checks inside Cocos2dxDataManager, so we only need to tell
# R8 to ignore the dangling references.
############################################################
-dontwarn com.oppo.oiface.engine.**
-dontwarn android.os.ServiceManager
-dontwarn android.util.Slog
# Belt-and-suspenders: keep the OPPO bridge so reflective lookups
# from Cocos2dxDataManager don't get pruned.
-keep class com.oppo.oiface.engine.** { *; }
-keepnames class com.oppo.oiface.engine.**
