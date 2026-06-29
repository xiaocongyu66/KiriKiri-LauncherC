#include "ConfigFileIO.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace {

struct SDLFreeDeleter {
    void operator()(void *ptr) const { SDL_free(ptr); }
};

std::string JoinPath(const char *prefix, const std::string &path) {
    if(!prefix || !*prefix)
        return path;
    std::string joined(prefix);
    if(!joined.empty() && joined.back() != '/' && joined.back() != '\\')
        joined.push_back('/');
    joined += path;
    return joined;
}

bool LoadWithSDL(const std::string &path, std::string *text) {
    if(!text || path.empty())
        return false;

    size_t size = 0;
    std::unique_ptr<void, SDLFreeDeleter> data(SDL_LoadFile(path.c_str(), &size));
    if(!data)
        return false;

    const char *bytes = static_cast<const char *>(data.get());
    text->assign(bytes, bytes + size);
    return true;
}

bool ReadFile(FILE *file, std::string *text) {
    if(!file || !text)
        return false;

    std::string out;
    char buffer[4096];
    for(;;) {
        const size_t n = std::fread(buffer, 1, sizeof(buffer), file);
        if(n)
            out.append(buffer, n);
        if(n < sizeof(buffer)) {
            if(std::ferror(file))
                return false;
            break;
        }
    }
    *text = std::move(out);
    return true;
}

bool LoadWithStdio(const std::string &path, std::string *text) {
    if(!text || path.empty())
        return false;

    FILE *file = std::fopen(path.c_str(), "rb");
    if(!file)
        return false;
    const bool ok = ReadFile(file, text);
    std::fclose(file);
    return ok;
}

bool TryLoadBundledPath(const std::string &path, std::string *text,
                        std::string *resolvedPath) {
    if(LoadWithSDL(path, text) || LoadWithStdio(path, text)) {
        if(resolvedPath)
            *resolvedPath = path;
        return true;
    }
    return false;
}

} // namespace

bool TVPConfigFileExists(const std::string &path) {
    if(path.empty())
        return false;

    FILE *file = std::fopen(path.c_str(), "rb");
    if(!file)
        return false;
    std::fclose(file);
    return true;
}

bool TVPLoadConfigFileText(const std::string &path, std::string *text) {
    return LoadWithStdio(path, text);
}

bool TVPLoadBundledConfigText(const std::string &logicalPath,
                              std::string *text, std::string *resolvedPath) {
    if(!text || logicalPath.empty())
        return false;

    static const char *kResourcePrefixes[] = {
        "",
        "ui/cocos-studio",
        "flutter_launcher/assets/cocos-studio",
        "Resources",
    };

    for(const char *prefix : kResourcePrefixes) {
        const std::string candidate = JoinPath(prefix, logicalPath);
        if(TryLoadBundledPath(candidate, text, resolvedPath))
            return true;
    }

    const char *basePath = SDL_GetBasePath();
    if(basePath) {
        for(const char *prefix : kResourcePrefixes) {
            const std::string candidate =
                JoinPath(basePath, JoinPath(prefix, logicalPath));
            if(TryLoadBundledPath(candidate, text, resolvedPath))
                return true;
        }
    }

    return false;
}
