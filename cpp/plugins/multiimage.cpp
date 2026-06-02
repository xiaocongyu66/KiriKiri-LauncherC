#include "ncbind.hpp"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#define NCB_MODULE_NAME TJS_W("multiimage.dll")

struct MultiImageEntry {
    tTJSVariant img;
    int x;
    int y;
    double zoomX;
    double zoomY;
    int time;
};

namespace {

using TjsString = std::basic_string<tjs_char>;

static bool IsSpace(tjs_char ch) {
    return ch == TJS_W(' ') || ch == TJS_W('\t') || ch == TJS_W('\r') ||
           ch == TJS_W('\n');
}

static TjsString Trim(const TjsString &src) {
    size_t first = 0;
    while(first < src.size() && IsSpace(src[first]))
        ++first;

    size_t last = src.size();
    while(last > first && IsSpace(src[last - 1]))
        --last;

    return src.substr(first, last - first);
}

static std::vector<TjsString> SplitImageMultiLine(const TjsString &line) {
    std::vector<TjsString> tokens;
    TjsString current;
    bool sawTab = false;

    for(tjs_char ch : line) {
        if(ch == TJS_W('\t')) {
            sawTab = true;
            tokens.push_back(Trim(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    tokens.push_back(Trim(current));

    if(sawTab)
        return tokens;

    tokens.clear();
    current.clear();
    for(tjs_char ch : line) {
        if(IsSpace(ch)) {
            if(!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if(!current.empty())
        tokens.push_back(current);
    return tokens;
}

static TjsString ToLowerString(const TjsString &src) {
    ttstr value(src.c_str(), static_cast<tjs_int>(src.size()));
    value.ToLowerCase();
    return TjsString(value.c_str(), value.length());
}

static size_t FindScaleSeparator(const TjsString &token) {
    const size_t halfWidth = token.find(TJS_W(':'));
    const size_t fullWidth = token.find(static_cast<tjs_char>(0xff1a));
    if(halfWidth == TjsString::npos)
        return fullWidth;
    if(fullWidth == TjsString::npos)
        return halfWidth;
    return std::min(halfWidth, fullWidth);
}

static TjsString NormalizeScaleSeparators(const TjsString &src) {
    TjsString normalized = src;
    for(tjs_char &ch : normalized) {
        if(ch == static_cast<tjs_char>(0xff1a))
            ch = TJS_W(':');
    }
    return normalized;
}

static bool ParseScalePrefix(const TjsString &token, tjs_real &scale,
                             TjsString &file) {
    const size_t colon = FindScaleSeparator(token);
    if(colon == TjsString::npos || colon == 0)
        return false;

    bool numeric = true;
    bool hasDigit = false;
    bool hasDot = false;
    for(size_t i = 0; i < colon; ++i) {
        const tjs_char ch = token[i];
        if(ch >= TJS_W('0') && ch <= TJS_W('9')) {
            hasDigit = true;
            continue;
        }
        if(ch == TJS_W('.') && !hasDot) {
            hasDot = true;
            continue;
        }
        numeric = false;
        break;
    }

    if(!numeric || !hasDigit)
        return false;

    tjs_real value = 0;
    tjs_real place = 0.1;
    bool fraction = false;
    for(size_t i = 0; i < colon; ++i) {
        const tjs_char ch = token[i];
        if(ch == TJS_W('.')) {
            fraction = true;
            continue;
        }
        const int digit = static_cast<int>(ch - TJS_W('0'));
        if(fraction) {
            value += digit * place;
            place *= 0.1;
        } else {
            value = value * 10 + digit;
        }
    }

    TjsString rest = Trim(token.substr(colon + 1));
    if(rest.empty())
        return false;

    scale = value / 100.0;
    file = rest;
    return true;
}

static tTJSVariant MakeImageMultiEntry(const TjsString &token, bool first) {
    tjs_real scale = first ? 1.0 : 0.0;
    bool hasScale = first;
    TjsString file = ToLowerString(Trim(token));
    if(ParseScalePrefix(file, scale, file))
        hasScale = true;

    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    tTJSVariant fileVar(ttstr(file.c_str(), file.size()));
    tTJSVariant scaleVar;
    if(hasScale)
        scaleVar = scale;
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("file"), nullptr, &fileVar, dict);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("scale"), nullptr, &scaleVar, dict);

    tTJSVariant ret(dict, dict);
    dict->Release();
    return ret;
}

static bool GetObjectCount(iTJSDispatch2 *object, tjs_int &count) {
    if(!object)
        return false;

    tTJSVariant countVar;
    if(TJS_FAILED(object->PropGet(TJS_IGNOREPROP, TJS_W("count"), nullptr,
                                  &countVar, object))) {
        return false;
    }

    count = static_cast<tjs_int>(countVar);
    return count >= 0;
}

static void AppendTextRows(const TjsString &content,
                           std::vector<std::vector<TjsString>> &rows) {
    size_t lineStart = 0;
    while(lineStart <= content.size()) {
        size_t lineEnd = lineStart;
        while(lineEnd < content.size() && content[lineEnd] != TJS_W('\r') &&
              content[lineEnd] != TJS_W('\n')) {
            ++lineEnd;
        }

        TjsString line = Trim(content.substr(lineStart, lineEnd - lineStart));
        if(!line.empty() && line[0] == 0xfeff)
            line = Trim(line.substr(1));

        if(!line.empty() && line[0] != TJS_W('#')) {
            std::vector<TjsString> tokens = SplitImageMultiLine(line);
            tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                                        [](const TjsString &v) {
                                            return Trim(v).empty();
                                        }),
                         tokens.end());
            if(tokens.size() >= 2)
                rows.push_back(std::move(tokens));
        }

        if(lineEnd >= content.size())
            break;
        lineStart = lineEnd + 1;
        if(lineEnd + 1 < content.size() && content[lineEnd] == TJS_W('\r') &&
           content[lineEnd + 1] == TJS_W('\n')) {
            lineStart = lineEnd + 2;
        }
    }
}

static bool AppendVariantRows(tTJSVariant &input,
                              std::vector<std::vector<TjsString>> &rows) {
    if(input.Type() != tvtObject)
        return false;

    iTJSDispatch2 *object = input.AsObjectNoAddRef();
    tjs_int rowCount = 0;
    if(!GetObjectCount(object, rowCount))
        return false;

    for(tjs_int i = 0; i < rowCount; ++i) {
        tTJSVariant rowVar;
        if(TJS_FAILED(object->PropGetByNum(TJS_IGNOREPROP, i, &rowVar,
                                           object))) {
            continue;
        }

        if(rowVar.Type() == tvtObject) {
            iTJSDispatch2 *rowObject = rowVar.AsObjectNoAddRef();
            tjs_int columnCount = 0;
            if(GetObjectCount(rowObject, columnCount)) {
                std::vector<TjsString> row;
                for(tjs_int j = 0; j < columnCount; ++j) {
                    tTJSVariant cellVar;
                    if(TJS_FAILED(rowObject->PropGetByNum(TJS_IGNOREPROP, j,
                                                          &cellVar,
                                                          rowObject))) {
                        continue;
                    }
                    ttstr cell(cellVar);
                    TjsString token = Trim(TjsString(cell.c_str(),
                                                     cell.length()));
                    if(!token.empty())
                        row.push_back(std::move(token));
                }
                if(row.size() >= 2)
                    rows.push_back(std::move(row));
                continue;
            }
        }

        ttstr rowText(rowVar);
        AppendTextRows(TjsString(rowText.c_str(), rowText.length()), rows);
    }

    return true;
}

static tjs_error ImagemultiLoadInfo(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param,
                                    iTJSDispatch2 *objthis) {
    if(!result)
        return TJS_S_OK;

    iTJSDispatch2 *groups = TJSCreateArrayObject();
    try {
        if(numparams < 1 || !param || !param[0] || param[0]->Type() == tvtVoid) {
            *result = tTJSVariant(groups, groups);
            groups->Release();
            return TJS_S_OK;
        }

        std::vector<std::vector<TjsString>> rows;
        if(!AppendVariantRows(*param[0], rows)) {
            ttstr text(*param[0]);
            AppendTextRows(TjsString(text.c_str(), text.length()), rows);
        }

        tjs_int groupIndex = 0;

        for(const auto &tokens : rows) {
            iTJSDispatch2 *rawGroup = TJSCreateArrayObject();
            iTJSDispatch2 *parsedGroup = TJSCreateArrayObject();
            try {
                tjs_int rawIndex = 0;
                tjs_int parsedIndex = 0;
                for(size_t i = 0; i < tokens.size(); ++i) {
                    TjsString token = NormalizeScaleSeparators(Trim(tokens[i]));
                    tTJSVariant rawValue(ttstr(token.c_str(), token.size()));
                    rawGroup->PropSetByNum(TJS_MEMBERENSURE, rawIndex++,
                                           &rawValue, rawGroup);

                    tTJSVariant item = MakeImageMultiEntry(token, i == 0);
                    parsedGroup->PropSetByNum(TJS_MEMBERENSURE, parsedIndex++,
                                              &item, parsedGroup);
                }

                tTJSVariant rawGroupVar(rawGroup, rawGroup);
                groups->PropSetByNum(TJS_MEMBERENSURE, groupIndex++,
                                     &rawGroupVar, groups);

                TjsString key = ToLowerString(NormalizeScaleSeparators(
                    Trim(tokens.front())));
                tjs_real ignoredScale = 1.0;
                TjsString keyFile = key;
                ParseScalePrefix(key, ignoredScale, keyFile);
                if(!keyFile.empty()) {
                    tTJSVariant parsedGroupVar(parsedGroup, parsedGroup);
                    ttstr keyName(keyFile.c_str(), keyFile.size());
                    groups->PropSet(TJS_MEMBERENSURE, keyName.c_str(), nullptr,
                                    &parsedGroupVar, groups);
                }

                rawGroup->Release();
                parsedGroup->Release();
            } catch(...) {
                rawGroup->Release();
                parsedGroup->Release();
                throw;
            }
        }

        *result = tTJSVariant(groups, groups);
        groups->Release();
        return TJS_S_OK;
    } catch(...) {
        groups->Release();
        throw;
    }
}

static void SetObjectMethod(iTJSDispatch2 *object, const tjs_char *name,
                            tTJSNativeClassMethodCallback callback) {
    if(!object || !name || !callback)
        return;

    tTJSVariant existing;
    if(TJS_SUCCEEDED(object->PropGet(0, name, nullptr, &existing, object)) &&
       existing.Type() == tvtObject) {
        iTJSDispatch2 *existingObject = existing.AsObjectNoAddRef();
        if(existingObject &&
           existingObject->IsInstanceOf(0, nullptr, nullptr, TJS_W("Function"),
                                        existingObject) == TJS_S_TRUE) {
            return;
        }
    }

    iTJSDispatch2 *method = TJSCreateNativeClassMethod(callback);
    if(!method)
        return;

    tTJSVariant value(method, method);
    object->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, object);
    method->Release();
}

static void RegisterImageMultiObject() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    iTJSDispatch2 *imagemulti = nullptr;
    tTJSVariant existing;
    if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("imagemulti"), nullptr,
                                     &existing, global)) &&
       existing.Type() == tvtObject) {
        imagemulti = existing.AsObject();
    }

    if(!imagemulti) {
        imagemulti = TJSCreateDictionaryObject();
        tTJSVariant value(imagemulti, imagemulti);
        global->PropSet(TJS_MEMBERENSURE, TJS_W("imagemulti"), nullptr,
                        &value, global);
    }

    SetObjectMethod(imagemulti, TJS_W("loadInfo"), ImagemultiLoadInfo);
    imagemulti->Release();
    global->Release();
}

} // namespace

// MultiImage Class Definition
class MultiImage {
    std::vector<MultiImageEntry> queue;
public:
    MultiImage() {}
    ~MultiImage() {}

    void clear() {
        queue.clear();
    }

    void addZoom(double zoomX, double zoomY, int time) {
        if (!queue.empty()) {
            queue.back().zoomX = zoomX;
            queue.back().zoomY = zoomY;
            queue.back().time = time;
        }
    }

    void push(tTJSVariant img, int x, int y) {
        MultiImageEntry entry;
        entry.img = img;
        entry.x = x;
        entry.y = y;
        entry.zoomX = 1.0;
        entry.zoomY = 1.0;
        entry.time = 0;
        queue.push_back(entry);
    }

    void divisionLayer(double div) { }

    tTJSVariant calcMultiAffine(double a, double b, double c, double d, double tx, double ty, int w, int h, int opa, int mode) {
        return tTJSVariant(); 
    }

    tTJSVariant count() {
        return tTJSVariant((tjs_int)queue.size());
    }

    const std::vector<MultiImageEntry>& getQueue() const {
        return queue;
    }
};

NCB_REGISTER_CLASS(MultiImage)
{
    NCB_CONSTRUCTOR(());
    NCB_METHOD(clear);
    NCB_METHOD(addZoom);
    NCB_METHOD(push);
    NCB_METHOD(divisionLayer);
    NCB_METHOD(calcMultiAffine);
    NCB_PROPERTY_RO(count, count);
}

static void ExtractImageInfo(const tTJSVariant& imgVar, tTJSVariant& src, tTJSVariant& sx, tTJSVariant& sy, tTJSVariant& sw, tTJSVariant& sh, tjs_real& img_l, tjs_real& img_t) {
    src = imgVar;
    sx = tTJSVariant(0);
    sy = tTJSVariant(0);
    sw = tTJSVariant(0);
    sh = tTJSVariant(0);
    img_l = 0; img_t = 0;

    if (imgVar.Type() == tvtObject) {
        iTJSDispatch2* dsp = imgVar.AsObjectNoAddRef();
        if (dsp) {
            tTJSVariant val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipLeft"), NULL, &val, dsp))) sx = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipTop"), NULL, &val, dsp))) sy = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipWidth"), NULL, &val, dsp))) sw = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipHeight"), NULL, &val, dsp))) sh = val;

            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("imageLeft"), NULL, &val, dsp))) img_l = (tjs_real)val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("imageTop"), NULL, &val, dsp))) img_t = (tjs_real)val;
        }
    }
}

// LayerExMulti Class Definition
class LayerExMulti {
public:
    static tjs_error multiAffineCopy(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {

        if(numparams < 10 || !objthis) return TJS_E_BADPARAMCOUNT;
        
        tTJSVariant multiImg = *param[0];
        double a = (double)*param[1];
        double b = (double)*param[2];
        double c = (double)*param[3];
        double d = (double)*param[4];
        double tx = (double)*param[5];
        double ty = (double)*param[6];
        int opa = (int)*param[7];
        int mode = (int)*param[8];
        int type = (int)*param[9];

        if (multiImg.Type() != tvtObject) return TJS_S_OK;
        iTJSDispatch2* miDsp = multiImg.AsObjectNoAddRef();
        if (!miDsp) return TJS_S_OK;

        MultiImage* mi = ncbInstanceAdaptor<MultiImage>::GetNativeInstance(miDsp);
        if (!mi) return TJS_S_OK;

        const auto& queue = mi->getQueue();
        for (const auto& entry : queue) {
            tTJSVariant src, sx, sy, sw, sh;
            tjs_real img_l = 0, img_t = 0;
            ExtractImageInfo(entry.img, src, sx, sy, sw, sh, img_l, img_t);

            double final_a = a * entry.zoomX;
            double final_b = b * entry.zoomX;
            double final_c = c * entry.zoomY;
            double final_d = d * entry.zoomY;
            tjs_real local_cx = entry.x + img_l;
            tjs_real local_cy = entry.y + img_t;
            double final_tx = a * local_cx + c * local_cy + tx;
            double final_ty = b * local_cx + d * local_cy + ty;

            tTJSVariant args[15];
            args[0] = src;
            args[1] = sx;
            args[2] = sy;
            args[3] = sw;
            args[4] = sh;
            args[5] = tTJSVariant((tjs_int)1); // affine mode
            args[6] = tTJSVariant(final_a);
            args[7] = tTJSVariant(final_b);
            args[8] = tTJSVariant(final_c);
            args[9] = tTJSVariant(final_d);
            args[10] = tTJSVariant(final_tx);
            args[11] = tTJSVariant(final_ty);
            args[12] = tTJSVariant(type);
            args[13] = tTJSVariant(opa);
            args[14] = tTJSVariant((tjs_int)0); // clear

            tTJSVariant* args_ptr[15] = {&args[0], &args[1], &args[2], &args[3], &args[4], &args[5], &args[6], &args[7], &args[8], &args[9], &args[10], &args[11], &args[12], &args[13], &args[14]};
            objthis->FuncCall(0, TJS_W("affineCopy"), NULL, NULL, 15, args_ptr, objthis);
        }

        return TJS_S_OK;
    }

    static tjs_error operateMultiAffine(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        
        if(numparams < 9 || !objthis) return TJS_E_BADPARAMCOUNT;
        
        tTJSVariant multiImg = *param[0];
        double a = (double)*param[1];
        double b = (double)*param[2];
        double c = (double)*param[3];
        double d = (double)*param[4];
        double tx = (double)*param[5];
        double ty = (double)*param[6];
        int opa = (int)*param[7];
        int mode = (int)*param[8];

        if (multiImg.Type() != tvtObject) return TJS_S_OK;
        iTJSDispatch2* miDsp = multiImg.AsObjectNoAddRef();
        if (!miDsp) return TJS_S_OK;

        MultiImage* mi = ncbInstanceAdaptor<MultiImage>::GetNativeInstance(miDsp);
        if (!mi) return TJS_S_OK;

        const auto& queue = mi->getQueue();
        bool clear = false;
        if (numparams >= 10 && param[9]->Type() != tvtVoid) clear = (int)*param[9] != 0;

        if (clear) {
            tTJSVariant val;
            tjs_uint32 color = 0;
            if (TJS_SUCCEEDED(objthis->PropGet(0, TJS_W("neutralColor"), NULL, &val, objthis))) color = (tjs_int)val;
            tTJSVariant args_clear[5];
            args_clear[0] = (tjs_int32)0; args_clear[1] = (tjs_int32)0; args_clear[2] = (tjs_int32)0; args_clear[3] = (tjs_int32)0; args_clear[4] = (tjs_int64)color;
            // IF WIDTH/HEIGHT ARE 0, THIS DOES NOTHING!
            tTJSVariant val_w, val_h;
            if (TJS_SUCCEEDED(objthis->PropGet(0, TJS_W("width"), NULL, &val_w, objthis))) args_clear[2] = val_w;
            if (TJS_SUCCEEDED(objthis->PropGet(0, TJS_W("height"), NULL, &val_h, objthis))) args_clear[3] = val_h;
            
            tTJSVariant* args_ptr[5] = {&args_clear[0], &args_clear[1], &args_clear[2], &args_clear[3], &args_clear[4]};
            objthis->FuncCall(0, TJS_W("fillRect"), NULL, NULL, 5, args_ptr, objthis);
        }

        for (const auto& entry : queue) {
            iTJSDispatch2* dsp = entry.img.AsObjectNoAddRef();
            if (!dsp) continue;

            tTJSVariant src = entry.img; // imgVar itself is the source layer!

            tTJSVariant val;
            tjs_real l = 0, t = 0, w = 0, h = 0;
            // The original multiimage plugin extracted clip bounds
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipLeft"), NULL, &val, dsp))) l = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipTop"), NULL, &val, dsp))) t = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipWidth"), NULL, &val, dsp))) w = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("clipHeight"), NULL, &val, dsp))) h = val;

            tjs_real img_l = 0, img_t = 0;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("imageLeft"), NULL, &val, dsp))) img_l = val;
            if (TJS_SUCCEEDED(dsp->PropGet(0, TJS_W("imageTop"), NULL, &val, dsp))) img_t = val;

            tjs_real local_cx = entry.x + img_l;
            tjs_real local_cy = entry.y + img_t;
            double final_tx = a * local_cx + c * local_cy + tx;
            double final_ty = b * local_cx + d * local_cy + ty;

            tTJSVariant sx(l), sy(t), sw(w), sh(h);
            tTJSVariant is_affine(1);
            tTJSVariant var_a(a), var_b(b), var_c(c), var_d(d), var_tx(final_tx), var_ty(final_ty);
            tTJSVariant param_mode(mode), param_opa(opa), param_type(0); // 0 = stNearest

            tTJSVariant* args_ptr[15] = {&src, &sx, &sy, &sw, &sh, &is_affine, &var_a, &var_b, &var_c, &var_d, &var_tx, &var_ty, &param_mode, &param_opa, &param_type};
            objthis->FuncCall(0, TJS_W("operateAffine"), NULL, NULL, 15, args_ptr, objthis);
        }

        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(LayerExMulti, Layer)
{
    RawCallback("multiAffineCopy", &LayerExMulti::multiAffineCopy, 0);
    RawCallback("operateMultiAffine", &LayerExMulti::operateMultiAffine, 0);
}

NCB_POST_REGIST_CALLBACK(RegisterImageMultiObject);
