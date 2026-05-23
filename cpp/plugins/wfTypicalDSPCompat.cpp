#define NCB_MODULE_NAME TJS_W("wfTypicalDSP.dll")

#include "ncbind.hpp"
#include "tjs.h"
#include "waveFilterCompat.hpp"
#include <vector>

namespace {

class WaveDSPFilter : public PassThroughWaveFilterBase {
public:
    static tjs_error factory(WaveDSPFilter **result, tjs_int count,
                             tTJSVariant **params, iTJSDispatch2 *) {
        *result = new WaveDSPFilter();
        if(count > 0)
            (*result)->ApplyParams(count, params);
        return TJS_S_OK;
    }

    tjs_int64 getinterface() const { return GetInterfaceValue(); }

    ttstr getType() const { return type_; }
    void setType(const ttstr &value) { type_ = value; }

    ttstr getDesign() const { return design_; }
    void setDesign(const ttstr &value) { design_ = value; }

    tjs_int getOrder() const { return order_; }
    void setOrder(tjs_int value) { order_ = value; }

    tjs_real getFrequency() const { return frequency_; }
    void setFrequency(tjs_real value) { frequency_ = value; }

    tjs_real getfreq() const { return frequency_; }
    void setfreq(tjs_real value) { frequency_ = value; }

    tjs_real getGain() const { return gain_; }
    void setGain(tjs_real value) { gain_ = value; }

    tjs_real getQ() const { return q_; }
    void setQ(tjs_real value) { q_ = value; }

    tjs_real getBandwidth() const { return bandwidth_; }
    void setBandwidth(tjs_real value) { bandwidth_ = value; }

    tjs_real getRipple() const { return ripple_; }
    void setRipple(tjs_real value) { ripple_ = value; }

    tjs_int getChannels() const { return channels_; }
    void setChannels(tjs_int value) { channels_ = value; }

    tjs_int getSampleRate() const { return sampleRate_; }
    void setSampleRate(tjs_int value) { sampleRate_ = value; }

    tjs_real getMix() const { return mix_; }
    void setMix(tjs_real value) { mix_ = value; }

    void reset() { Reset(); }
    void clear() { Clear(); params_.clear(); }

    static tjs_error setParamsCallback(tTJSVariant *result, tjs_int count,
                                       tTJSVariant **params,
                                       iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<WaveDSPFilter>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        self->ApplyParams(count, params);
        if(result)
            *result = tTJSVariant(objthis, objthis);
        return TJS_S_OK;
    }

    static tjs_error getInterfaceProperty(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<WaveDSPFilter>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(result)
            *result = static_cast<tjs_int64>(self->getinterface());
        return TJS_S_OK;
    }

    static tjs_error getTypeProperty(tTJSVariant *result, tjs_int,
                                     tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<WaveDSPFilter>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(result)
            *result = self->getType();
        return TJS_S_OK;
    }

    static tjs_error setTypeProperty(tTJSVariant *, tjs_int count,
                                     tTJSVariant **params,
                                     iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<WaveDSPFilter>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count < 1)
            return TJS_E_BADPARAMCOUNT;
        self->setType(VariantToStringDefault(params[0], self->type_));
        return TJS_S_OK;
    }

    static tjs_error getDesignProperty(tTJSVariant *result, tjs_int,
                                       tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<WaveDSPFilter>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(result)
            *result = self->getDesign();
        return TJS_S_OK;
    }

    static tjs_error setDesignProperty(tTJSVariant *, tjs_int count,
                                       tTJSVariant **params,
                                       iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<WaveDSPFilter>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count < 1)
            return TJS_E_BADPARAMCOUNT;
        self->setDesign(VariantToStringDefault(params[0], self->design_));
        return TJS_S_OK;
    }

private:
    void ApplyParams(tjs_int count, tTJSVariant **params) {
        CopyVariantParams(params_, count, params);
        if(count <= 0)
            return;

        ttstr first = VariantToStringDefault(params[0]);
        const tjs_char *underscore = TJS_strchr(first.c_str(), TJS_W('_'));
        if(underscore) {
            tjs_int pos = static_cast<tjs_int>(underscore - first.c_str());
            type_ = first.SubString(0, pos);
            design_ = first.SubString(pos + 1, first.GetLen() - pos - 1);
        } else {
            type_ = first;
            if(count >= 2)
                design_ = VariantToStringDefault(params[1], design_);
        }

        tjs_int numericBase = (count >= 2 && !underscore) ? 2 : 1;
        if(count > numericBase)
            order_ = VariantToIntDefault(params[numericBase], order_);
        if(count > numericBase + 1)
            frequency_ = VariantToRealDefault(params[numericBase + 1], frequency_);
        if(count > numericBase + 2)
            q_ = VariantToRealDefault(params[numericBase + 2], q_);
        if(count > numericBase + 3)
            gain_ = VariantToRealDefault(params[numericBase + 3], gain_);
        if(count > numericBase + 4)
            ripple_ = VariantToRealDefault(params[numericBase + 4], ripple_);
        if(count > numericBase + 5)
            bandwidth_ = VariantToRealDefault(params[numericBase + 5], bandwidth_);
        if(count > numericBase + 6)
            sampleRate_ = VariantToIntDefault(params[numericBase + 6], sampleRate_);
        if(count > numericBase + 7)
            channels_ = VariantToIntDefault(params[numericBase + 7], channels_);
    }

    ttstr type_ = TJS_W("LowPass");
    ttstr design_ = TJS_W("RBJ");
    tjs_int order_ = 2;
    tjs_real frequency_ = 1000.0;
    tjs_real gain_ = 0.0;
    tjs_real q_ = 0.70710678;
    tjs_real bandwidth_ = 1.0;
    tjs_real ripple_ = 0.0;
    tjs_real mix_ = 1.0;
    tjs_int channels_ = 2;
    tjs_int sampleRate_ = 44100;
    std::vector<tTJSVariant> params_;
};

} // namespace

NCB_REGISTER_CLASS(WaveDSPFilter) {
    Factory(&WaveDSPFilter::factory);
    RawCallback(TJS_W("interface"), &Class::getInterfaceProperty, 0, 0);
    RawCallback(TJS_W("type"), &Class::getTypeProperty, &Class::setTypeProperty, 0);
    RawCallback(TJS_W("design"), &Class::getDesignProperty,
                &Class::setDesignProperty, 0);
    Property(TJS_W("order"), &Class::getOrder, &Class::setOrder);
    Property(TJS_W("frequency"), &Class::getFrequency, &Class::setFrequency);
    Property(TJS_W("freq"), &Class::getfreq, &Class::setfreq);
    Property(TJS_W("gain"), &Class::getGain, &Class::setGain);
    Property(TJS_W("Q"), &Class::getQ, &Class::setQ);
    Property(TJS_W("bandwidth"), &Class::getBandwidth, &Class::setBandwidth);
    Property(TJS_W("ripple"), &Class::getRipple, &Class::setRipple);
    Property(TJS_W("channels"), &Class::getChannels, &Class::setChannels);
    Property(TJS_W("sampleRate"), &Class::getSampleRate, &Class::setSampleRate);
    Property(TJS_W("mix"), &Class::getMix, &Class::setMix);
    RawCallback(TJS_W("setParams"), &Class::setParamsCallback, 0);
    Method(TJS_W("reset"), &Class::reset);
    Method(TJS_W("clear"), &Class::clear);
}
