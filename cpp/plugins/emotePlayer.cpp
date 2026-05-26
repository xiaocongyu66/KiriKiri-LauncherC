#include "ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("emoteplayer.dll")

static void InitPlugin_EMotePlayer() {
    ncbAutoRegister::LoadModule(TJS_W("motionplayer.dll"));
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_EMotePlayer);