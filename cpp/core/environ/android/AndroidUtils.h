#pragma once
#include <jni.h>
#include <string>

std::string TVPGetDeviceID();
void TVPAppendNativeFatalBreadcrumb(const char *tag, const char *message);
