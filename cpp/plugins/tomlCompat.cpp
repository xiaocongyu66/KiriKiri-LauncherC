#include "BinaryStream.h"
#include "StorageIntf.h"
#include "tjs.h"
#include "ncbind.hpp"

#include <spdlog/spdlog.h>

#define NCB_MODULE_NAME TJS_W("toml.dll")

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

static tjs_error TJS_INTF_METHOD tomlDecodeFromStorage(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
    iTJSDispatch2 *objthis) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    if(!result)
        return TJS_S_OK;

    ttstr name = *param[0];
    ttstr mode;
    if(numparams >= 2 && param[1]->Type() != tvtVoid)
        mode = *param[1];

    tTJSBinaryStream *stream = nullptr;
    try {
        stream = TVPCreateBinaryStreamForRead(name, mode);
        if(!stream)
            return TJS_E_INVALIDPARAM;

        tTJSVariant value;
        stream->SetPosition(0);
        if(tTJS::LoadBinaryDictionayArray(stream, &value)) {
            *result = value;
            delete stream;
            return TJS_S_OK;
        }
    } catch(...) {
        if(stream)
            delete stream;
        throw;
    }

    delete stream;

    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    *result = tTJSVariant(dict, dict);
    dict->Release();
    spdlog::warn("[krkr] toml.dll fallback empty dict for {}",
                 name.AsStdString());
    return TJS_S_OK;
}

NCB_REGISTER_FUNCTION(tomlDecodeFromStorage, tomlDecodeFromStorage);
