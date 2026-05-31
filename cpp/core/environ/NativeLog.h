#pragma once

#include <string>

void TVPInitializeNativeLogging();
void TVPConfigureNativeLogging(bool fileLoggingEnabled,
                               const std::string &logFilePath);
void TVPNativeLogInfo(const char *tag, const char *message);

extern "C" void KR2RenderProbeWriteF(const char *fmt, ...);
