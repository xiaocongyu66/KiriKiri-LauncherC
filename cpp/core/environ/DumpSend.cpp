#include <minizip/unzip/ioapi.h>
#include <minizip/zip.h>
#include <sstream>
#include <iomanip>
#include <condition_variable>
#include <map>
#include <thread>
#include "Platform.h"
#include "ConfigManager/LocaleConfigManager.h"
#include "StorageImpl.h"

// Cocos dump upload path removed; only local dump clear remains.

static void ClearDumps(const std::string &dumpdir,
                       std::vector<std::string> &allDumps) {
    for(const std::string &path : allDumps) {
        remove((dumpdir + "/" + path).c_str());
    }
    // allDumps.clear();
}

static void SendDumps(std::string dumpdir, std::vector<std::string> allDumps,
                      std::string packageName, std::string versionStr) {
    (void)packageName;
    (void)versionStr;
    ClearDumps(dumpdir, allDumps);
}


void TVPCheckAndSendDumps(const std::string &dumpdir,
                          const std::string &packageName,
                          const std::string &versionStr) {
    std::vector<std::string> allDumps;
    TVPListDir(dumpdir, [&](const std::string &name, int mask) {
        if(mask & (S_IFREG | S_IFDIR)) {
            if(name.size() <= 4)
                return;
            if(name.substr(name.size() - 4) != ".dmp")
                return;
            allDumps.emplace_back(name);
        }
    });
    if(!allDumps.empty()) {
        std::string title =
            LocaleConfigManager::GetInstance()->GetText("crash_report");
        std::string msgfmt =
            LocaleConfigManager::GetInstance()->GetText("crash_report_msg");
        char buf[256];
        sprintf(buf, msgfmt.c_str(), allDumps.size());
        if(TVPShowSimpleMessageBoxYesNo(buf, title) == 0) {
            static std::thread dumpthread;
            dumpthread =
                std::thread([dumpdir, allDumps, packageName, versionStr] {
                    SendDumps(dumpdir, allDumps, packageName, versionStr);
                });
        } else {
            ClearDumps(dumpdir, allDumps);
        }
    }
}
