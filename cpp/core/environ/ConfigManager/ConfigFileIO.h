#pragma once

#include <string>

bool TVPConfigFileExists(const std::string &path);
bool TVPLoadConfigFileText(const std::string &path, std::string *text);
bool TVPLoadBundledConfigText(const std::string &logicalPath,
                              std::string *text,
                              std::string *resolvedPath = nullptr);
