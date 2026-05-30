// SPDX-License-Identifier: MIT
//
// KiriKiroid2 compatibility entry points used by mobile-port scripts.

#define NCB_MODULE_NAME TJS_W("kirikiroid2.dll")

#include "ncbind.hpp"
#include "TextStream.h"

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

static tjs_error TJS_INTF_METHOD krkr_str_ord(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis) {
    (void)objthis;
    if(numparams == 0)
        return TJS_E_FAIL;

    const tTJSVariant &arg = *param[0];
    if(arg.Type() == tvtString) {
        ttstr s = arg;
        const tjs_char *p = s.c_str();
        *result = (tjs_int)(p && *p ? (unsigned int)*p : 0);
    } else {
        *result = arg;
    }
    return TJS_S_OK;
}

NCB_REGISTER_FUNCTION(_str_ord, krkr_str_ord);

static tjs_error TJS_INTF_METHOD krkr_setTextEncoding(tTJSVariant *result,
                                                      tjs_int numparams,
                                                      tTJSVariant **param,
                                                      iTJSDispatch2 *objthis) {
    (void)result;
    (void)objthis;
    if(numparams == 0)
        return TJS_E_FAIL;

    tTJSVariant arg = *param[0];
    if(arg.Type() == tvtString) {
        TVPSetDefaultReadEncoding(arg);
    }
    return TJS_S_OK;
}

NCB_ATTACH_FUNCTION(setTextEncoding, Storages, krkr_setTextEncoding);
