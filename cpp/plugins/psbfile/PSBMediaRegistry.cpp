//
// PSBMediaRegistry: routes web-port resource registration to the
// pre-existing global PSBMedia singleton owned by initPsbFile() in
// psbfile/main.cpp. We deliberately do NOT call TVPRegisterStorageMedia
// here, otherwise the storage media list would carry two PSBMedia
// instances and lookups become ambiguous.
//
#include "PSBMediaRegistry.h"

#include <spdlog/spdlog.h>

#include "PSBMedia.h"
#include "PSBValue.h"
#include "StorageIntf.h"

namespace PSB {
#define LOGGER spdlog::get("plugin")

    namespace {
        void registerValueResources(const ttstr &normalizedContainer,
                                    const std::shared_ptr<IPSBValue> &value,
                                    std::vector<std::string> &path) {
            PSBMedia *psbMedia = GetGlobalPSBMedia();
            if(psbMedia == nullptr || value == nullptr) {
                return;
            }
            if(const auto resource = std::dynamic_pointer_cast<PSBResource>(value)) {
                ttstr resourceKey;
                for(size_t index = 0; index < path.size(); ++index) {
                    if(index != 0) {
                        resourceKey += TJS_W("/");
                    }
                    resourceKey += ttstr{ path[index] };
                }
                if(resourceKey.IsEmpty()) {
                    return;
                }
                psbMedia->NormalizePathName(resourceKey);
                psbMedia->add((normalizedContainer + TJS_W("/") + resourceKey)
                                  .AsStdString(),
                              resource);
                return;
            }
            if(const auto dic = std::dynamic_pointer_cast<PSBDictionary>(value)) {
                for(const auto &[key, child] : *dic) {
                    path.push_back(key);
                    registerValueResources(normalizedContainer, child, path);
                    path.pop_back();
                }
                return;
            }
            if(const auto list = std::dynamic_pointer_cast<PSBList>(value)) {
                for(size_t index = 0; index < list->size(); ++index) {
                    path.push_back(std::to_string(index));
                    registerValueResources(normalizedContainer,
                                           (*list)[static_cast<int>(index)],
                                           path);
                    path.pop_back();
                }
            }
        }

        void registerRootResourcesForContainer(
            const ttstr &container,
            const std::shared_ptr<const PSBDictionary> &root) {
            PSBMedia *psbMedia = GetGlobalPSBMedia();
            if(psbMedia == nullptr || root == nullptr || container.IsEmpty()) {
                return;
            }
            ttstr normalizedContainer = container;
            psbMedia->NormalizeDomainName(normalizedContainer);
            std::vector<std::string> path;
            registerValueResources(
                normalizedContainer,
                std::const_pointer_cast<PSBDictionary>(root), path);
        }
    } // namespace

    // PSBMedia lifecycle is owned by initPsbFile()/deInitPsbFile() in
    // psbfile/main.cpp on Android; these stay as no-ops for the web-port
    // call sites that try to lazy-init from registerRootResources.
    void initPSBMedia() {}
    void deInitPSBMedia() {}

    void registerRootResources(const ttstr &container,
                               const std::shared_ptr<const PSBDictionary> &root) {
        registerRootResourcesForContainer(container, root);
    }

    void registerRootResources(const std::vector<ttstr> &containers,
                               const std::shared_ptr<const PSBDictionary> &root) {
        for(const auto &container : containers) {
            registerRootResourcesForContainer(container, root);
        }
    }

    void registerRootResources(const ttstr &container, const PSBFile &file) {
        registerRootResources(container, file.getObjects());
    }

    void registerRootResources(const std::vector<ttstr> &containers,
                               const PSBFile &file) {
        registerRootResources(containers, file.getObjects());
    }
} // namespace PSB