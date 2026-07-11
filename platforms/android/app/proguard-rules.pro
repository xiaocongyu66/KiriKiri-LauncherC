# Add project specific ProGuard rules here.
# By default, the flags in this file are appended to flags specified
# in <SDK>/tools/proguard/proguard-android.txt
# You can edit the include path and order by changing the proguardFiles
# directive in build.gradle.

############################################################
# KRKR2 / SDL3 — native FindClass / JNI bindings
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

# KR2Activity (kirikiri2 native bridge) — native methods + JNI callbacks
-keep class org.tvp.kirikiri2.** { *; }
-keepnames class org.tvp.kirikiri2.**

# Application package — keep activities/services referenced from manifest + JNI helpers
-keep class org.github.krkr2.** { *; }
-keepnames class org.github.krkr2.**

# Flutter game surface reflection — kept for the SDL overlay bridge when older
# embeddings need to fall back from SurfaceProducer to SurfaceTextureEntry.
-keep class io.flutter.embedding.engine.renderer.FlutterRenderer { *; }
-keep interface io.flutter.view.TextureRegistry { *; }
-keep enum io.flutter.view.TextureRegistry$SurfaceLifecycle { *; }
-keep interface io.flutter.view.TextureRegistry$SurfaceProducer { *; }
-keep interface io.flutter.view.TextureRegistry$SurfaceProducer$Callback { *; }
-dontwarn io.flutter.view.TextureRegistry
-dontwarn io.flutter.view.TextureRegistry$SurfaceLifecycle
-dontwarn io.flutter.view.TextureRegistry$SurfaceProducer
-dontwarn io.flutter.view.TextureRegistry$SurfaceProducer$Callback

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
-dontwarn org.tvp.kirikiri2.**
