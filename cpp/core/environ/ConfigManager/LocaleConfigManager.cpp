#include "LocaleConfigManager.h"
#include "ConfigFileIO.h"
#include "GlobalConfigManager.h"
#include <tinyxml2.h>

LocaleConfigManager::LocaleConfigManager() = default;

std::string LocaleConfigManager::GetLogicalFilePath() const {
    std::string pathprefix = "locale/"; // constant file in app package
    return pathprefix + currentLangCode + ".xml"; // exp. "locale/en_us.xml"
}

LocaleConfigManager *LocaleConfigManager::GetInstance() {
    static LocaleConfigManager instance;
    return &instance;
}

const std::string &LocaleConfigManager::GetText(const std::string &tid) {
    auto it = AllConfig.find(tid);
    if(it == AllConfig.end()) {
        AllConfig[tid] = tid;
        return AllConfig[tid];
    }
    return it->second;
}

void LocaleConfigManager::Initialize(const std::string &sysLang) {
    // override by global configured lang
    currentLangCode = GlobalConfigManager::GetInstance()->GetValue<std::string>(
        "user_language", "");
    if(currentLangCode.empty())
        currentLangCode = sysLang;
    AllConfig.clear();
    tinyxml2::XMLDocument doc;
    std::string xmlData;
    if(!TVPLoadBundledConfigText(GetLogicalFilePath(), &xmlData)) {
        currentLangCode = "en_us"; // restore to default language config
        TVPLoadBundledConfigText(GetLogicalFilePath(), &xmlData);
    }
    bool _writeBOM = false;
    const char *p = xmlData.c_str();
    p = tinyxml2::XMLUtil::ReadBOM(p, &_writeBOM);
    doc.Parse(p);
    tinyxml2::XMLElement *rootElement = doc.RootElement();
    if(rootElement) {
        for(tinyxml2::XMLElement *item = rootElement->FirstChildElement(); item;
            item = item->NextSiblingElement()) {
            const char *key = item->Attribute("id");
            const char *val = item->Attribute("text");
            if(key && val) {
                AllConfig[key] = val;
            }
        }
    }
}
