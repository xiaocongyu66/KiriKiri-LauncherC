//
// Created by LiDon on 2025/9/15.
//
#pragma once
#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "tjs.h"

namespace PSB {
    class PSBFile;
}

namespace motion {

    class ResourceManager {
    public:
        explicit ResourceManager() = default;

        explicit ResourceManager(iTJSDispatch2 *kag, tjs_int cacheSize);

        tTJSVariant load(ttstr path) const;
        void unload(const ttstr &path) const;
        void clearCache() const;
        static ttstr getLastLoadedPath();
        static bool hasLoadedPath(const ttstr &path);
        static int getDecryptSeed();
        static std::shared_ptr<PSB::PSBFile> getLoadedFile(const ttstr &path);

        static tjs_error setEmotePSBDecryptSeed(tTJSVariant *r, tjs_int count,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

        static tjs_error setEmotePSBDecryptFunc(tTJSVariant *r, tjs_int n,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

    private:
        static void trimCacheLocked();

        inline static int _decryptSeed;
        inline static size_t _cacheLimit = 32;
        inline static std::mutex _mutex;
        inline static std::deque<std::string> _cacheOrder;
        inline static std::string _lastLoadedPath;
        inline static std::unordered_map<std::string, std::shared_ptr<PSB::PSBFile>>
            _loadedFiles;
    };
} // namespace motion
