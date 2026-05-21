//
// Created by LiDon on 2025/9/15.
//

#include "ResourceManager.h"

#include "tjsObject.h"
#include "psbfile/PSBFile.h"

#define LOGGER spdlog::get("plugin")

motion::ResourceManager::ResourceManager(iTJSDispatch2 *kag,
                                         tjs_int cacheSize) {
    LOGGER->info("kag: {}, cacheSize: {}", static_cast<void *>(kag), cacheSize);
    if(cacheSize > 0) {
        std::lock_guard<std::mutex> lock(_mutex);
        _cacheLimit = static_cast<size_t>(cacheSize);
        trimCacheLocked();
    }
}

tjs_error motion::ResourceManager::setEmotePSBDecryptSeed(tTJSVariant *,
                                                          tjs_int count,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *) {
    if(count != 1 || !p || !p[0] || (*p)->Type() != tvtInteger) {
        return TJS_E_BADPARAMCOUNT;
    }
    _decryptSeed = static_cast<tjs_int>(*p[0]);
    LOGGER->info("setEmotePSBDecryptSeed: {}", _decryptSeed);
    return TJS_S_OK;
}

tjs_error motion::ResourceManager::setEmotePSBDecryptFunc(tTJSVariant *r,
                                                          tjs_int n,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *obj) {
    LOGGER->critical("setEmotePSBDecryptFunc no implement!");
    return TJS_S_OK;
}

tTJSVariant motion::ResourceManager::load(ttstr path) const {
    auto canonical = path.AsLowerCase().AsStdString();
    auto file = std::make_shared<PSB::PSBFile>();
    file->setSeed(_decryptSeed);
    if(!file->loadPSBFile(path)) {
        LOGGER->error("emote load file: {} failed", path.AsStdString());
        iTJSDispatch2 *empty = TJSCreateCustomObject();
        tTJSVariant result{ empty, empty };
        empty->Release();
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _lastLoadedPath = canonical;
        _loadedFiles[canonical] = file;
        _cacheOrder.erase(std::remove(_cacheOrder.begin(), _cacheOrder.end(), canonical),
                          _cacheOrder.end());
        _cacheOrder.push_back(canonical);
        trimCacheLocked();
    }

    iTJSDispatch2 *dic = TJSCreateCustomObject();
    auto objs = file->getObjects();
    if(objs != nullptr) {
        for(const auto &[k, v] : *objs) {
            tTJSVariant tmp = v->toTJSVal();
            dic->PropSet(TJS_MEMBERENSURE, ttstr{ k }.c_str(), nullptr, &tmp,
                         dic);
        }
    }
    tTJSVariant result{ dic, dic };
    dic->Release();
    return result;
}

void motion::ResourceManager::unload(const ttstr &path) const {
    const auto canonical = path.AsLowerCase().AsStdString();
    std::lock_guard<std::mutex> lock(_mutex);
    _loadedFiles.erase(canonical);
    _cacheOrder.erase(std::remove(_cacheOrder.begin(), _cacheOrder.end(), canonical),
                      _cacheOrder.end());
    if(_lastLoadedPath == canonical) {
        _lastLoadedPath.clear();
    }
}

void motion::ResourceManager::clearCache() const {
    std::lock_guard<std::mutex> lock(_mutex);
    _loadedFiles.clear();
    _cacheOrder.clear();
    _lastLoadedPath.clear();
}

ttstr motion::ResourceManager::getLastLoadedPath() {
    std::lock_guard<std::mutex> lock(_mutex);
    return ttstr(_lastLoadedPath.c_str());
}

bool motion::ResourceManager::hasLoadedPath(const ttstr &path) {
    std::lock_guard<std::mutex> lock(_mutex);
    return _loadedFiles.find(path.AsLowerCase().AsStdString()) != _loadedFiles.end();
}

int motion::ResourceManager::getDecryptSeed() {
    return _decryptSeed;
}

std::shared_ptr<PSB::PSBFile>
motion::ResourceManager::getLoadedFile(const ttstr &path) {
    const auto canonical = path.AsLowerCase().AsStdString();
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _loadedFiles.find(canonical);
    if(it == _loadedFiles.end()) {
        return nullptr;
    }
    return it->second;
}

void motion::ResourceManager::trimCacheLocked() {
    while(_cacheLimit > 0 && _loadedFiles.size() > _cacheLimit &&
          !_cacheOrder.empty()) {
        const auto victim = _cacheOrder.front();
        _cacheOrder.pop_front();
        auto it = _loadedFiles.find(victim);
        if(it == _loadedFiles.end()) {
            continue;
        }
        if(_lastLoadedPath == victim) {
            _lastLoadedPath.clear();
        }
        _loadedFiles.erase(it);
    }
}
