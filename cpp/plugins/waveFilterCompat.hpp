#pragma once

#include "WaveIntf.h"
#include <cstring>

class PassThroughWaveFilterBase : public iTVPBasicWaveFilter,
                                  public tTVPSampleAndLabelSource {
public:
    tTVPSampleAndLabelSource *
    Recreate(tTVPSampleAndLabelSource *source) override {
        source_ = source;
        if(source_) {
            format_ = source_->GetFormat();
        } else {
            std::memset(&format_, 0, sizeof(format_));
        }
        return this;
    }

    void Clear() override {
        source_ = nullptr;
        std::memset(&format_, 0, sizeof(format_));
    }

    void Update() override {}

    void Reset() override {}

    void Decode(void *dest, tjs_uint samples, tjs_uint &written,
                tTVPWaveSegmentQueue &segments) override {
        if(!source_) {
            written = 0;
            segments.Clear();
            return;
        }
        source_->Decode(dest, samples, written, segments);
    }

    [[nodiscard]] const tTVPWaveFormat &GetFormat() const override {
        return format_;
    }

protected:
    [[nodiscard]] tjs_int64 GetInterfaceValue() const {
        return reinterpret_cast<tjs_int64>(
            const_cast<PassThroughWaveFilterBase *>(this));
    }

private:
    tTVPSampleAndLabelSource *source_ = nullptr;
    tTVPWaveFormat format_ {};
};

inline void CopyVariantParams(std::vector<tTJSVariant> &dest, tjs_int count,
                              tTJSVariant **params) {
    dest.clear();
    dest.reserve(count > 0 ? static_cast<size_t>(count) : 0U);
    for(tjs_int i = 0; i < count; ++i)
        dest.emplace_back(*params[i]);
}

inline tjs_int VariantToIntDefault(const tTJSVariant *value, tjs_int fallback) {
    if(!value || value->Type() == tvtVoid)
        return fallback;
    return static_cast<tjs_int>(*value);
}

inline tjs_real VariantToRealDefault(const tTJSVariant *value,
                                     tjs_real fallback) {
    if(!value || value->Type() == tvtVoid)
        return fallback;
    return static_cast<tjs_real>(*value);
}

inline ttstr VariantToStringDefault(const tTJSVariant *value,
                                    const ttstr &fallback = ttstr()) {
    if(!value || value->Type() == tvtVoid)
        return fallback;
    return ttstr(*value);
}
