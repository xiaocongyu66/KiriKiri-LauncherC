// multi language config mainly for ui
#pragma once
#include <unordered_map>
#include <string>
#include <vector>

#ifndef KRKR2_ENABLE_COCOS_HOST
#define KRKR2_ENABLE_COCOS_HOST 0
#endif

#if KRKR2_ENABLE_COCOS_HOST
namespace cocos2d::ui {
    class Text;
    class Button;
} // namespace cocos2d::ui
#endif


class LocaleConfigManager {

    std::unordered_map<std::string, std::string> AllConfig; // tid->text in utf8

    bool ConfigUpdated{};

    LocaleConfigManager();

    std::string GetLogicalFilePath() const;

public:
    static LocaleConfigManager *GetInstance();

    void Initialize(const std::string &sysLang);

    const std::string &GetText(const std::string &tid); // in utf8

#if KRKR2_ENABLE_COCOS_HOST
    bool initText(cocos2d::ui::Text *ctrl);
    bool initText(cocos2d::ui::Button *ctrl);
    bool initText(cocos2d::ui::Text *ctrl, const std::string &tid);
    bool initText(cocos2d::ui::Button *ctrl, const std::string &tid);
#endif

private:
    std::string currentLangCode;
};
