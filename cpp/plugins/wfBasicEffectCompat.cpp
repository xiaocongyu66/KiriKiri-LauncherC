#define NCB_MODULE_NAME TJS_W("wfBasicEffect.dll")

#include "ncbind.hpp"
#include "tjs.h"
#include "waveFilterCompat.hpp"
#include <algorithm>
#include <vector>

namespace {

class FreeVerbCompat : public PassThroughWaveFilterBase {
public:
    static tjs_error factory(FreeVerbCompat **result, tjs_int count,
                             tTJSVariant **params, iTJSDispatch2 *) {
        *result = new FreeVerbCompat();
        if(count > 0)
            (*result)->setmix(VariantToRealDefault(params[0], (*result)->mix_));
        return TJS_S_OK;
    }

    tjs_int64 getinterface() const { return GetInterfaceValue(); }
    tjs_real geteffectMix() const { return mix_; }
    void seteffectMix(tjs_real value) { mix_ = std::clamp(value, 0.0, 1.0); }
    tjs_real getmix() const { return mix_; }
    void setmix(tjs_real value) { seteffectMix(value); }
    tjs_real getroomSize() const { return roomSize_; }
    void setroomSize(tjs_real value) { roomSize_ = value; }
    tjs_real getdamp() const { return damp_; }
    void setdamp(tjs_real value) { damp_ = value; }
    tjs_real getwidth() const { return width_; }
    void setwidth(tjs_real value) { width_ = value; }
    tjs_real getwet() const { return wet_; }
    void setwet(tjs_real value) { wet_ = value; }
    tjs_real getdry() const { return dry_; }
    void setdry(tjs_real value) { dry_ = value; }
    void reset() { Reset(); }
    void clear() { Clear(); }

    static tjs_error setEffectMixCallback(tTJSVariant *result, tjs_int count,
                                          tTJSVariant **params,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<FreeVerbCompat>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count < 1)
            return TJS_E_BADPARAMCOUNT;
        self->seteffectMix(VariantToRealDefault(params[0], self->mix_));
        if(result)
            *result = self->geteffectMix();
        return TJS_S_OK;
    }

    static tjs_error getInterfaceProperty(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<FreeVerbCompat>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(result)
            *result = static_cast<tjs_int64>(self->getinterface());
        return TJS_S_OK;
    }

private:
    tjs_real mix_ = 0.25;
    tjs_real roomSize_ = 0.5;
    tjs_real damp_ = 0.5;
    tjs_real width_ = 1.0;
    tjs_real wet_ = 0.33;
    tjs_real dry_ = 0.67;
};

class GraphicEqualizerCompat : public PassThroughWaveFilterBase {
public:
    static tjs_error factory(GraphicEqualizerCompat **result, tjs_int count,
                             tTJSVariant **params, iTJSDispatch2 *) {
        *result = new GraphicEqualizerCompat();
        if(count > 0)
            (*result)->Resize(VariantToIntDefault(params[0], 10));
        return TJS_S_OK;
    }

    tjs_int64 getinterface() const { return GetInterfaceValue(); }
    tjs_real geteffectMix() const { return mix_; }
    void seteffectMix(tjs_real value) { mix_ = value; }
    tjs_real getmix() const { return mix_; }
    void setmix(tjs_real value) { mix_ = value; }
    tjs_int getbandCount() const { return static_cast<tjs_int>(bands_.size()); }
    void setbandCount(tjs_int value) { Resize(value); }
    void reset() {
        std::fill(bands_.begin(), bands_.end(), 0.0);
        Reset();
    }
    void clear() {
        bands_.clear();
        Clear();
    }

    static tjs_error setCallback(tTJSVariant *result, tjs_int count,
                                 tTJSVariant **params, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<GraphicEqualizerCompat>::GetNativeInstance(
            objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count < 2)
            return TJS_E_BADPARAMCOUNT;
        tjs_int index = VariantToIntDefault(params[0], 0);
        tjs_real gain = VariantToRealDefault(params[1], 0.0);
        self->SetBand(index, gain);
        if(result)
            *result = gain;
        return TJS_S_OK;
    }

    static tjs_error setParamsCallback(tTJSVariant *, tjs_int count,
                                       tTJSVariant **params,
                                       iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<GraphicEqualizerCompat>::GetNativeInstance(
            objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count >= 1)
            self->Resize(VariantToIntDefault(params[0], self->getbandCount()));
        for(tjs_int i = 1; i < count; ++i)
            self->SetBand(i - 1, VariantToRealDefault(params[i], 0.0));
        return TJS_S_OK;
    }

    static tjs_error getInterfaceProperty(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<GraphicEqualizerCompat>::GetNativeInstance(
            objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(result)
            *result = static_cast<tjs_int64>(self->getinterface());
        return TJS_S_OK;
    }

private:
    void Resize(tjs_int count) {
        count = std::max<tjs_int>(count, 1);
        bands_.assign(static_cast<size_t>(count), 0.0);
    }

    void SetBand(tjs_int index, tjs_real gain) {
        if(index < 0)
            return;
        if(index >= getbandCount())
            bands_.resize(static_cast<size_t>(index + 1), 0.0);
        bands_[static_cast<size_t>(index)] = gain;
    }

    tjs_real mix_ = 1.0;
    std::vector<tjs_real> bands_ = std::vector<tjs_real>(10, 0.0);
};

template <typename Derived>
class DelayCompatBase : public PassThroughWaveFilterBase {
public:
    static tjs_error factory(Derived **result, tjs_int count,
                             tTJSVariant **params, iTJSDispatch2 *) {
        *result = new Derived();
        if(count >= 1)
            (*result)->setdelay(VariantToRealDefault(params[0], (*result)->delay_));
        if(count >= 2)
            (*result)->setmaxDelay(
                VariantToRealDefault(params[1], (*result)->maxDelay_));
        return TJS_S_OK;
    }

    tjs_int64 getinterface() const { return GetInterfaceValue(); }
    tjs_real geteffectMix() const { return mix_; }
    void seteffectMix(tjs_real value) { mix_ = value; }
    tjs_real getmix() const { return mix_; }
    void setmix(tjs_real value) { mix_ = value; }
    tjs_real getdelay() const { return delay_; }
    void setdelay(tjs_real value) { delay_ = std::max<tjs_real>(value, 0.0); }
    tjs_real getmaxDelay() const { return maxDelay_; }
    void setmaxDelay(tjs_real value) {
        maxDelay_ = std::max<tjs_real>(value, 0.0);
        if(delay_ > maxDelay_)
            delay_ = maxDelay_;
    }
    tjs_real getfeedback() const { return feedback_; }
    void setfeedback(tjs_real value) { feedback_ = value; }
    void reset() { Reset(); }
    void clear() { Clear(); }

    static tjs_error setDelayCallback(tTJSVariant *result, tjs_int count,
                                      tTJSVariant **params,
                                      iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<Derived>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count < 1)
            return TJS_E_BADPARAMCOUNT;
        self->setdelay(VariantToRealDefault(params[0], self->delay_));
        if(count >= 2)
            self->setmaxDelay(VariantToRealDefault(params[1], self->maxDelay_));
        if(result)
            *result = self->getdelay();
        return TJS_S_OK;
    }

    static tjs_error setParamsCallback(tTJSVariant *, tjs_int count,
                                       tTJSVariant **params,
                                       iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<Derived>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count >= 1)
            self->setdelay(VariantToRealDefault(params[0], self->delay_));
        if(count >= 2)
            self->setmaxDelay(VariantToRealDefault(params[1], self->maxDelay_));
        if(count >= 3)
            self->setfeedback(VariantToRealDefault(params[2], self->feedback_));
        if(count >= 4)
            self->setmix(VariantToRealDefault(params[3], self->mix_));
        return TJS_S_OK;
    }

    static tjs_error getInterfaceProperty(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<Derived>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(result)
            *result = static_cast<tjs_int64>(self->getinterface());
        return TJS_S_OK;
    }

protected:
    tjs_real mix_ = 0.5;
    tjs_real delay_ = 0.0;
    tjs_real maxDelay_ = 1000.0;
    tjs_real feedback_ = 0.0;
};

class DelayEffect : public DelayCompatBase<DelayEffect> {};
class WaveDelay : public DelayCompatBase<WaveDelay> {};

class GainLimit : public PassThroughWaveFilterBase {
public:
    static tjs_error factory(GainLimit **result, tjs_int count,
                             tTJSVariant **params, iTJSDispatch2 *) {
        *result = new GainLimit();
        if(count >= 1)
            (*result)->setgain(VariantToRealDefault(params[0], (*result)->gain_));
        if(count >= 2)
            (*result)->setlimit(VariantToRealDefault(params[1], (*result)->limit_));
        return TJS_S_OK;
    }

    tjs_int64 getinterface() const { return GetInterfaceValue(); }
    tjs_real geteffectMix() const { return mix_; }
    void seteffectMix(tjs_real value) { mix_ = value; }
    tjs_real getmix() const { return mix_; }
    void setmix(tjs_real value) { mix_ = value; }
    tjs_real getgain() const { return gain_; }
    void setgain(tjs_real value) { gain_ = value; }
    tjs_real getlimit() const { return limit_; }
    void setlimit(tjs_real value) { limit_ = value; }
    tjs_real getclip() const { return clip_; }
    void setclip(tjs_real value) { clip_ = value; }
    tjs_real getthreshold() const { return threshold_; }
    void setthreshold(tjs_real value) { threshold_ = value; }
    tjs_real getattack() const { return attack_; }
    void setattack(tjs_real value) { attack_ = value; }
    tjs_real getrelease() const { return release_; }
    void setrelease(tjs_real value) { release_ = value; }
    void reset() { Reset(); }
    void clear() { Clear(); }

    static tjs_error setGainCallback(tTJSVariant *result, tjs_int count,
                                     tTJSVariant **params,
                                     iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<GainLimit>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count < 1)
            return TJS_E_BADPARAMCOUNT;
        self->setgain(VariantToRealDefault(params[0], self->gain_));
        if(result)
            *result = self->getgain();
        return TJS_S_OK;
    }

    static tjs_error setLimitCallback(tTJSVariant *result, tjs_int count,
                                      tTJSVariant **params,
                                      iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<GainLimit>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count < 1)
            return TJS_E_BADPARAMCOUNT;
        self->setlimit(VariantToRealDefault(params[0], self->limit_));
        if(result)
            *result = self->getlimit();
        return TJS_S_OK;
    }

    static tjs_error setParamsCallback(tTJSVariant *, tjs_int count,
                                       tTJSVariant **params,
                                       iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<GainLimit>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(count >= 1)
            self->setgain(VariantToRealDefault(params[0], self->gain_));
        if(count >= 2)
            self->setlimit(VariantToRealDefault(params[1], self->limit_));
        if(count >= 3)
            self->setclip(VariantToRealDefault(params[2], self->clip_));
        if(count >= 4)
            self->setthreshold(VariantToRealDefault(params[3], self->threshold_));
        if(count >= 5)
            self->setmix(VariantToRealDefault(params[4], self->mix_));
        return TJS_S_OK;
    }

    static tjs_error getInterfaceProperty(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<GainLimit>::GetNativeInstance(objthis, true);
        if(!self)
            return TJS_E_FAIL;
        if(result)
            *result = static_cast<tjs_int64>(self->getinterface());
        return TJS_S_OK;
    }

private:
    tjs_real mix_ = 1.0;
    tjs_real gain_ = 1.0;
    tjs_real limit_ = 1.0;
    tjs_real clip_ = 1.0;
    tjs_real threshold_ = 1.0;
    tjs_real attack_ = 0.01;
    tjs_real release_ = 0.1;
};

} // namespace

NCB_REGISTER_CLASS_DIFFER(StkFreeVerb, FreeVerbCompat) {
    Factory(&FreeVerbCompat::factory);
    RawCallback(TJS_W("interface"), &Class::getInterfaceProperty, 0, 0);
    Property(TJS_W("effectMix"), &Class::geteffectMix, &Class::seteffectMix);
    Property(TJS_W("mix"), &Class::getmix, &Class::setmix);
    Property(TJS_W("roomSize"), &Class::getroomSize, &Class::setroomSize);
    Property(TJS_W("damp"), &Class::getdamp, &Class::setdamp);
    Property(TJS_W("width"), &Class::getwidth, &Class::setwidth);
    Property(TJS_W("wet"), &Class::getwet, &Class::setwet);
    Property(TJS_W("dry"), &Class::getdry, &Class::setdry);
    RawCallback(TJS_W("setEffectMix"), &Class::setEffectMixCallback, 0);
    Method(TJS_W("reset"), &Class::reset);
    Method(TJS_W("clear"), &Class::clear);
}

NCB_REGISTER_CLASS_DIFFER(GraphicEqualizer, GraphicEqualizerCompat) {
    Factory(&GraphicEqualizerCompat::factory);
    RawCallback(TJS_W("interface"), &Class::getInterfaceProperty, 0, 0);
    Property(TJS_W("effectMix"), &Class::geteffectMix, &Class::seteffectMix);
    Property(TJS_W("mix"), &Class::getmix, &Class::setmix);
    Property(TJS_W("bandCount"), &Class::getbandCount, &Class::setbandCount);
    RawCallback(TJS_W("set"), &Class::setCallback, 0);
    RawCallback(TJS_W("setBand"), &Class::setCallback, 0);
    RawCallback(TJS_W("setGain"), &Class::setCallback, 0);
    RawCallback(TJS_W("setParams"), &Class::setParamsCallback, 0);
    Method(TJS_W("reset"), &Class::reset);
    Method(TJS_W("clear"), &Class::clear);
}

NCB_REGISTER_CLASS(DelayEffect) {
    Factory(&DelayEffect::factory);
    RawCallback(TJS_W("interface"), &Class::getInterfaceProperty, 0, 0);
    Property(TJS_W("effectMix"), &Class::geteffectMix, &Class::seteffectMix);
    Property(TJS_W("mix"), &Class::getmix, &Class::setmix);
    Property(TJS_W("delay"), &Class::getdelay, &Class::setdelay);
    Property(TJS_W("maxDelay"), &Class::getmaxDelay, &Class::setmaxDelay);
    Property(TJS_W("feedback"), &Class::getfeedback, &Class::setfeedback);
    RawCallback(TJS_W("setDelay"), &Class::setDelayCallback, 0);
    RawCallback(TJS_W("setParams"), &Class::setParamsCallback, 0);
    Method(TJS_W("reset"), &Class::reset);
    Method(TJS_W("clear"), &Class::clear);
}

NCB_REGISTER_CLASS(WaveDelay) {
    Factory(&WaveDelay::factory);
    RawCallback(TJS_W("interface"), &Class::getInterfaceProperty, 0, 0);
    Property(TJS_W("effectMix"), &Class::geteffectMix, &Class::seteffectMix);
    Property(TJS_W("mix"), &Class::getmix, &Class::setmix);
    Property(TJS_W("delay"), &Class::getdelay, &Class::setdelay);
    Property(TJS_W("maxDelay"), &Class::getmaxDelay, &Class::setmaxDelay);
    Property(TJS_W("feedback"), &Class::getfeedback, &Class::setfeedback);
    RawCallback(TJS_W("setDelay"), &Class::setDelayCallback, 0);
    RawCallback(TJS_W("setParams"), &Class::setParamsCallback, 0);
    Method(TJS_W("reset"), &Class::reset);
    Method(TJS_W("clear"), &Class::clear);
}

NCB_REGISTER_CLASS(GainLimit) {
    Factory(&GainLimit::factory);
    RawCallback(TJS_W("interface"), &Class::getInterfaceProperty, 0, 0);
    Property(TJS_W("effectMix"), &Class::geteffectMix, &Class::seteffectMix);
    Property(TJS_W("mix"), &Class::getmix, &Class::setmix);
    Property(TJS_W("gain"), &Class::getgain, &Class::setgain);
    Property(TJS_W("limit"), &Class::getlimit, &Class::setlimit);
    Property(TJS_W("clip"), &Class::getclip, &Class::setclip);
    Property(TJS_W("threshold"), &Class::getthreshold, &Class::setthreshold);
    Property(TJS_W("attack"), &Class::getattack, &Class::setattack);
    Property(TJS_W("release"), &Class::getrelease, &Class::setrelease);
    RawCallback(TJS_W("setGain"), &Class::setGainCallback, 0);
    RawCallback(TJS_W("setLimit"), &Class::setLimitCallback, 0);
    RawCallback(TJS_W("setParams"), &Class::setParamsCallback, 0);
    Method(TJS_W("reset"), &Class::reset);
    Method(TJS_W("clear"), &Class::clear);
}
