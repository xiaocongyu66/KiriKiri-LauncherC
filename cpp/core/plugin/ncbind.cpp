#include "ncbind.hpp"
#include <set>

#if defined(__ANDROID__)
extern "C" void KR2RenderProbeWriteF(const char *fmt, ...);
#define KR2_PLUG_LOG(...) KR2RenderProbeWriteF(__VA_ARGS__)
#else
#define KR2_PLUG_LOG(...) ((void)0)
#endif

// static変数の実体

// auto register 先頭ポインタ
ncbAutoRegister::ThisClassT const*
ncbAutoRegister::_top[ncbAutoRegister::LINE_COUNT] = NCB_INNER_AUTOREGISTER_LINES_INSTANCE;

std::map<ttstr, ncbAutoRegister::INTERNAL_PLUGIN_LISTS > ncbAutoRegister::_internal_plugins;

bool ncbAutoRegister::LoadModule(const ttstr &_name)
{
	ttstr name = _name.AsLowerCase();
	// already load
    if (TVPRegisteredPlugins.find(name) != TVPRegisteredPlugins.end())
		return true;
	auto it = _internal_plugins.find(name);
	if (it != _internal_plugins.end()) {
		for (const auto & plugin_list : it->second.lists) {
            for (auto i : plugin_list) {
				i->Regist();
			}
		}
		TVPRegisteredPlugins.insert(name);
		return true;
	}
	return false;
}

bool ncbAutoRegister::HasModule(const ttstr &_name)
{
	ttstr name = _name.AsLowerCase();
	return _internal_plugins.find(name) != _internal_plugins.end();
}

// LoadAllModules: register every internal plugin module that has been
// registered into _internal_plugins via NCB_REGISTER_*. This mirrors the
// AetherKiri / KrKr2-Next behaviour where every layerEx*, layerex_draw,
// etc. attaches to the Layer / Bitmap classes at startup so KAG
// scripts that expect copyRaster / copyBottomBlueToTopAlpha / drawImage
// to exist as Layer methods will find them without an explicit
// Plugins.link("xxx.dll") call.
void ncbAutoRegister::LoadAllModules()
{
	int total = 0;
	int registered = 0;
	int skipped = 0;
	for (auto &kv : _internal_plugins) {
		++total;
		const ttstr &name = kv.first;
		if (name == TJS_W("packinone.dll")) {
			// PackinOne injects game-script helpers and must run at the
			// script-requested timing, after the game has defined AffineSource.
			++skipped;
			continue;
		}
		if (TVPRegisteredPlugins.find(name) != TVPRegisteredPlugins.end()) {
			++skipped;
			continue;
		}
		KR2_PLUG_LOG("[plug] LoadAll: register '%s'",
			name.AsStdString().c_str());
		try {
			for (const auto & plugin_list : kv.second.lists) {
				for (auto i : plugin_list) {
					try {
						i->Regist();
					} catch(...) {
						KR2_PLUG_LOG("[plug] LoadAll: Regist threw for '%s'",
							name.AsStdString().c_str());
						// keep going so one bad plugin does not abort the
						// rest of internal-plugin registration
					}
				}
			}
			TVPRegisteredPlugins.insert(name);
			++registered;
		} catch(...) {
			KR2_PLUG_LOG("[plug] LoadAll: outer throw for '%s'",
				name.AsStdString().c_str());
		}
	}
	KR2_PLUG_LOG("[plug] LoadAll: done total=%d registered=%d skipped=%d",
		total, registered, skipped);
}
