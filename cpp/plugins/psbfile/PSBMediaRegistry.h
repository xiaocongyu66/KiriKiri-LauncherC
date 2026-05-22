#pragma once
//
// PSBMediaRegistry: ported from kirikiroid2-web. The web build keeps the
// PSBMedia singleton inside this module; the Android build creates and
// registers PSBMedia in psbfile/main.cpp::initPsbFile(). Here we keep only
// the resource-tree walker and route adds through PSB::GetGlobalPSBMedia()
// so we never double-register the storage media.
//
#include <memory>
#include <vector>
#include "tjs.h"
#include "PSBFile.h"

namespace PSB {
    // No-op on Android: PSBMedia is initialised by initPsbFile() in main.cpp.
    // Kept for source-level compatibility with the web port.
    void initPSBMedia();
    void deInitPSBMedia();

    void registerRootResources(const ttstr &container,
                               const std::shared_ptr<const PSBDictionary> &root);
    void registerRootResources(const std::vector<ttstr> &containers,
                               const std::shared_ptr<const PSBDictionary> &root);
    void registerRootResources(const ttstr &container, const PSBFile &file);
    void registerRootResources(const std::vector<ttstr> &containers,
                               const PSBFile &file);
} // namespace PSB
