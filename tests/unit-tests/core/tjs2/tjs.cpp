//
// Created by LiDong on 2025/8/28.
//

#include <fstream>
#include <filesystem>
#include <catch2/catch_test_macros.hpp>

#include "DebugImpl.h"
#include "FontImpl.h"
#include "Platform.h"
#include "test_config.h"
#include "TextStream.h"
#include "tjsScriptBlock.h"

class TJSConsoleOutputDef final : public iTJSConsoleOutput {
public:
    void ExceptionPrint(const tjs_char *msg) override {
        spdlog::get("tjs2")->critical(ttstr{ msg }.AsStdString());
    }

    void Print(const tjs_char *msg) override {
        spdlog::get("tjs2")->info(ttstr{ msg }.AsStdString());
    }
} static iTJSConsoleOutputDef{};

TEST_CASE("text stream reads bomless cp932 scripts") {
    const auto path =
        std::filesystem::temp_directory_path() / "krkr2_cp932_stand_test.tjs";
    const unsigned char cp932Script[] = {
        '[',  '\r', '\n', '%',  '[',  '\r', '\n', ' ',
        'f',  'i',  'l',  'e',  'n',  'a',  'm',  'e',
        ':',  '\'', 0x98, 0x61, 0x91, 0x74, 'a',  '\'',
        ',',  '\r', '\n', ']',  ',',  '\r', '\n', ']',
        '\r', '\n',
    };

    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(cp932Script),
                  sizeof(cp932Script));
    }

    ttstr text;
    auto *stream = TVPCreateTextStreamForRead(path.string().c_str(), "");
    stream->Read(text, 0);
    delete stream;
    std::filesystem::remove(path);

    const std::u16string got(
        reinterpret_cast<const char16_t *>(text.c_str()),
        static_cast<size_t>(text.GetLen()));
    REQUIRE(got.find(u"filename:'\u548c\u594fa'") != std::u16string::npos);
}


TEST_CASE("exec tjs2 script") {
    const auto tvPScriptEngine = new tTJS();
    tvPScriptEngine->SetPPValue(TJS_W("krkr2"), 1);
    tvPScriptEngine->SetConsoleOutput(&iTJSConsoleOutputDef);

    // register some TVP classes/objects/functions/propeties
    tTJSVariant val;
    iTJSDispatch2 *dsp;
    iTJSDispatch2 *global = tvPScriptEngine->GetGlobalNoAddRef();

#define REGISTER_OBJECT(classname, instance)                                   \
    dsp = (instance);                                                          \
    val = tTJSVariant(dsp /*, dsp*/);                                          \
    dsp->Release();                                                            \
    global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_W(#classname),      \
                    nullptr, &val, global);

    REGISTER_OBJECT(Debug, TVPCreateNativeClass_Debug());

#define SCRIPT(file_name)                                                      \
    do {                                                                       \
        ttstr tvPInitTJSScriptText{};                                          \
        auto *stream = TVPCreateTextStreamForRead(                             \
            TEST_FILES_PATH "/tjs2/" file_name, "");                           \
        stream->Read(tvPInitTJSScriptText, 0);                                 \
        tvPScriptEngine->ExecScript(tvPInitTJSScriptText);                     \
        delete stream;                                                         \
    } while(false)

    SECTION("exec test_class.tjs") { SCRIPT("test_class.tjs"); }

    SECTION("exec test_function.tjs") { SCRIPT("test_function.tjs"); }

    SECTION("exec test_misc.tjs") { SCRIPT("test_misc.tjs"); }

    SECTION("exec test_string.tjs") { SCRIPT("test_string.tjs"); }

    SECTION("exec test_variant.tjs") { SCRIPT("test_variant.tjs"); }

    SECTION("exec test_with.tjs") { SCRIPT("test_with.tjs"); }

    // SECTION("exec test.tjs") {
    //     SCRIPT("test.tjs");
    // }

    tvPScriptEngine->Release();
}
