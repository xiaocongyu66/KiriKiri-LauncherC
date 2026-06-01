// SPDX-License-Identifier: MIT
//
// pluginNameStubs.cpp
//
// Lightweight ncbind module-name placeholders so that KAG scripts can call
// `Plugins.link("xxx.dll")` without triggering their fallback / catch
// branches even when our engine does not implement the named plugin.
//
// The functionality these dlls used to provide is either already covered
// by other compatibility shims (e.g. wfBasicEffectCompat / extrans) or is
// not relevant on Android. We simply need the ncbind auto-register table
// to contain an entry under the lower-cased dll name so that
// TVPLoadInternalPlugin -> ncbAutoRegister::LoadModule returns true.
//
// Reference: AetherKiri-main/cpp/plugins/dummy_plugin_stubs.cpp
//            AetherKiri-main/cpp/plugins/packinone.cpp
//            krkrsdl3-main/cpp/plugins/{kirikiroid2,expat,PackinOne}.cpp
// ---------------------------------------------------------------------------

#include "ncbind.hpp"
#include "ScriptMgnIntf.h"

#include <algorithm>
#include <memory>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

// ---------------------------------------------------------------------------
// Generic dummy stubs (registered via NCB_PRE_REGIST_CALLBACK).
// Each block defines NCB_MODULE_NAME, declares an empty static callback,
// then registers it. The macro pulls in the current NCB_MODULE_NAME at
// expansion time, so we have to #undef + #define between blocks.
// ---------------------------------------------------------------------------

#define NCB_MODULE_NAME TJS_W("k2compat.dll")
static void k2compat_stub() {}
NCB_PRE_REGIST_CALLBACK(k2compat_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kagexopt.dll")
static void kagexopt_stub() {}
NCB_PRE_REGIST_CALLBACK(kagexopt_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krmovie.dll")
static void krmovie_stub() {}
NCB_PRE_REGIST_CALLBACK(krmovie_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kztouch.dll")
static void kztouch_stub() {}
NCB_PRE_REGIST_CALLBACK(kztouch_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("lzfs.dll")
static void lzfs_stub() {}
NCB_PRE_REGIST_CALLBACK(lzfs_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("win32ole.dll")
static void win32ole_stub() {}
NCB_PRE_REGIST_CALLBACK(win32ole_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("menu.dll")
static void menu_dll_stub() {}
NCB_PRE_REGIST_CALLBACK(menu_dll_stub);

class MenuItemCompat {
public:
    MenuItemCompat() = default;
    ~MenuItemCompat() {
        clearChildren();
        actionOwner_.Release();
    }

    static tjs_error factory(MenuItemCompat **result, tjs_int count,
                             tTJSVariant **params, iTJSDispatch2 *objthis) {
        if(!result)
            return TJS_S_OK;

        std::unique_ptr<MenuItemCompat> self(new MenuItemCompat());
        self->owner_ = objthis;

        if(count > 0 && params && params[0] &&
           params[0]->Type() == tvtObject) {
            self->window_ = params[0]->AsObjectNoAddRef();
        }

        if(count > 1 && params && params[1]) {
            if(params[1]->Type() == tvtObject) {
                if(params[0] && params[0]->Type() == tvtObject)
                    self->actionOwner_ = params[0]->AsObjectClosure();
                self->window_ = params[1]->AsObjectNoAddRef();
            } else if(params[1]->Type() != tvtVoid) {
                self->caption_ = ttstr(*params[1]);
            }
        }

        if(count > 2 && params && params[2] && params[2]->Type() != tvtVoid)
            self->caption_ = ttstr(*params[2]);

        *result = self.release();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD add(tTJSVariant *result, tjs_int count,
                                         tTJSVariant **params,
                                         MenuItemCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;

        MenuItemCompat *child = fromVariant(params[0]);
        if(!child)
            return TJS_E_INVALIDOBJECT;
        self->addChild(child);
        if(result)
            result->Clear();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD insert(tTJSVariant *result,
                                            tjs_int count,
                                            tTJSVariant **params,
                                            MenuItemCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(count < 2 || !params || !params[0] || !params[1])
            return TJS_E_BADPARAMCOUNT;

        MenuItemCompat *child = fromVariant(params[0]);
        if(!child)
            return TJS_E_INVALIDOBJECT;
        self->insertChild(child, static_cast<tjs_int>(params[1]->AsInteger()));
        if(result)
            result->Clear();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD remove(tTJSVariant *result,
                                            tjs_int count,
                                            tTJSVariant **params,
                                            MenuItemCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;

        MenuItemCompat *child = fromVariant(params[0]);
        if(!child)
            return TJS_E_INVALIDOBJECT;
        self->removeChild(child);
        if(result)
            result->Clear();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD popup(tTJSVariant *result, tjs_int,
                                           tTJSVariant **,
                                           MenuItemCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(result)
            *result = static_cast<tTVInteger>(0);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD onClick(tTJSVariant *result, tjs_int,
                                             tTJSVariant **,
                                             MenuItemCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        return self->invokeClick(result);
    }

    static tjs_error TJS_INTF_METHOD fireClick(tTJSVariant *result,
                                               tjs_int, tTJSVariant **,
                                               MenuItemCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        return self->invokeClick(result);
    }

    tjs_error invokeClick(tTJSVariant *result) {
        if(actionOwner_.Object) {
            actionOwner_.FuncCall(0, TJS_W("action"), nullptr, result, 0,
                                  nullptr, nullptr);
        } else if(result) {
            result->Clear();
        }
        return TJS_S_OK;
    }

    ttstr getCaption() const { return caption_; }
    void setCaption(ttstr value) { caption_ = value; }

    bool getChecked() const { return checked_; }
    void setChecked(bool value) {
        checked_ = value;
        if(value && radio_ && parent_)
            parent_->uncheckRadioSiblings(this);
    }

    bool getEnabled() const { return enabled_; }
    void setEnabled(bool value) { enabled_ = value; }

    tjs_int getGroup() const { return group_; }
    void setGroup(tjs_int value) { group_ = value; }

    tjs_int getIndex() const {
        if(!parent_)
            return 0;
        const auto it =
            std::find(parent_->children_.begin(), parent_->children_.end(),
                      this);
        if(it == parent_->children_.end())
            return -1;
        return static_cast<tjs_int>(it - parent_->children_.begin());
    }

    void setIndex(tjs_int value) {
        if(!parent_)
            return;
        parent_->moveChild(this, value);
    }

    bool getRadio() const { return radio_; }
    void setRadio(bool value) {
        radio_ = value;
        if(checked_ && radio_ && parent_)
            parent_->uncheckRadioSiblings(this);
    }

    ttstr getShortcut() const { return shortcut_; }
    void setShortcut(ttstr value) { shortcut_ = value; }

    bool getVisible() const { return visible_; }
    void setVisible(bool value) { visible_ = value; }

    tjs_int64 getHMENU() const { return 0; }
    tjs_int64 getHandle() const { return 0; }

    tTJSVariant getChildren() const {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        if(!array)
            return tTJSVariant();

        tjs_int index = 0;
        for(MenuItemCompat *child : children_) {
            if(!child || !child->owner_)
                continue;
            tTJSVariant value(child->owner_, child->owner_);
            array->PropSetByNum(TJS_MEMBERENSURE, index++, &value, array);
        }

        tTJSVariant result(array, array);
        array->Release();
        return result;
    }

    tTJSVariant getParent() const { return objectVariant(parent_); }
    tTJSVariant getRoot() {
        MenuItemCompat *root = this;
        while(root->parent_)
            root = root->parent_;
        return objectVariant(root);
    }
    tTJSVariant getWindow() const {
        if(!window_)
            return tTJSVariant((iTJSDispatch2 *)nullptr);
        return tTJSVariant(window_, window_);
    }

    static tjs_error TJS_INTF_METHOD getTextToKeycode(tTJSVariant *result,
                                                      tjs_int, tTJSVariant **,
                                                      iTJSDispatch2 *) {
        if(result) {
            iTJSDispatch2 *dict = TJSCreateDictionaryObject();
            if(dict) {
                *result = tTJSVariant(dict, dict);
                dict->Release();
            } else {
                result->Clear();
            }
        }
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD getKeycodeToText(tTJSVariant *result,
                                                      tjs_int, tTJSVariant **,
                                                      iTJSDispatch2 *) {
        if(result) {
            iTJSDispatch2 *array = TJSCreateArrayObject();
            if(array) {
                *result = tTJSVariant(array, array);
                array->Release();
            } else {
                result->Clear();
            }
        }
        return TJS_S_OK;
    }

private:
    static MenuItemCompat *fromVariant(tTJSVariant *value) {
        if(!value || value->Type() != tvtObject)
            return nullptr;
        return ncbInstanceAdaptor<MenuItemCompat>::GetNativeInstance(
            value->AsObjectNoAddRef());
    }

    static tTJSVariant objectVariant(const MenuItemCompat *item) {
        if(!item || !item->owner_)
            return tTJSVariant((iTJSDispatch2 *)nullptr);
        return tTJSVariant(item->owner_, item->owner_);
    }

    bool isAncestorOf(MenuItemCompat *item) const {
        for(MenuItemCompat *current = item; current; current = current->parent_) {
            if(current == this)
                return true;
        }
        return false;
    }

    void addChild(MenuItemCompat *child) {
        insertChild(child, static_cast<tjs_int>(children_.size()));
    }

    void insertChild(MenuItemCompat *child, tjs_int index) {
        if(!child || child == this || child->isAncestorOf(this))
            return;

        if(child->parent_)
            child->parent_->detachChild(child, false);

        if(index < 0)
            index = 0;
        if(index > static_cast<tjs_int>(children_.size()))
            index = static_cast<tjs_int>(children_.size());

        children_.insert(children_.begin() + index, child);
        child->parent_ = this;
        child->window_ = window_;
        if(child->owner_)
            child->owner_->AddRef();
        if(child->checked_ && child->radio_)
            uncheckRadioSiblings(child);
    }

    void removeChild(MenuItemCompat *child) { detachChild(child, true); }

    void detachChild(MenuItemCompat *child, bool clearParent) {
        const auto it =
            std::find(children_.begin(), children_.end(), child);
        if(it == children_.end())
            return;

        iTJSDispatch2 *owner = child ? child->owner_ : nullptr;
        children_.erase(it);
        if(child && clearParent)
            child->parent_ = nullptr;
        if(owner)
            owner->Release();
    }

    void moveChild(MenuItemCompat *child, tjs_int index) {
        const auto it =
            std::find(children_.begin(), children_.end(), child);
        if(it == children_.end())
            return;

        MenuItemCompat *saved = *it;
        children_.erase(it);
        if(index < 0)
            index = 0;
        if(index > static_cast<tjs_int>(children_.size()))
            index = static_cast<tjs_int>(children_.size());
        children_.insert(children_.begin() + index, saved);
    }

    void clearChildren() {
        for(MenuItemCompat *child : children_) {
            if(!child)
                continue;
            child->parent_ = nullptr;
            if(child->owner_)
                child->owner_->Release();
        }
        children_.clear();
    }

    void uncheckRadioSiblings(MenuItemCompat *selected) {
        for(MenuItemCompat *child : children_) {
            if(child && child != selected && child->radio_ &&
               child->group_ == selected->group_)
                child->checked_ = false;
        }
    }

    iTJSDispatch2 *owner_ = nullptr;
    iTJSDispatch2 *window_ = nullptr;
    tTJSVariantClosure actionOwner_ = tTJSVariantClosure(nullptr, nullptr);
    MenuItemCompat *parent_ = nullptr;
    std::vector<MenuItemCompat *> children_;
    ttstr caption_;
    ttstr shortcut_;
    bool checked_ = false;
    bool enabled_ = true;
    bool radio_ = false;
    bool visible_ = true;
    tjs_int group_ = 0;
};

static tTJSVariant CreateMenuItemForWindow(iTJSDispatch2 *window) {
    tTJSVariant classValue;
    TVPExecuteExpression(TJS_W("MenuItem"), &classValue);
    iTJSDispatch2 *classObject = classValue.AsObjectNoAddRef();
    if(!classObject)
        return tTJSVariant();

    iTJSDispatch2 *created = nullptr;
    tjs_error hr;
    if(window) {
        tTJSVariant windowValue(window, window);
        tTJSVariant *params[1] = { &windowValue };
        hr = classObject->CreateNew(0, nullptr, nullptr, &created, 1, params,
                                    classObject);
    } else {
        hr = classObject->CreateNew(0, nullptr, nullptr, &created, 0, nullptr,
                                    classObject);
    }
    if(TJS_FAILED(hr) || !created)
        return tTJSVariant();

    tTJSVariant result(created, created);
    created->Release();
    return result;
}

NCB_REGISTER_CLASS_DIFFER(MenuItem, MenuItemCompat) {
    Factory(&MenuItemCompat::factory);
    RawCallback(TJS_W("add"), &Class::add, 0);
    RawCallback(TJS_W("insert"), &Class::insert, 0);
    RawCallback(TJS_W("remove"), &Class::remove, 0);
    RawCallback(TJS_W("popup"), &Class::popup, 0);
    RawCallback(TJS_W("onClick"), &Class::onClick, 0);
    RawCallback(TJS_W("fireClick"), &Class::fireClick, 0);
    NCB_PROPERTY(caption, getCaption, setCaption);
    NCB_PROPERTY(checked, getChecked, setChecked);
    NCB_PROPERTY_RO(children, getChildren);
    NCB_PROPERTY(enabled, getEnabled, setEnabled);
    NCB_PROPERTY(group, getGroup, setGroup);
    NCB_PROPERTY(index, getIndex, setIndex);
    NCB_PROPERTY_RO(parent, getParent);
    NCB_PROPERTY(radio, getRadio, setRadio);
    NCB_PROPERTY_RO(root, getRoot);
    NCB_PROPERTY(shortcut, getShortcut, setShortcut);
    NCB_PROPERTY(visible, getVisible, setVisible);
    NCB_PROPERTY_RO(window, getWindow);
    NCB_PROPERTY_RO(HMENU, getHMENU);
    NCB_PROPERTY_RO(handle, getHandle);
    RawCallback(TJS_W("textToKeycode"), &Class::getTextToKeycode, 0,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("keycodeToText"), &Class::getKeycodeToText, 0,
                TJS_STATICMEMBER);
    Variant(TJS_W("tpmLeftButton"), (tjs_int)0x0000);
    Variant(TJS_W("tpmRightButton"), (tjs_int)0x0002);
    Variant(TJS_W("tpmLeftAlign"), (tjs_int)0x0000);
    Variant(TJS_W("tpmCenterAlign"), (tjs_int)0x0004);
    Variant(TJS_W("tpmRightAlign"), (tjs_int)0x0008);
    Variant(TJS_W("tpmTopAlign"), (tjs_int)0x0000);
    Variant(TJS_W("tpmVCenterAlign"), (tjs_int)0x0010);
    Variant(TJS_W("tpmBottomAlign"), (tjs_int)0x0020);
    Variant(TJS_W("tpmHorizontal"), (tjs_int)0x0000);
    Variant(TJS_W("tpmVertical"), (tjs_int)0x0040);
    Variant(TJS_W("tpmNoNotify"), (tjs_int)0x0080);
    Variant(TJS_W("tpmReturnCmd"), (tjs_int)0x0100);
    Variant(TJS_W("tpmRecurse"), (tjs_int)0x0001);
    Variant(TJS_W("tpmHorPosAnimation"), (tjs_int)0x0400);
    Variant(TJS_W("tpmHorNegAnimation"), (tjs_int)0x0800);
    Variant(TJS_W("tpmVerPosAnimation"), (tjs_int)0x1000);
    Variant(TJS_W("tpmVerNegAnimation"), (tjs_int)0x2000);
    Variant(TJS_W("tpmNoAnimation"), (tjs_int)0x4000);
}

class WindowMenuCompat {
public:
    explicit WindowMenuCompat(iTJSDispatch2 *obj) : window_(obj) {}

    tTJSVariant getMenu() {
        if(menu_.Type() == tvtVoid || !menu_.AsObjectNoAddRef())
            menu_ = CreateMenuItemForWindow(window_);
        return menu_;
    }
    void setMenu(tTJSVariant v) { menu_ = v; }

private:
    iTJSDispatch2 *window_ = nullptr;
    tTJSVariant menu_;
};

NCB_GET_INSTANCE_HOOK(WindowMenuCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowMenuCompat, Window) {
    NCB_PROPERTY(menu, getMenu, setMenu);
}

// ---------------------------------------------------------------------------
// VoiceEffect-related stubs.
// voiceeffect.tjs (used by wamsoft titles such as ライムライト・レモネードジャム)
// links wumultitrack.dll / wvdecoder.dll at top level. The actual filter and
// codec features are already provided by wfBasicEffectCompat /
// wfTypicalDSPCompat / extrans (wuvorbis, wuopus, wuflac). We only need the
// module name to resolve so the script's top-level Plugins.link does not go
// down the SystemConfig.voiceEffectForceDisabled = 1 catch branch.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wumultitrack.dll")
static void wumultitrack_stub() {}
NCB_PRE_REGIST_CALLBACK(wumultitrack_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wvdecoder.dll")
static void wvdecoder_stub() {}
NCB_PRE_REGIST_CALLBACK(wvdecoder_stub);
