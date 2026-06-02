//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS2 Script Managing
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include "tjs.h"
#include "tjsDebug.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "DebugIntf.h"
#include "WindowIntf.h"
#include "LayerIntf.h"
#include "CDDAIntf.h"
#include "MIDIIntf.h"
#include "WaveIntf.h"
#include "TimerIntf.h"
#include "EventIntf.h"
#include "SystemIntf.h"
#include "PluginIntf.h"
#include "MenuItemIntf.h"
#include "ClipboardIntf.h"
#include "MsgIntf.h"
#include "KAGParser.h"
#include "VideoOvlIntf.h"
#include "PadIntf.h"
#include "TextStream.h"

#if defined(__ANDROID__)
extern "C" void KR2RenderProbeWriteF(const char *fmt, ...);
#define KR2_SCR_LOG(...) KR2RenderProbeWriteF(__VA_ARGS__)
#else
#define KR2_SCR_LOG(...) ((void)0)
#endif
#include "Random.h"
#include "tjsRandomGenerator.h"
#include "SysInitIntf.h"
#include "PhaseVocoderFilter.h"
#include "BasicDrawDevice.h"
#include "BinaryStream.h"
#include "SysInitImpl.h"
#include "Application.h"

#include <spdlog/spdlog.h>

#include "RectItf.h"
#include "ImageFunction.h"
#include "BitmapIntf.h"
#include "tjsScriptBlock.h"
#include "ApplicationSpecialPath.h"
#include "SystemImpl.h"
#include "BitmapLayerTreeOwner.h"
#include "Extension.h"
#include "Platform.h"
#include "ConfigManager/LocaleConfigManager.h"

//---------------------------------------------------------------------------
// Script system initialization script
//---------------------------------------------------------------------------
static const tjs_nchar *TVPInitTJSScript =
    // note that this script is stored as narrow string
    TJS_N(R"(const
/* constants */
 /* tTVPBorderStyle */ bsNone=0,  bsSingle=1,  bsSizeable=2,  bsDialog=3,  bsToolWindow=4,  bsSizeToolWin=5,
 /* tTVPUpdateType */ utNormal=0,  utEntire =1,
 /* tTVPMouseButton */  mbLeft=0,  mbRight=1,  mbMiddle=2, mbX1=3, mbX2=4,
 /* tTVPMouseCursorState */ mcsVisible=0, mcsTempHidden=1, mcsHidden=2,
 /* tTVPImeMode */ imDisable=0, imClose=1, imOpen=2, imDontCare=3, imSAlpha=4, imAlpha=5, imHira=6, imSKata=7, imKata=8, imChinese=9, imSHanguel=10, imHanguel=11,
 /* Set of shift state */  ssShift=(1<<0),  ssAlt=(1<<1),  ssCtrl=(1<<2),  ssLeft=(1<<3),  ssRight=(1<<4),  ssMiddle=(1<<5),  ssDouble =(1<<6),  ssRepeat = (1<<7),
 /* TVP_FSF_???? */ fsfFixedPitch=1, fsfSameCharSet=2, fsfNoVertical=4, 
	fsfTrueTypeOnly=8, fsfUseFontFace=0x100, fsfIgnoreSymbol=0x10,
 /* tTVPLayerType */ ltBinder=0, ltCoverRect=1, ltOpaque=1, ltTransparent=2, ltAlpha=2, ltAdditive=3, ltSubtractive=4, ltMultiplicative=5, ltEffect=6, ltFilter=7, ltDodge=8, ltDarken=9, ltLighten=10, ltScreen=11, ltAddAlpha = 12,
	ltPsNormal = 13, ltPsAdditive = 14, ltPsSubtractive = 15, ltPsMultiplicative = 16, ltPsScreen = 17, ltPsOverlay = 18, ltPsHardLight = 19, ltPsSoftLight = 20, ltPsColorDodge = 21, ltPsColorDodge5 = 22, ltPsColorBurn = 23, ltPsLighten = 24, ltPsDarken = 25, ltPsDifference = 26, ltPsDifference5 = 27, ltPsExclusion = 28, 
 /* tTVPBlendOperationMode */ omPsNormal = ltPsNormal,omPsAdditive = ltPsAdditive,omPsSubtractive = ltPsSubtractive,omPsMultiplicative = ltPsMultiplicative,omPsScreen = ltPsScreen,omPsOverlay = ltPsOverlay,omPsHardLight = ltPsHardLight,omPsSoftLight = ltPsSoftLight,omPsColorDodge = ltPsColorDodge,omPsColorDodge5 = ltPsColorDodge5,omPsColorBurn = ltPsColorBurn,omPsLighten = ltPsLighten,omPsDarken = ltPsDarken,omPsDifference = ltPsDifference,omPsDifference5 = ltPsDifference5,omPsExclusion = ltPsExclusion, 
	omAdditive=ltAdditive, omSubtractive=ltSubtractive, omMultiplicative=ltMultiplicative, omDodge=ltDodge, omDarken=ltDarken, omLighten=ltLighten, omScreen=ltScreen, omAddAlpha=ltAddAlpha, omOpaque=ltOpaque, omAlpha=ltAlpha, omAuto = 128,
 /* tTVPDrawFace */ dfBoth=0, dfAlpha = dfBoth, dfAddAlpha = 4, dfMain=1, dfOpaque = dfMain, dfMask=2, dfProvince=3, dfAuto=128,
 /* tTVPHitType */ htMask=0, htProvince=1,
 /* tTVPScrollTransFrom */ sttLeft=0, sttTop=1, sttRight=2, sttBottom=3,
 /* tTVPScrollTransStay */ ststNoStay=0, ststStayDest=1, ststStaySrc=2, 
 /* tTVPKAGDebugLevel */ tkdlNone=0, tkdlSimple=1, tkdlVerbose=2, 
 /* tTVPAsyncTriggerMode */	atmNormal=0, atmExclusive=1, atmAtIdle=2, 
 /* tTVPBBStretchType */ stNearest=0, stFastLinear=1, stLinear=2, stCubic=3, stSemiFastLinear = 4, stFastCubic = 5, stLanczos2 = 6, stFastLanczos2 = 7, stLanczos3 = 8, stFastLanczos3 = 9, stSpline16 = 10, stFastSpline16 = 11, stSpline36 = 12, stFastSpline36 = 13, stAreaAvg = 14, stFastAreaAvg = 15, stGaussian = 16, stFastGaussian = 17, stBlackmanSinc = 18, stFastBlackmanSinc = 19, stRefNoClip = 0x10000,
 /* tTVPClipboardFormat */ cbfText = 1,
 /* TVP_COMPACT_LEVEL_???? */ clIdle = 5, clDeactivate = 10, clMinimize = 15, clAll = 100,
 /* tTVPVideoOverlayMode Add: T.Imoto */ vomOverlay=0, vomLayer=1, vomMixer=2, vomMFEVR=3,
 /* tTVPPeriodEventReason */ perLoop = 0, perPeriod = 1, perPrepare = 2, perSegLoop = 3,
 /* tTVPSoundGlobalFocusMode */ sgfmNeverMute = 0, sgfmMuteOnMinimize = 1, sgfmMuteOnDeactivate = 2,
 /* tTVPTouchDevice */ tdNone=0, tdIntegratedTouch=0x01, tdExternalTouch=0x02, tdIntegratedPen=0x04, tdExternalPen=0x08, tdMultiInput=0x40, tdDigitizerReady=0x80,
    tdMouse=0x0100, tdMouseWheel=0x0200,
 /* Display Orientation */ oriUnknown=0, oriPortrait=1, oriLandscape=2,

/* file attributes */
 faReadOnly=0x01, faHidden=0x02, faSysFile=0x04, faVolumeID=0x08, faDirectory=0x10, faArchive=0x20, faAnyFile=0x3f,
/* mouse cursor constants */
 crDefault = 0x0,
 crNone = -1,
 crArrow = -2,
 crCross = -3,
 crIBeam = -4,
 crSize = -5,
 crSizeNESW = -6,
 crSizeNS = -7,
 crSizeNWSE = -8,
 crSizeWE = -9,
 crUpArrow = -10,
 crHourGlass = -11,
 crDrag = -12,
 crNoDrop = -13,
 crHSplit = -14,
 crVSplit = -15,
 crMultiDrag = -16,
 crSQLWait = -17,
 crNo = -18,
 crAppStart = -19,
 crHelp = -20,
 crHandPoint = -21,
 crSizeAll = -22,
 crHBeam = 1,
/* color constants */
 clScrollBar = 0x80000000,
 clBackground = 0x80000001,
 clActiveCaption = 0x80000002,
 clInactiveCaption = 0x80000003,
 clMenu = 0x80000004,
 clWindow = 0x80000005,
 clWindowFrame = 0x80000006,
 clMenuText = 0x80000007,
 clWindowText = 0x80000008,
 clCaptionText = 0x80000009,
 clActiveBorder = 0x8000000a,
 clInactiveBorder = 0x8000000b,
 clAppWorkSpace = 0x8000000c,
 clHighlight = 0x3399ff,
 clHighlightText = 0x8000000e,
 clBtnFace = 0xf0f0f0,
 clBtnShadow = 0x787878,
 clGrayText = 0x80000011,
 clBtnText = 0x000000,
 clInactiveCaptionText = 0x80000013,
 clBtnHighlight = 0x80000014,
 cl3DDkShadow = 0x80000015,
 cl3DLight = 0x80000016,
 clInfoText = 0x80000017,
 clInfoBk = 0x80000018,
 clNone = 0x1fffffff,
 clAdapt= 0x01ffffff,
 clPalIdx = 0x3000000,
 clAlphaMat = 0x4000000,
/* for Menu.trackPopup (see winuser.h) */
 tpmLeftButton      = 0x0000,
 tpmRightButton     = 0x0002,
 tpmLeftAlign       = 0x0000,
 tpmCenterAlign     = 0x0004,
 tpmRightAlign      = 0x0008,
 tpmTopAlign        = 0x0000,
 tpmVCenterAlign    = 0x0010,
 tpmBottomAlign     = 0x0020,
 tpmHorizontal      = 0x0000,
 tpmVertical        = 0x0040,
 tpmNoNotify        = 0x0080,
 tpmReturnCmd       = 0x0100,
 tpmRecurse         = 0x0001,
 tpmHorPosAnimation = 0x0400,
 tpmHorNegAnimation = 0x0800,
 tpmVerPosAnimation = 0x1000,
 tpmVerNegAnimation = 0x2000,
 tpmNoAnimation     = 0x4000,
/* for Pad.showScrollBars (see Vcl/stdctrls.hpp :: enum TScrollStyle) */
 ssNone       = 0,
 ssHorizontal = 1,
 ssVertical   = 2,
 ssBoth       = 3,
/* virtual keycodes */
 VK_LBUTTON =0x01,
 VK_RBUTTON =0x02,
 VK_CANCEL =0x03,
 VK_MBUTTON =0x04,
 VK_BACK =0x08,
 VK_TAB =0x09,
 VK_CLEAR =0x0C,
 VK_RETURN =0x0D,
 VK_SHIFT =0x10,
 VK_CONTROL =0x11,
 VK_MENU =0x12,
 VK_PAUSE =0x13,
 VK_CAPITAL =0x14,
 VK_KANA =0x15,
 VK_HANGEUL =0x15,
 VK_HANGUL =0x15,
 VK_JUNJA =0x17,
 VK_FINAL =0x18,
 VK_HANJA =0x19,
 VK_KANJI =0x19,
 VK_ESCAPE =0x1B,
 VK_CONVERT =0x1C,
 VK_NONCONVERT =0x1D,
 VK_ACCEPT =0x1E,
 VK_MODECHANGE =0x1F,
 VK_SPACE =0x20,
 VK_PRIOR =0x21,
 VK_NEXT =0x22,
 VK_END =0x23,
 VK_HOME =0x24,
 VK_LEFT =0x25,
 VK_UP =0x26,
 VK_RIGHT =0x27,
 VK_DOWN =0x28,
 VK_SELECT =0x29,
 VK_PRINT =0x2A,
 VK_EXECUTE =0x2B,
 VK_SNAPSHOT =0x2C,
 VK_INSERT =0x2D,
 VK_DELETE =0x2E,
 VK_HELP =0x2F,
 VK_0 =0x30,
 VK_1 =0x31,
 VK_2 =0x32,
 VK_3 =0x33,
 VK_4 =0x34,
 VK_5 =0x35,
 VK_6 =0x36,
 VK_7 =0x37,
 VK_8 =0x38,
 VK_9 =0x39,
 VK_A =0x41,
 VK_B =0x42,
 VK_C =0x43,
 VK_D =0x44,
 VK_E =0x45,
 VK_F =0x46,
 VK_G =0x47,
 VK_H =0x48,
 VK_I =0x49,
 VK_J =0x4A,
 VK_K =0x4B,
 VK_L =0x4C,
 VK_M =0x4D,
 VK_N =0x4E,
 VK_O =0x4F,
 VK_P =0x50,
 VK_Q =0x51,
 VK_R =0x52,
 VK_S =0x53,
 VK_T =0x54,
 VK_U =0x55,
 VK_V =0x56,
 VK_W =0x57,
 VK_X =0x58,
 VK_Y =0x59,
 VK_Z =0x5A,
 VK_LWIN =0x5B,
 VK_RWIN =0x5C,
 VK_APPS =0x5D,
 VK_NUMPAD0 =0x60,
 VK_NUMPAD1 =0x61,
 VK_NUMPAD2 =0x62,
 VK_NUMPAD3 =0x63,
 VK_NUMPAD4 =0x64,
 VK_NUMPAD5 =0x65,
 VK_NUMPAD6 =0x66,
 VK_NUMPAD7 =0x67,
 VK_NUMPAD8 =0x68,
 VK_NUMPAD9 =0x69,
 VK_MULTIPLY =0x6A,
 VK_ADD =0x6B,
 VK_SEPARATOR =0x6C,
 VK_SUBTRACT =0x6D,
 VK_DECIMAL =0x6E,
 VK_DIVIDE =0x6F,
 VK_F1 =0x70,
 VK_F2 =0x71,
 VK_F3 =0x72,
 VK_F4 =0x73,
 VK_F5 =0x74,
 VK_F6 =0x75,
 VK_F7 =0x76,
 VK_F8 =0x77,
 VK_F9 =0x78,
 VK_F10 =0x79,
 VK_F11 =0x7A,
 VK_F12 =0x7B,
 VK_F13 =0x7C,
 VK_F14 =0x7D,
 VK_F15 =0x7E,
 VK_F16 =0x7F,
 VK_F17 =0x80,
 VK_F18 =0x81,
 VK_F19 =0x82,
 VK_F20 =0x83,
 VK_F21 =0x84,
 VK_F22 =0x85,
 VK_F23 =0x86,
 VK_F24 =0x87,
 VK_NUMLOCK =0x90,
 VK_SCROLL =0x91,
 VK_LSHIFT =0xA0,
 VK_RSHIFT =0xA1,
 VK_LCONTROL =0xA2,
 VK_RCONTROL =0xA3,
 VK_LMENU =0xA4,
 VK_RMENU =0xA5,
/* VK_PADXXXX are KIRIKIRI specific */
 VK_PADLEFT =0x1B5,
 VK_PADUP =0x1B6,
 VK_PADRIGHT =0x1B7,
 VK_PADDOWN =0x1B8,
 VK_PAD1 =0x1C0,
 VK_PAD2 =0x1C1,
 VK_PAD3 =0x1C2,
 VK_PAD4 =0x1C3,
 VK_PAD5 =0x1C4,
 VK_PAD6 =0x1C5,
 VK_PAD7 =0x1C6,
 VK_PAD8 =0x1C7,
 VK_PAD9 =0x1C8,
 VK_PAD10 =0x1C9,
 VK_PADANY = 0x1DF,
 VK_PROCESSKEY =0xE5,
 VK_ATTN =0xF6,
 VK_CRSEL =0xF7,
 VK_EXSEL =0xF8,
 VK_EREOF =0xF9,
 VK_PLAY =0xFA,
 VK_ZOOM =0xFB,
 VK_NONAME =0xFC,
 VK_PA1 =0xFD,
 VK_OEM_CLEAR =0xFE,
 frFreeType=0,
 frGDI=1,
/* graphic cache system */
 gcsAuto=-1,
/* image 'mode' tag (mainly is generated by image format converter) constants */
 imageTagLayerType = %[
opaque		:%[type:ltOpaque			],
rect		:%[type:ltOpaque			],
alpha		:%[type:ltAlpha				],
transparent	:%[type:ltAlpha				],
addalpha	:%[type:ltAddAlpha			],
add			:%[type:ltAdditive			],
sub			:%[type:ltSubtractive		],
mul			:%[type:ltMultiplicative	],
dodge		:%[type:ltDodge				],
darken		:%[type:ltDarken			],
lighten		:%[type:ltLighten			],
screen		:%[type:ltScreen			],
psnormal	:%[type:ltPsNormal			],
psadd		:%[type:ltPsAdditive		],
pssub		:%[type:ltPsSubtractive		],
psmul		:%[type:ltPsMultiplicative	],
psscreen	:%[type:ltPsScreen			],
psoverlay	:%[type:ltPsOverlay			],
pshlight	:%[type:ltPsHardLight		],
psslight	:%[type:ltPsSoftLight		],
psdodge		:%[type:ltPsColorDodge		],
psdodge5	:%[type:ltPsColorDodge5		],
psburn		:%[type:ltPsColorBurn		],
pslighten	:%[type:ltPsLighten			],
psdarken	:%[type:ltPsDarken			],
psdiff		:%[type:ltPsDifference		],
psdiff5		:%[type:ltPsDifference5		],
psexcl		:%[type:ltPsExclusion		],
],
/* draw thread num */
 dtnAuto=0
;)");
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// global variables
//---------------------------------------------------------------------------
tTJS *TVPScriptEngine = nullptr;
ttstr TVPStartupScriptName(TJS_W("startup.tjs"));
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Garbage Collection stuff
//---------------------------------------------------------------------------
class tTVPTJSGCCallback : public tTVPCompactEventCallbackIntf {
    void OnCompact(tjs_int level) override {
        // OnCompact method from tTVPCompactEventCallbackIntf
        // called when the application is idle, deactivated,
        // minimized, or etc...
        if(TVPScriptEngine) {
            if(level >= TVP_COMPACT_LEVEL_IDLE) {
                TVPScriptEngine->DoGarbageCollection();
            }
        }
    }
} static TVPTJSGCCallback;
//---------------------------------------------------------------------------
// Forward declarations for compat-layer entry points implemented in
// cpp/plugins/. They must be reachable from TVPInitScriptEngine /
// TVPExecuteStartupScript below.
void TVPEnsureKirikiroidCompatibilityPatch();
void TVPLoadXP3FilterScript(bool searchArchives);
void TVPRunRuntimeCompatibilityPatches();
void TVPRegisterVoiceEffectStubs();
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// TVPInitScriptEngine
//---------------------------------------------------------------------------
static bool TVPScriptEngineInit = false;

void TVPInitScriptEngine() {
    if(TVPScriptEngineInit)
        return;
    TVPScriptEngineInit = true;

    tTJSVariant val;

    // Set eval expression mode
    if(TVPGetCommandLine(TJS_W("-evalcontext"), &val)) {
        ttstr str(val);
        if(str == TJS_W("global")) {
            TJSEvalOperatorIsOnGlobal = true;
            TJSWarnOnNonGlobalEvalOperator = true;
        }
    }

    // Set igonre-prop compat mode
    if(TVPGetCommandLine(TJS_W("-unaryaster"), &val)) {
        ttstr str(val);
        if(str == TJS_W("compat")) {
            TJSUnaryAsteriskIgnoresPropAccess = true;
        }
    }

    // Set debug mode
    if(TVPGetCommandLine(TJS_W("-debug"), &val)) {
        ttstr str(val);
        if(str == TJS_W("yes")) {
            TJSEnableDebugMode = true;
            TVPAddImportantLog((const tjs_char *)TVPWarnDebugOptionEnabled);
            //			if(TVPGetCommandLine(TJS_W("-warnrundelobj"),
            //&val) )
            //			{
            //				str = val;
            //				if(str == TJS_W("yes"))
            //				{
            TJSWarnOnExecutionOnDeletingObject = true;
            //				}
            //			}
        }
    }

#ifdef TVP_START_UP_SCRIPT_NAME
    TVPStartupScriptName = TVP_START_UP_SCRIPT_NAME;
#else
    // Set startup script name
    if(TVPGetCommandLine(TJS_W("-startup"), &val)) {
        ttstr str(val);
        TVPStartupScriptName = str;
    }
#endif

    // create script engine object
    TVPScriptEngine = new tTJS();

    // add kirikiriz
    //	TVPScriptEngine->SetPPValue( TJS_W("kirikiriz"), 1 );

    // set TJSGetRandomBits128
    TJSGetRandomBits128 = TVPGetRandomBits128;

    // script system initialization
    TVPScriptEngine->ExecScript(ttstr(TVPInitTJSScript));

    // set console output gateway handler
    TVPScriptEngine->SetConsoleOutput(TVPGetTJS2ConsoleOutputGateway());

    // set text stream functions
    TJSCreateTextStreamForRead = TVPCreateTextStreamForRead;
    TJSCreateTextStreamForWrite = TVPCreateTextStreamForWrite;

    // set binary stream functions
    TJSCreateBinaryStreamForRead = TVPCreateBinaryStreamForRead;
    TJSCreateBinaryStreamForWrite = TVPCreateBinaryStreamForWrite;

    // register some TVP classes/objects/functions/propeties
    iTJSDispatch2 *dsp;
    iTJSDispatch2 *global = TVPScriptEngine->GetGlobalNoAddRef();

    auto registerObject = [&](const tjs_char *classname, auto instance) {
        auto dsp = instance;
        tTJSVariant val(dsp /*, dsp */);
        dsp->Release();
        global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, classname, nullptr,
                        &val, global);
    };

    /* classes */
    registerObject(TJS_W("Debug"), TVPCreateNativeClass_Debug());
    registerObject(TJS_W("Font"), TVPCreateNativeClass_Font());
    registerObject(TJS_W("Layer"), TVPCreateNativeClass_Layer());
    registerObject(TJS_W("CDDASoundBuffer"),
                   TVPCreateNativeClass_CDDASoundBuffer());
    registerObject(TJS_W("MIDISoundBuffer"),
                   TVPCreateNativeClass_MIDISoundBuffer());
    registerObject(TJS_W("Timer"), TVPCreateNativeClass_Timer());
    registerObject(TJS_W("AsyncTrigger"), TVPCreateNativeClass_AsyncTrigger());
    registerObject(TJS_W("System"), TVPCreateNativeClass_System());
    registerObject(TJS_W("Storages"), TVPCreateNativeClass_Storages());
    registerObject(TJS_W("Plugins"), TVPCreateNativeClass_Plugins());
    registerObject(TJS_W("VideoOverlay"), TVPCreateNativeClass_VideoOverlay());
    registerObject(TJS_W("Pad"), TVPCreateNativeClass_Pad());
    registerObject(TJS_W("Clipboard"), TVPCreateNativeClass_Clipboard());
    registerObject(TJS_W("Scripts"),
                   TVPCreateNativeClass_Scripts()); // declared in this file
    registerObject(TJS_W("Rect"), TVPCreateNativeClass_Rect());
    registerObject(TJS_W("Bitmap"), TVPCreateNativeClass_Bitmap());
    registerObject(TJS_W("ImageFunction"),
                   TVPCreateNativeClass_ImageFunction());
    registerObject(TJS_W("BitmapLayerTreeOwner"),
                   TVPCreateNativeClass_BitmapLayerTreeOwner());

    /* KAG special support */
    registerObject(TJS_W("KAGParser"), TVPCreateNativeClass_KAGParser());

    /* WaveSoundBuffer and its filters */
    iTJSDispatch2 *waveclass = nullptr;
    registerObject(TJS_W("WaveSoundBuffer"),
                   (waveclass = TVPCreateNativeClass_WaveSoundBuffer()));
    dsp = new tTJSNC_PhaseVocoder();
    val = tTJSVariant(dsp);
    dsp->Release();
    waveclass->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
                       TJS_W("PhaseVocoder"), nullptr, &val, waveclass);

    /* Window and its drawdevices */
    iTJSDispatch2 *windowclass = nullptr;
    registerObject(TJS_W("Window"),
                   (windowclass = TVPCreateNativeClass_Window()));
    dsp = new tTJSNC_BasicDrawDevice();
    val = tTJSVariant(dsp);
    dsp->Release();
    windowclass->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
                         TJS_W("BasicDrawDevice"), nullptr, &val, windowclass);

    windowclass->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
                         TJS_W("PassThroughDrawDevice"), nullptr, &val,
                         windowclass); // compatible for old version kr2

    CreateShortCutKeyCodeTable();

    auto *gWindowMenuProperty = new WindowMenuProperty();
    val = tTJSVariant(gWindowMenuProperty);
    gWindowMenuProperty->Release();
    windowclass->PropSet(TJS_MEMBERENSURE, TJS_W("menu"), nullptr, &val,
                         windowclass);
    registerObject(TJS_W("MenuItem"), TVPCreateNativeClass_MenuItem());

    // Add Extension Classes
    TVPCauseAtInstallExtensionClass(global);
    // Register no-op stubs for KAG-side compat names that some games
    // depend on but aren't always created (e.g. when wamsoft DLLs are
    // missing). Doing this in C++ avoids any TJS-script bootstrapping
    // ambiguity and is safe even if a real plugin later replaces the
    // stub.
    TVPRegisterVoiceEffectStubs();
    // Garbage Collection Hook
    TVPAddCompactEventHook(&TVPTJSGCCallback);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPUninitScriptEngine
//---------------------------------------------------------------------------
static bool TVPScriptEngineUninit = false;

void TVPUninitScriptEngine() {
    if(TVPScriptEngineUninit)
        return;
    TVPScriptEngineUninit = true;

    // TVPScriptEngine->Shutdown();
    TVPScriptEngine->Release();
    /*
        Objects, theirs lives are contolled by reference counter, may
       not be all freed here in some occations.
    */
    TVPScriptEngine = nullptr;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPRestartScriptEngine
//---------------------------------------------------------------------------
void TVPRestartScriptEngine() {
    TVPUninitScriptEngine();
    TVPScriptEngineInit = false;
    TVPInitScriptEngine();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetScriptEngine
//---------------------------------------------------------------------------
tTJS *TVPGetScriptEngine() { return TVPScriptEngine; }
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetScriptDispatch
//---------------------------------------------------------------------------
iTJSDispatch2 *TVPGetScriptDispatch() {
    if(TVPScriptEngine)
        return TVPScriptEngine->GetGlobal();
    return nullptr;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteScript
//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr &content, tTJSVariant *result) {
    if(TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result);
    else
        TVPThrowInternalError;
}

//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr &content, const ttstr &name, tjs_int lineofs,
                      tTJSVariant *result) {
    if(TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, nullptr, &name, lineofs);
    else
        TVPThrowInternalError;
}

//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr &content, iTJSDispatch2 *context,
                      tTJSVariant *result) {
    if(TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, context);
    else
        TVPThrowInternalError;
}

//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr &content, const ttstr &name, tjs_int lineofs,
                      iTJSDispatch2 *context, tTJSVariant *result) {
    if(TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, context, &name, lineofs);
    else
        TVPThrowInternalError;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteExpression
//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr &content, tTJSVariant *result) {
    TVPExecuteExpression(content, nullptr, result);
}

//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr &content, const ttstr &name,
                          tjs_int lineofs, tTJSVariant *result) {
    TVPExecuteExpression(content, name, lineofs, nullptr, result);
}

//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr &content, iTJSDispatch2 *context,
                          tTJSVariant *result) {
    if(TVPScriptEngine) {
        iTJSConsoleOutput *output = TVPScriptEngine->GetConsoleOutput();
        TVPScriptEngine->SetConsoleOutput(
            nullptr); // once set TJS console to nullptr
        try {
            TVPScriptEngine->EvalExpression(content, result, context);
        } catch(...) {
            TVPScriptEngine->SetConsoleOutput(output);
            throw;
        }
        TVPScriptEngine->SetConsoleOutput(output);
    } else {
        TVPThrowInternalError;
    }
}

//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr &content, const ttstr &name,
                          tjs_int lineofs, iTJSDispatch2 *context,
                          tTJSVariant *result) {
    if(TVPScriptEngine) {
        iTJSConsoleOutput *output = TVPScriptEngine->GetConsoleOutput();
        TVPScriptEngine->SetConsoleOutput(
            nullptr); // once set TJS console to nullptr
        try {
            TVPScriptEngine->EvalExpression(content, result, context, &name,
                                            lineofs);
        } catch(...) {
            TVPScriptEngine->SetConsoleOutput(output);
            throw;
        }
        TVPScriptEngine->SetConsoleOutput(output);
    } else {
        TVPThrowInternalError;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteBytecode
//---------------------------------------------------------------------------
void TVPExecuteBytecode(const tjs_uint8 *content, size_t len,
                        iTJSDispatch2 *context, tTJSVariant *result,
                        const tjs_char *name) {
    if(!TVPScriptEngine)
        TVPThrowInternalError;

    TVPScriptEngine->LoadByteCode(content, len, result, context, name);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPExecuteStorage(const ttstr &name, tTJSVariant *result,
                       bool isexpression, const tjs_char *modestr) {
    TVPExecuteStorage(name, nullptr, result, isexpression, modestr);
}
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <lz4.h>
#include <tjsBinarySerializer.h>
#include <tjsByteCodeLoader.h>
#include <vector>

static bool TVPExecuteStorageWithAfterInitCompatibility(const ttstr &name,
                                                        iTJSDispatch2 *context,
                                                        tTJSVariant *result,
                                                        bool isexpression,
                                                        const tjs_char *modestr);

class tTVPPbdByteChecker {
    int Flag{ 1 };
    tjs_uint8 Seed[3]{};

    static tjs_uint8 Round(tjs_uint8 seed[3]) {
        tjs_uint8 b = static_cast<tjs_uint8>(
            seed[0] ^ static_cast<tjs_uint8>(seed[0] * 2));
        const tjs_uint8 a = b;
        b >>= 2;
        b ^= seed[2];
        b >>= 3;
        b ^= seed[2];
        b ^= a;

        seed[0] = seed[1];
        seed[1] = seed[2];
        seed[2] = b;
        return b;
    }

    tjs_uint8 Update(tjs_uint8 code) {
        if(Flag == 0)
            return 0;
        if(code == 0)
            return Seed[2];
        return Round(Seed);
    }

public:
    explicit tTVPPbdByteChecker(tjs_uint32 seed) {
        Seed[0] = static_cast<tjs_uint8>((seed >> 24) ^ seed);
        Seed[1] = static_cast<tjs_uint8>(seed >> 8);
        Seed[2] = static_cast<tjs_uint8>(seed >> 16);
    }

    bool CheckByte(tjs_uint8 code, tjs_uint8 expected) {
        const tjs_uint8 actual = Update(code);
        if(Flag == 0 || expected == actual)
            return true;
        Flag = -1;
        return false;
    }

    bool CheckFinal(tjs_uint32 expected) {
        Round(Seed);
        Round(Seed);
        Round(Seed);

        tjs_uint32 actual = 0;
        actual |= static_cast<tjs_uint32>(Seed[2]);
        actual |= static_cast<tjs_uint32>(Seed[1]) << 8;
        actual |= static_cast<tjs_uint32>(Seed[0]) << 16;
        return Flag >= 0 && expected == actual;
    }
};

class tTVPPbdVariantReader {
    const tjs_uint8 *Data{ nullptr };
    size_t Size{ 0 };
    size_t Pos{ 0 };
    bool BigEndian{ false };
    bool CheckBytesOk{ true };
    tTVPPbdByteChecker Checker;

    bool Require(size_t count) const {
        return Pos <= Size && count <= Size - Pos;
    }

    bool ReadU8(tjs_uint8 &value) {
        if(!Require(1))
            return false;
        value = Data[Pos++];
        return true;
    }

    bool ReadU16(tjs_uint16 &value) {
        if(!Require(2))
            return false;
        if(BigEndian) {
            value = (static_cast<tjs_uint16>(Data[Pos]) << 8) |
                    static_cast<tjs_uint16>(Data[Pos + 1]);
        } else {
            value = static_cast<tjs_uint16>(Data[Pos]) |
                    (static_cast<tjs_uint16>(Data[Pos + 1]) << 8);
        }
        Pos += 2;
        return true;
    }

    bool ReadU32(tjs_uint32 &value) {
        if(!Require(4))
            return false;
        if(BigEndian) {
            value = (static_cast<tjs_uint32>(Data[Pos]) << 24) |
                    (static_cast<tjs_uint32>(Data[Pos + 1]) << 16) |
                    (static_cast<tjs_uint32>(Data[Pos + 2]) << 8) |
                    static_cast<tjs_uint32>(Data[Pos + 3]);
        } else {
            value = static_cast<tjs_uint32>(Data[Pos]) |
                    (static_cast<tjs_uint32>(Data[Pos + 1]) << 8) |
                    (static_cast<tjs_uint32>(Data[Pos + 2]) << 16) |
                    (static_cast<tjs_uint32>(Data[Pos + 3]) << 24);
        }
        Pos += 4;
        return true;
    }

    bool ReadU64(tjs_uint64 &value) {
        tjs_uint32 lo = 0;
        tjs_uint32 hi = 0;
        if(BigEndian) {
            if(!ReadU32(hi) || !ReadU32(lo))
                return false;
        } else {
            if(!ReadU32(lo) || !ReadU32(hi))
                return false;
        }
        value = static_cast<tjs_uint64>(lo) |
                (static_cast<tjs_uint64>(hi) << 32);
        return true;
    }

    bool ReadType(tjs_uint8 &type) {
        tjs_uint8 check = 0;
        if(!ReadU8(type) || !ReadU8(check))
            return false;
        if(!Checker.CheckByte(type, check))
            CheckBytesOk = false;
        return true;
    }

    bool ReadStringValue(ttstr &value) {
        tjs_uint32 length = 0;
        if(!ReadU32(length))
            return false;
        if(length > (std::numeric_limits<size_t>::max() / sizeof(tjs_char)) ||
           !Require(static_cast<size_t>(length) * sizeof(tjs_char))) {
            return false;
        }

        std::basic_string<tjs_char> chars;
        chars.reserve(length);
        for(tjs_uint32 i = 0; i < length; ++i) {
            tjs_uint16 ch = 0;
            if(!ReadU16(ch))
                return false;
            chars.push_back(static_cast<tjs_char>(ch));
        }
        value = ttstr(chars.c_str(), chars.size());
        return true;
    }

    bool ReadArrayValue(tTJSVariant &out) {
        tjs_uint32 count = 0;
        if(!ReadU32(count) || count > static_cast<tjs_uint32>(
                                std::numeric_limits<tjs_int>::max())) {
            return false;
        }

        iTJSDispatch2 *array = TJSCreateArrayObject();
        try {
            for(tjs_uint32 i = 0; i < count; ++i) {
                tTJSVariant item;
                if(!ReadVariant(item)) {
                    array->Release();
                    return false;
                }
                const tjs_error hr = array->PropSetByNum(
                    TJS_MEMBERENSURE, static_cast<tjs_int>(i), &item, array);
                if(TJS_FAILED(hr)) {
                    array->Release();
                    return false;
                }
            }
            out = tTJSVariant(array, array);
            array->Release();
            return true;
        } catch(...) {
            array->Release();
            throw;
        }
    }

    bool ReadDictionaryValue(tTJSVariant &out) {
        tjs_uint32 count = 0;
        if(!ReadU32(count))
            return false;

        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        try {
            for(tjs_uint32 i = 0; i < count; ++i) {
                ttstr key;
                tTJSVariant item;
                if(!ReadStringValue(key) || !ReadVariant(item)) {
                    dict->Release();
                    return false;
                }
                const tjs_error hr =
                    dict->PropSet(TJS_MEMBERENSURE, key.c_str(), nullptr,
                                  &item, dict);
                if(TJS_FAILED(hr)) {
                    dict->Release();
                    return false;
                }
            }
            out = tTJSVariant(dict, dict);
            dict->Release();
            return true;
        } catch(...) {
            dict->Release();
            throw;
        }
    }

public:
    tTVPPbdVariantReader(const tjs_uint8 *data, size_t size, bool bigEndian,
                         tjs_uint32 seed) :
        Data(data),
        Size(size), BigEndian(bigEndian), Checker(seed) {}

    bool ReadVariant(tTJSVariant &out) {
        tjs_uint8 type = 0;
        if(!ReadType(type))
            return false;

        switch(type) {
            case 0x00:
                out.Clear();
                return true;
            case 0x01:
                out = tTJSVariant((iTJSDispatch2 *)nullptr);
                return true;
            case 0x02: {
                ttstr value;
                if(!ReadStringValue(value))
                    return false;
                out = tTJSVariant(value);
                return true;
            }
            case 0x03: {
                tjs_uint32 length = 0;
                if(!ReadU32(length) || !Require(length))
                    return false;
                out = length != 0 ? tTJSVariant(Data + Pos, length)
                                  : tTJSVariant((const tjs_uint8 *)nullptr, 0);
                Pos += length;
                return true;
            }
            case 0x04: {
                tjs_uint64 raw = 0;
                if(!ReadU64(raw))
                    return false;
                out = tTJSVariant(static_cast<tjs_int64>(raw));
                return true;
            }
            case 0x05: {
                tjs_uint64 raw = 0;
                tjs_real value = 0;
                if(!ReadU64(raw))
                    return false;
                std::memcpy(&value, &raw, sizeof(value));
                out = tTJSVariant(value);
                return true;
            }
            case 0x81:
                return ReadArrayValue(out);
            case 0xC1:
                return ReadDictionaryValue(out);
            default:
                return false;
        }
    }

    bool FinishAndCheck() {
        tjs_uint32 expected = 0;
        if(!ReadU32(expected))
            return false;
        return Checker.CheckFinal(expected) && CheckBytesOk;
    }
};

static tjs_uint16 TVPReadPbdU16(const std::vector<tjs_uint8> &data, size_t pos,
                                bool bigEndian) {
    if(bigEndian) {
        return (static_cast<tjs_uint16>(data[pos]) << 8) |
               static_cast<tjs_uint16>(data[pos + 1]);
    }
    return static_cast<tjs_uint16>(data[pos]) |
           (static_cast<tjs_uint16>(data[pos + 1]) << 8);
}

static tjs_uint32 TVPReadPbdU32(const std::vector<tjs_uint8> &data, size_t pos,
                                bool bigEndian) {
    if(bigEndian) {
        return (static_cast<tjs_uint32>(data[pos]) << 24) |
               (static_cast<tjs_uint32>(data[pos + 1]) << 16) |
               (static_cast<tjs_uint32>(data[pos + 2]) << 8) |
               static_cast<tjs_uint32>(data[pos + 3]);
    }
    return static_cast<tjs_uint32>(data[pos]) |
           (static_cast<tjs_uint32>(data[pos + 1]) << 8) |
           (static_cast<tjs_uint32>(data[pos + 2]) << 16) |
           (static_cast<tjs_uint32>(data[pos + 3]) << 24);
}

static tjs_uint16 TVPReadLE16(const tjs_uint8 *data) {
    return static_cast<tjs_uint16>(data[0]) |
           (static_cast<tjs_uint16>(data[1]) << 8);
}

static tjs_uint32 TVPReadLE32(const tjs_uint8 *data) {
    return static_cast<tjs_uint32>(data[0]) |
           (static_cast<tjs_uint32>(data[1]) << 8) |
           (static_cast<tjs_uint32>(data[2]) << 16) |
           (static_cast<tjs_uint32>(data[3]) << 24);
}

static void TVPWriteLE32(tjs_uint8 *data, tjs_uint32 value) {
    data[0] = static_cast<tjs_uint8>(value);
    data[1] = static_cast<tjs_uint8>(value >> 8);
    data[2] = static_cast<tjs_uint8>(value >> 16);
    data[3] = static_cast<tjs_uint8>(value >> 24);
}

static tjs_uint32 TVPRotl32(tjs_uint32 value, int shift) {
    return (value << shift) | (value >> (32 - shift));
}

static void TVPBlake2sCompress(tjs_uint32 h[8], const tjs_uint8 block[64],
                               tjs_uint64 counter, bool last) {
    static const tjs_uint32 iv[8] = {
        0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
        0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
    };
    static const tjs_uint8 sigma[10][16] = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
        { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
        { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
        { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
        { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
        { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
        { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
        { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
        { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
        { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 }
    };

    tjs_uint32 m[16];
    for(int i = 0; i < 16; ++i)
        m[i] = TVPReadLE32(block + i * 4);

    tjs_uint32 v[16];
    for(int i = 0; i < 8; ++i) {
        v[i] = h[i];
        v[i + 8] = iv[i];
    }
    v[12] ^= static_cast<tjs_uint32>(counter);
    v[13] ^= static_cast<tjs_uint32>(counter >> 32);
    if(last) {
        v[14] = ~v[14];
    }

#define TVP_BLAKE2S_G(a, b, c, d, x, y) \
    do { \
        v[(a)] = v[(a)] + v[(b)] + (x); \
        v[(d)] = TVPRotl32(v[(d)] ^ v[(a)], 16); \
        v[(c)] = v[(c)] + v[(d)]; \
        v[(b)] = TVPRotl32(v[(b)] ^ v[(c)], 20); \
        v[(a)] = v[(a)] + v[(b)] + (y); \
        v[(d)] = TVPRotl32(v[(d)] ^ v[(a)], 24); \
        v[(c)] = v[(c)] + v[(d)]; \
        v[(b)] = TVPRotl32(v[(b)] ^ v[(c)], 25); \
    } while(false)

    for(int round = 0; round < 10; ++round) {
        const tjs_uint8 *s = sigma[round];
        TVP_BLAKE2S_G(0, 4, 8, 12, m[s[0]], m[s[1]]);
        TVP_BLAKE2S_G(1, 5, 9, 13, m[s[2]], m[s[3]]);
        TVP_BLAKE2S_G(2, 6, 10, 14, m[s[4]], m[s[5]]);
        TVP_BLAKE2S_G(3, 7, 11, 15, m[s[6]], m[s[7]]);
        TVP_BLAKE2S_G(0, 5, 10, 15, m[s[8]], m[s[9]]);
        TVP_BLAKE2S_G(1, 6, 11, 12, m[s[10]], m[s[11]]);
        TVP_BLAKE2S_G(2, 7, 8, 13, m[s[12]], m[s[13]]);
        TVP_BLAKE2S_G(3, 4, 9, 14, m[s[14]], m[s[15]]);
    }

#undef TVP_BLAKE2S_G

    for(int i = 0; i < 8; ++i)
        h[i] ^= v[i] ^ v[i + 8];
}

static void TVPBlake2sKeyed32(const tjs_uint8 *data, size_t size,
                              const tjs_uint8 *key, size_t keySize,
                              tjs_uint8 out[32]) {
    static const tjs_uint32 iv[8] = {
        0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
        0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
    };

    tjs_uint32 h[8];
    for(int i = 0; i < 8; ++i)
        h[i] = iv[i];
    h[0] ^= 0x01010000u ^ (static_cast<tjs_uint32>(keySize) << 8) ^ 32u;

    std::array<tjs_uint8, 64> buffer{};
    size_t buffered = 0;
    tjs_uint64 counter = 0;

    auto update = [&](const tjs_uint8 *src, size_t len) {
        if(len == 0)
            return;

        if(buffered + len > buffer.size()) {
            const size_t fill = buffer.size() - buffered;
            if(fill != 0) {
                std::memcpy(buffer.data() + buffered, src, fill);
                src += fill;
                len -= fill;
            }
            counter += buffer.size();
            TVPBlake2sCompress(h, buffer.data(), counter, false);
            buffered = 0;
        }

        while(len > buffer.size()) {
            counter += buffer.size();
            TVPBlake2sCompress(h, src, counter, false);
            src += buffer.size();
            len -= buffer.size();
        }

        if(len != 0) {
            std::memcpy(buffer.data() + buffered, src, len);
            buffered += len;
        }
    };

    if(keySize != 0) {
        buffer.fill(0);
        std::memcpy(buffer.data(), key, std::min<size_t>(keySize, buffer.size()));
        buffered = buffer.size();
    }

    update(data, size);

    std::fill(buffer.begin() + static_cast<std::ptrdiff_t>(buffered),
              buffer.end(), 0);
    counter += buffered;
    TVPBlake2sCompress(h, buffer.data(), counter, true);

    for(int i = 0; i < 8; ++i)
        TVPWriteLE32(out + i * 4, h[i]);
}

static tjs_uint32 TVPXXHash32(const tjs_uint8 *data, size_t size,
                              tjs_uint32 seed) {
    constexpr tjs_uint32 prime1 = 0x9E3779B1u;
    constexpr tjs_uint32 prime2 = 0x85EBCA77u;
    constexpr tjs_uint32 prime3 = 0xC2B2AE3Du;
    constexpr tjs_uint32 prime4 = 0x27D4EB2Fu;
    constexpr tjs_uint32 prime5 = 0x165667B1u;

    const tjs_uint8 *p = data;
    const tjs_uint8 *end = data + size;
    tjs_uint32 h = 0;

    if(size >= 16) {
        const tjs_uint8 *limit = end - 16;
        tjs_uint32 v1 = seed + prime1 + prime2;
        tjs_uint32 v2 = seed + prime2;
        tjs_uint32 v3 = seed;
        tjs_uint32 v4 = seed - prime1;
        do {
            v1 = TVPRotl32(v1 + TVPReadLE32(p) * prime2, 13) * prime1;
            p += 4;
            v2 = TVPRotl32(v2 + TVPReadLE32(p) * prime2, 13) * prime1;
            p += 4;
            v3 = TVPRotl32(v3 + TVPReadLE32(p) * prime2, 13) * prime1;
            p += 4;
            v4 = TVPRotl32(v4 + TVPReadLE32(p) * prime2, 13) * prime1;
            p += 4;
        } while(p <= limit);
        h = TVPRotl32(v1, 1) + TVPRotl32(v2, 7) +
            TVPRotl32(v3, 12) + TVPRotl32(v4, 18);
    } else {
        h = seed + prime5;
    }

    h += static_cast<tjs_uint32>(size);
    while(p + 4 <= end) {
        h = TVPRotl32(h + TVPReadLE32(p) * prime3, 17) * prime4;
        p += 4;
    }
    while(p < end) {
        h = TVPRotl32(h + static_cast<tjs_uint32>(*p) * prime5, 11) * prime1;
        ++p;
    }

    h ^= h >> 15;
    h *= prime2;
    h ^= h >> 13;
    h *= prime3;
    h ^= h >> 16;
    return h;
}

static void TVPChaChaBlock(const tjs_uint8 key[32], tjs_uint64 nonce,
                           tjs_uint64 counter, int rounds,
                           tjs_uint8 out[64]) {
    static const tjs_uint8 sigma[16] = {
        'e', 'x', 'p', 'a', 'n', 'd', ' ', '3',
        '2', '-', 'b', 'y', 't', 'e', ' ', 'k'
    };

    tjs_uint32 state[16];
    for(int i = 0; i < 4; ++i)
        state[i] = TVPReadLE32(sigma + i * 4);
    for(int i = 0; i < 8; ++i)
        state[i + 4] = TVPReadLE32(key + i * 4);
    state[12] = static_cast<tjs_uint32>(counter);
    state[13] = static_cast<tjs_uint32>(counter >> 32);
    state[14] = static_cast<tjs_uint32>(nonce);
    state[15] = static_cast<tjs_uint32>(nonce >> 32);

    tjs_uint32 z[16];
    std::memcpy(z, state, sizeof(z));

#define TVP_CHACHA_QR(a, b, c, d) \
    do { \
        z[(a)] += z[(b)]; z[(d)] = TVPRotl32(z[(d)] ^ z[(a)], 16); \
        z[(c)] += z[(d)]; z[(b)] = TVPRotl32(z[(b)] ^ z[(c)], 12); \
        z[(a)] += z[(b)]; z[(d)] = TVPRotl32(z[(d)] ^ z[(a)], 8); \
        z[(c)] += z[(d)]; z[(b)] = TVPRotl32(z[(b)] ^ z[(c)], 7); \
    } while(false)

    for(int i = 0; i < rounds; i += 2) {
        TVP_CHACHA_QR(0, 4, 8, 12);
        TVP_CHACHA_QR(1, 5, 9, 13);
        TVP_CHACHA_QR(2, 6, 10, 14);
        TVP_CHACHA_QR(3, 7, 11, 15);
        TVP_CHACHA_QR(0, 5, 10, 15);
        TVP_CHACHA_QR(1, 6, 11, 12);
        TVP_CHACHA_QR(2, 7, 8, 13);
        TVP_CHACHA_QR(3, 4, 9, 14);
    }

#undef TVP_CHACHA_QR

    for(int i = 0; i < 16; ++i)
        TVPWriteLE32(out + i * 4, z[i] + state[i]);
}

static bool TVPGetPbdCryptoParams(tjs_uint16 cryptoMode, int &rounds,
                                  int &tableCount) {
    switch(cryptoMode) {
        case 1:
            rounds = 8;
            tableCount = 16;
            return true;
        case 2:
            rounds = 12;
            tableCount = 8;
            return true;
        case 3:
            rounds = 20;
            tableCount = 4;
            return true;
        case 4:
            rounds = 8;
            tableCount = 1;
            return true;
        case 5:
            rounds = 12;
            tableCount = 1;
            return true;
        case 6:
            rounds = 20;
            tableCount = 1;
            return true;
        default:
            return false;
    }
}

static bool TVPDecryptPbdPayload(const ttstr &place,
                                 std::vector<tjs_uint8> &payload,
                                 tjs_uint16 cryptoMode, tjs_uint32 seed,
                                 const tjs_uint8 *iv, size_t ivSize) {
    if(cryptoMode == 0 || payload.empty())
        return true;

    int rounds = 0;
    int tableCount = 0;
    if(!TVPGetPbdCryptoParams(cryptoMode, rounds, tableCount)) {
        KR2_SCR_LOG("[res] PBD unsupported crypto '%s' crypto=%d",
                    place.AsStdString().c_str(), (int)cryptoMode);
        return false;
    }

    tjs_uint8 seedKey[4];
    TVPWriteLE32(seedKey, seed);

    tjs_uint8 key[32];
    TVPBlake2sKeyed32(iv, ivSize, seedKey, sizeof(seedKey), key);

    const tjs_uint32 nonceLow = TVPXXHash32(iv, ivSize, seed);
    tjs_uint32 tableSeed = 0xFFFFFFFFu;
    if(tableCount > 1) {
        if(nonceLow != seed)
            tableSeed = nonceLow ^ seed;
        else if(seed != 0)
            tableSeed = seed;
    }
    const tjs_uint64 nonce =
        (static_cast<tjs_uint64>(seed) << 32) | nonceLow;

    const size_t tableBytes = static_cast<size_t>(tableCount) * 64u;
    std::vector<tjs_uint8> table(tableBytes);
    tjs_uint64 counter = 0;
    size_t tablePos = tableBytes;

    for(size_t i = 0; i < payload.size(); ++i) {
        if(tablePos >= tableBytes) {
            TVPChaChaBlock(key, nonce, counter++, rounds, table.data());

            if(tableCount > 1) {
                const size_t wordCount = tableBytes / sizeof(tjs_uint32);
                for(size_t word = 16; word < wordCount; ++word) {
                    tjs_uint32 s =
                        TVPReadLE32(table.data() + (word - 16) * 4);
                    const tjs_uint32 mixed = ((s << 13) ^ s);
                    s = (mixed >> 17) ^ mixed;
                    s = (32u * s) ^ s;
                    if(s == 0)
                        s = tableSeed;
                    TVPWriteLE32(table.data() + word * 4, s);
                }
            }

            tablePos = 0;
        }

        payload[i] ^= table[tablePos++];
    }

    return true;
}

static bool TVPDecompressPbdLz4Blocks(const ttstr &place,
                                      const std::vector<tjs_uint8> &input,
                                      std::vector<tjs_uint8> &output) {
    constexpr int blockLimit = 0x100000;
    std::vector<tjs_uint8> decodeBuffer(blockLimit);
    std::vector<tjs_uint8> dictBuffer(blockLimit);

    size_t pos = 0;
    int dictSize = 0;
    output.clear();

    while(pos < input.size()) {
        if(input.size() - pos < 2)
            return false;

        const tjs_uint16 encodedSize = TVPReadLE16(input.data() + pos);
        pos += 2;
        if(encodedSize == 0 || encodedSize > input.size() - pos)
            return false;

        const int decodedSize = LZ4_decompress_safe_usingDict(
            reinterpret_cast<const char *>(input.data() + pos),
            reinterpret_cast<char *>(decodeBuffer.data()), encodedSize,
            blockLimit, reinterpret_cast<const char *>(dictBuffer.data()),
            dictSize);
        if(decodedSize < 0) {
            KR2_SCR_LOG("[res] PBD LZ4 block decode failed '%s' block=%u",
                        place.AsStdString().c_str(), (unsigned)encodedSize);
            return false;
        }

        output.insert(output.end(), decodeBuffer.begin(),
                      decodeBuffer.begin() + decodedSize);
        std::memcpy(dictBuffer.data(), decodeBuffer.data(), decodedSize);
        dictSize = decodedSize;
        pos += encodedSize;
    }

    return !output.empty();
}

static bool TVPDecompressPbdLz4Sized(const ttstr &place,
                                     const std::vector<tjs_uint8> &input,
                                     bool bigEndian,
                                     std::vector<tjs_uint8> &output) {
    if(input.size() < 4)
        return false;

    const tjs_uint32 outputSize = bigEndian
                                      ? ((static_cast<tjs_uint32>(input[0])
                                          << 24) |
                                         (static_cast<tjs_uint32>(input[1])
                                          << 16) |
                                         (static_cast<tjs_uint32>(input[2])
                                          << 8) |
                                         static_cast<tjs_uint32>(input[3]))
                                      : TVPReadLE32(input.data());
    if(outputSize == 0 ||
       outputSize > static_cast<tjs_uint32>(256u * 1024u * 1024u)) {
        return false;
    }

    output.resize(outputSize);
    const int decoded = LZ4_decompress_safe(
        reinterpret_cast<const char *>(input.data() + 4),
        reinterpret_cast<char *>(output.data()), static_cast<int>(input.size() - 4),
        static_cast<int>(output.size()));
    if(decoded != static_cast<int>(output.size())) {
        KR2_SCR_LOG("[res] PBD LZ4 sized decode failed '%s' decoded=%d size=%u",
                    place.AsStdString().c_str(), decoded, outputSize);
        output.clear();
        return false;
    }
    return true;
}

static bool TVPBuildPbdPayload(const ttstr &place,
                               const std::vector<tjs_uint8> &data,
                               size_t payloadOffset, bool bigEndian,
                               tjs_uint8 compressMode,
                               tjs_uint16 cryptoMode, tjs_uint32 seed,
                               const tjs_uint8 *iv, size_t ivSize,
                               std::vector<tjs_uint8> &payload) {
    if(payloadOffset >= data.size())
        return false;

    std::vector<tjs_uint8> raw(data.begin() + payloadOffset, data.end());
    if(!TVPDecryptPbdPayload(place, raw, cryptoMode, seed, iv, ivSize))
        return false;

    if(compressMode == 'n') {
        payload.swap(raw);
        return true;
    }

    if(compressMode == '4') {
        if(TVPDecompressPbdLz4Blocks(place, raw, payload))
            return true;
        return TVPDecompressPbdLz4Sized(place, raw, bigEndian, payload);
    }

    KR2_SCR_LOG("[res] PBD unsupported compression '%s' comp=%d",
                place.AsStdString().c_str(), (int)compressMode);
    return false;
}

bool TVPTryLoadPbdTJSVariant(const ttstr &place,
                             const tjs_char *modestr,
                             tTJSVariant *result,
                             const tjs_uint8 *outerIV,
                             size_t outerIVSize) {
    if(result == nullptr)
        return false;

    std::unique_ptr<tTJSBinaryStream> stream{
        TVPCreateBinaryStreamForRead(place, modestr)
    };
    if(!stream)
        return false;

    const tjs_uint64 streamSize64 = stream->GetSize();
    if(streamSize64 < 20 ||
       streamSize64 > static_cast<tjs_uint64>(
                          std::numeric_limits<size_t>::max())) {
        return false;
    }

    std::vector<tjs_uint8> data(static_cast<size_t>(streamSize64));
    if(stream->Read(data.data(), static_cast<tjs_uint>(data.size())) !=
       data.size()) {
        return false;
    }

    if(data.size() >= tTJSBinarySerializer::HEADER_LENGTH &&
       tTJSBinarySerializer::IsBinary(data.data())) {
        stream->SetPosition(0);
        if(tTJS::LoadBinaryDictionayArray(stream.get(), result)) {
            KR2_SCR_LOG("[res] KBAD PBD loaded '%s'",
                        place.AsStdString().c_str());
            return true;
        }
        KR2_SCR_LOG("[res] KBAD PBD decode failed '%s'",
                    place.AsStdString().c_str());
        return false;
    }

    const bool littlePbd = data[0] == 'T' && data[1] == 'J' &&
                           data[2] == 'S' && data[3] == '/';
    const bool bigPbd = data[0] == 'T' && data[1] == 'J' &&
                        data[2] == 'S' && data[3] == '\\';
    if((!littlePbd && !bigPbd) || data[5] != 's' || data[6] != '0' ||
       data[7] != 0) {
        return false;
    }

    const bool bigEndian = bigPbd;
    const tjs_uint8 compressMode = data[4];
    const tjs_uint32 seed = TVPReadPbdU32(data, 8, bigEndian);
    const tjs_uint16 cryptoMode = TVPReadPbdU16(data, 12, bigEndian);
    const tjs_uint16 ivLength = TVPReadPbdU16(data, 14, bigEndian);
    const size_t payloadOffset = 16u + ivLength;

    if(payloadOffset >= data.size())
        return false;

    const tjs_uint8 *iv = data.data() + 16u;
    size_t effectiveIVLength = ivLength;
    if(effectiveIVLength == 0 && outerIV != nullptr && outerIVSize != 0) {
        iv = outerIV;
        effectiveIVLength = outerIVSize;
    }

    if(cryptoMode != 0) {
        KR2_SCR_LOG("[res] PBD crypto flag '%s' comp=%d crypto=%d iv=%u "
                    "outeriv=%u; trying payload decode",
                    place.AsStdString().c_str(), (int)compressMode,
                    (int)cryptoMode, (unsigned)ivLength,
                    (unsigned)(ivLength == 0 ? effectiveIVLength : 0));
    }

    std::vector<tjs_uint8> payload;
    if(!TVPBuildPbdPayload(place, data, payloadOffset, bigEndian, compressMode,
                           cryptoMode, seed, iv, effectiveIVLength, payload)) {
        return false;
    }

    tTVPPbdVariantReader reader(payload.data(), payload.size(), bigEndian, seed);
    tTJSVariant value;
    if(!reader.ReadVariant(value)) {
        KR2_SCR_LOG("[res] PBD decode failed '%s'",
                    place.AsStdString().c_str());
        return false;
    }

    if(!reader.FinishAndCheck()) {
        KR2_SCR_LOG("[res] PBD checksum mismatch '%s'",
                    place.AsStdString().c_str());
    }

    *result = value;
    KR2_SCR_LOG("[res] PBD loaded '%s'", place.AsStdString().c_str());
    return true;
}

//---------------------------------------------------------------------------
void TVPExecuteStorage(const ttstr &name, iTJSDispatch2 *context,
                       tTJSVariant *result, bool isexpression,
                       const tjs_char *modestr) {
    // execute storage which contains script
    if(!TVPScriptEngine)
        TVPThrowInternalError;

#if defined(__ANDROID__)
    // Resource probe: every script TVPExecuteStorage call gets logged so
    // we can see the order in which initialize.tjs / Initialize.tjs /
    // KAGWindow.tjs / MainWindow.tjs ... are pulled.
    {
        static int s_execCount = 0;
        ++s_execCount;
        if(s_execCount <= 96 || (s_execCount & 0x3F) == 0) {
            ttstr msg = TJS_W("[res] EXEC_SCRIPT #") +
                ttstr((tjs_int)s_execCount) +
                TJS_W(" '") + name + TJS_W("' ctx=") +
                ttstr((tjs_int)(tTVInteger)(intptr_t)context) +
                TJS_W(" expr=") + ttstr(isexpression ? 1 : 0);
            KR2_SCR_LOG("%s", msg.AsStdString().c_str());
        }
    }
#endif

    if(TVPExecuteStorageWithAfterInitCompatibility(name, context, result,
                                                   isexpression, modestr)) {
        return;
    }

    if(isexpression) {
        ttstr place(TVPSearchPlacedPath(name));
        if(TVPTryLoadPbdTJSVariant(place, modestr, result))
            return;
    }

    { // for bytecode
        ttstr place(TVPSearchPlacedPath(name));
        ttstr shortname(TVPExtractStorageName(place));
        std::unique_ptr<tTJSBinaryStream> stream{ TVPCreateBinaryStreamForRead(
            place, modestr) };
        if(stream) {
            bool isbytecode = TVPScriptEngine->LoadByteCode(
                stream.get(), result, context, shortname.c_str());

            if(isbytecode) {
#if defined(__ANDROID__)
                KR2_SCR_LOG("[res] EXEC_SCRIPT_DONE '%s'",
                            name.AsStdString().c_str());
#endif
                // save extract binary file for debug!
                //                auto loader =
                //                std::make_unique<tTJSByteCodeLoader>(); auto
                //                *buff =
                //                    new tjs_uint8[static_cast<unsigned
                //                    int>(stream->GetSize())];
                //                stream->Read(buff,
                //                static_cast<tjs_uint>(stream->GetSize()));
                //
                //                std::unique_ptr<tTJSScriptBlock,
                //                                std::function<void(tTJSScriptBlock
                //                                *)>>
                //                    blk{ loader->ReadByteCode(TVPScriptEngine,
                //                    name.c_str(),
                //                                              buff,
                //                                              stream->GetSize()),
                //                         [](auto *ptr) { ptr->Release(); } };
                //                delete[] buff;
                //                if(!blk)
                //                    return;
                //                auto tmpPlace = place.AsStdString();
                //                tmpPlace.replace(tmpPlace.find(".xp3>"),
                //                std::strlen(".xp3>"),
                //                                 "_xp3/");
                //                std::filesystem::path absoluteScriptPath{
                //                tmpPlace.substr(
                //                    std::strlen("file://.")) };
                //                std::filesystem::create_directories(
                //                    absoluteScriptPath.parent_path());
                //                auto memoryStream =
                //                std::make_unique<tTVPMemoryStream>();
                //                blk->Dump(memoryStream.get());
                //
                //                std::vector<char16_t>
                //                buffer(memoryStream->GetSize() /
                //                                             sizeof(char16_t));
                //
                //                memoryStream->Seek(0, TJS_BS_SEEK_SET);
                //                memoryStream->Read(buffer.data(),
                //                memoryStream->GetSize()); FILE *f =
                //                fopen(absoluteScriptPath.c_str(), "wb");
                //                // 写入 UTF-16 LE BOM 小端
                //                char16_t bom = 0xFEFF;
                //                fwrite(&bom, sizeof(char16_t), 1, f);
                //
                //                fwrite(buffer.data(), sizeof(char16_t),
                //                buffer.size(), f); fclose(f);
                // end
                return;
            }
        }
    }

    ttstr place(TVPSearchPlacedPath(name));
    ttstr shortname(TVPExtractStorageName(place));
    std::unique_ptr<iTJSTextReadStream> stream{ TVPCreateTextStreamForRead(
        place, modestr) };
    ttstr buffer;
    stream->Read(buffer, 0);

    // save extract script file for debug!
    //    auto tmpPlace = place.AsStdString();
    //    auto i = tmpPlace.find(".xp3>");
    //    if(i > -1) {
    //        tmpPlace.replace(i, std::strlen(".xp3>"), "_xp3/");
    //        std::filesystem::path absoluteScriptPath{ tmpPlace.substr(
    //            std::strlen("file://.")) };
    //        std::filesystem::create_directories(absoluteScriptPath.parent_path());
    //        std::ofstream of{ absoluteScriptPath };
    //        of << buffer.AsStdString() << std::endl;
    //        of.close();
    //    }
    // end

    if(TVPScriptEngine) {

        if(!isexpression)
            TVPScriptEngine->ExecScript(buffer, result, context, &shortname);
        else
            TVPScriptEngine->EvalExpression(buffer, result, context,
                                            &shortname);
#if defined(__ANDROID__)
        KR2_SCR_LOG("[res] EXEC_SCRIPT_DONE '%s'",
                    name.AsStdString().c_str());
#endif
    }
}

//---------------------------------------------------------------------------
void TVPCompileStorage(const ttstr &name, bool isrequestresult,
                       bool outputdebug, bool isexpression,
                       const ttstr &outputpath) {
    // execute storage which contains script
    if(!TVPScriptEngine)
        TVPThrowInternalError;

    ttstr place(TVPSearchPlacedPath(name));
    ttstr shortname(TVPExtractStorageName(place));
    iTJSTextReadStream *stream = TVPCreateTextStreamForRead(place, TJS_W(""));

    ttstr buffer;
    try {
        stream->Read(buffer, 0);
    } catch(...) {
        stream->Destruct();
        throw;
    }
    stream->Destruct();

    tTJSBinaryStream *outputstream = TVPCreateStream(outputpath, TJS_BS_WRITE);
    if(TVPScriptEngine) {
        try {
            TVPScriptEngine->CompileScript(buffer.c_str(), outputstream,
                                           isrequestresult, outputdebug,
                                           isexpression, name.c_str(), 0);
        } catch(...) {
            delete outputstream;
            throw;
        }
    }
    delete outputstream;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateMessageMapFile
//---------------------------------------------------------------------------
void TVPCreateMessageMapFile(const ttstr &filename) {
#ifdef TJS_TEXT_OUT_CRLF
    ttstr script(TJS_W("{\r\n\tvar r = System.assignMessage;\r\n"));
#else
    ttstr script(TJS_W("{\n\tvar r = System.assignMessage;\n"));
#endif

    script += TJSCreateMessageMapString();

    script += TJS_W("}");

    iTJSTextWriteStream *stream =
        TVPCreateTextStreamForWrite(filename, TJS_W(""));
    try {
        stream->Write(script);
    } catch(...) {
        stream->Destruct();
        throw;
    }

    stream->Destruct();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPDumpScriptEngine
//---------------------------------------------------------------------------
void TVPDumpScriptEngine() {
    TVPTJS2StartDump();
    TVPScriptEngine->SetConsoleOutput(TVPGetTJS2DumpOutputGateway());
    try {
        TVPScriptEngine->Dump();
    } catch(...) {
        TVPTJS2EndDump();
        TVPScriptEngine->SetConsoleOutput(TVPGetTJS2ConsoleOutputGateway());
        throw;
    }
    TVPScriptEngine->SetConsoleOutput(TVPGetTJS2ConsoleOutputGateway());
    TVPTJS2EndDump();
}
//---------------------------------------------------------------------------

bool TVPStartupSuccess = false;
void TVPOpenPatchLibUrl();
static bool TVPExecuteStorageWithAfterInitCompatibility(const ttstr &name,
                                                        iTJSDispatch2 *context,
                                                        tTJSVariant *result,
                                                        bool isexpression,
                                                        const tjs_char *modestr) {
    auto sn = name.AsStdString();
    if(sn.find("AfterInit") == std::string::npos &&
       sn.find("afterinit") == std::string::npos) {
        return false;
    }

    static bool inAfterInit = false;
    if(inAfterInit)
        return false;

    inAfterInit = true;
    try {
        TVPExecuteStorage(name, context, result, isexpression, modestr);
    } catch(const eTJS &e) {
        TVPAddLog(ttstr(TJS_W("(warning) AfterInit exception caught: ")) +
                  e.GetMessage());
    } catch(const std::exception &e) {
        TVPAddLog(ttstr(TJS_W("(warning) AfterInit exception caught: ")) +
                  ttstr(e.what()));
    } catch(...) {
        TVPAddLog(TJS_W("(warning) AfterInit exception caught (unknown)"));
    }
    inAfterInit = false;
    return true;
}

// (forward declarations moved up near TVPInitScriptEngine)

//---------------------------------------------------------------------------
// TVPExecuteStartupScript
//---------------------------------------------------------------------------
void TVPExecuteStartupScript() {
    TVPEnsureKirikiroidCompatibilityPatch();
    TVPLoadXP3FilterScript(true);
    // patch.tjs 実行より前に走時補正を入れておく。これにより、
    // ゲーム自前 patch.tjs が voiceeffect.tjs などで失敗しても
    // 後続のフック (storeVoiceMap など) で Member not found を出さない。
    TVPRunRuntimeCompatibilityPatches();
    ttstr strPatchError;
    try {
        ttstr patch = TVPGetAppPath() + "patch.tjs";
        if(TVPIsExistentStorageNoSearch(patch)) {
            spdlog::info("[krkr] executing patch.tjs: {}", patch.AsStdString());
            TVPExecuteStorage(patch);
            spdlog::info("[krkr] patch.tjs executed OK");
        } else {
            spdlog::info("[krkr] no patch.tjs at: {}", patch.AsStdString());
        }
    } catch(const TJS::eTJSScriptError &e) {
        ttstr &msg = strPatchError;
        msg += e.GetMessage();
        const tjs_char *pszBlockName = e.GetBlockName();
        if(pszBlockName && *pszBlockName) {
            msg += TJS_W("\n@line(");
            tjs_char tmp[34];
            msg += TJS_int_to_str(e.GetSourceLine(), tmp);
            msg += TJS_W(") ");
            msg += pszBlockName;
        }
        msg += TJS_W("\n");
        msg += e.GetTrace();
    } catch(const TJS::eTJS &e) {
        if(!TVPSystemUninitCalled)
            strPatchError = e.GetMessage();
    } catch(const std::exception &e) {
        strPatchError = e.what();
    } catch(const char *e) {
        strPatchError = e;
    } catch(const tjs_char *e) {
        strPatchError = e;
    }

    if(!strPatchError.IsEmpty()) {
        spdlog::error("[krkr] patch.tjs failed: {}",
                      strPatchError.AsStdString());
        ttstr msg =
            LocaleConfigManager::GetInstance()->GetText("startup_patch_fail");
        msg += "\n";
        msg += strPatchError;
        std::vector<ttstr> btns;
        btns.emplace_back(
            LocaleConfigManager::GetInstance()->GetText("msgbox_ok"));
        btns.emplace_back(
            LocaleConfigManager::GetInstance()->GetText("browse_patch_lib"));
        if(TVPShowSimpleMessageBox(msg, TVPGetPackageVersionString(), btns) ==
           1) {
            TVPOpenPatchLibUrl();
        }
    }

    // execute "startup.tjs"
    try {

        ttstr place(TVPSearchPlacedPath(TVPStartupScriptName));
        spdlog::info("Loading startup script: {}", place.AsStdString());
        TVPStartupSuccess = false;
        try {
            iTJSTextReadStream *stream = TVPCreateTextStreamForRead(place, "");
            stream->Destruct();
            TVPExecuteStorage(TVPStartupScriptName);
            TVPStartupSuccess = true;
        } catch(...) {
            if(!TVPIsExistentStorage(TJS_W("system/Initialize.tjs"))) {
                throw;
            }
        }
        if(!TVPStartupSuccess) {
            // try direct execute initialize.tjs to compatible for
            // some patch
            TVPExecuteStorage(TJS_W("system/Initialize.tjs"));
            TVPStartupSuccess = true;
        }
        spdlog::info("Startup script ended.");
        // After startup.tjs, kag (KAGWindow) is fully constructed. Re-run
        // the voiceEffect compat to also attach the no-op stub onto the
        // live kag instance so chained accesses like kag.voiceEffectPlugin
        // .storeVoiceMap() survive missing wamsoft DLLs.
        try {
            TVPRegisterVoiceEffectStubs();
        } catch(...) {
            TVPAddLog(TJS_W("[krkr] post-startup voiceEffect stub re-run failed"));
        }
        try {
            ttstr patch = TVPGetAppPath() + "AfterStartup.tjs";
            if(TVPIsExistentStorageNoSearch(patch))
                TVPExecuteStorage(patch);
        } catch(...) {
        }
    }
    TJS_CONVERT_TO_TJS_EXCEPTION
    //}
    // TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION(TJS_W("startup"))
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// unhandled exception handler related
//---------------------------------------------------------------------------
static bool TJSGetSystem_exceptionHandler_Object(tTJSVariantClosure &dest) {
    // get System.exceptionHandler
    iTJSDispatch2 *global = TVPGetScriptEngine()->GetGlobalNoAddRef();
    if(!global)
        return false;

    tTJSVariant val;
    tTJSVariant val2;
    tTJSVariantClosure clo;

    tjs_error er;
    er = global->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("System"), nullptr, &val,
                         global);
    if(TJS_FAILED(er))
        return false;

    if(val.Type() != tvtObject)
        return false;

    clo = val.AsObjectClosureNoAddRef();

    if(clo.Object == nullptr)
        return false;

    clo.PropGet(TJS_MEMBERMUSTEXIST, TJS_W("exceptionHandler"), nullptr, &val2,
                nullptr);

    if(val2.Type() != tvtObject)
        return false;

    dest = val2.AsObjectClosure();

    if(!dest.Object) {
        dest.Release();
        return false;
    }

    return true;
}

//---------------------------------------------------------------------------
bool TVPProcessUnhandledException(eTJSScriptException &e) {
    bool result;
    tTJSVariantClosure clo;
    clo.Object = clo.ObjThis = nullptr;

    try {
        // get the script engine
        tTJS *engine = TVPGetScriptEngine();
        if(!engine)
            return false; // the script engine had been shutdown

        // get System.exceptionHandler
        if(!TJSGetSystem_exceptionHandler_Object(clo))
            return false; // System.exceptionHandler cannot be
                          // retrieved

        // execute clo
        tTJSVariant obj(e.GetValue());

        tTJSVariant *pval[] = { &obj };

        tTJSVariant res;

        clo.FuncCall(0, nullptr, nullptr, &res, 1, pval, nullptr);

        result = res.operator bool();
    } catch(eTJSScriptError &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(eTJS &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(...) {
        clo.Release();
        throw;
    }
    clo.Release();

    return result;
}

//---------------------------------------------------------------------------
bool TVPProcessUnhandledException(eTJSScriptError &e) {
    bool result;
    tTJSVariantClosure clo;
    clo.Object = clo.ObjThis = nullptr;

    try {
        // get the script engine
        tTJS *engine = TVPGetScriptEngine();
        if(!engine)
            return false; // the script engine had been shutdown

        // get System.exceptionHandler
        if(!TJSGetSystem_exceptionHandler_Object(clo))
            return false; // System.exceptionHandler cannot be
                          // retrieved

        // execute clo
        tTJSVariant obj;
        tTJSVariant msg(e.GetMessage());
        tTJSVariant trace(e.GetTrace());
        TJSGetExceptionObject(engine, &obj, msg, &trace);

        tTJSVariant *pval[] = { &obj };

        tTJSVariant res;

        clo.FuncCall(0, nullptr, nullptr, &res, 1, pval, nullptr);

        result = res.operator bool();
    } catch(eTJSScriptError &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(eTJS &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(...) {
        clo.Release();
        throw;
    }
    clo.Release();

    return result;
}

//---------------------------------------------------------------------------
bool TVPProcessUnhandledException(eTJS &e) {
    bool result;
    tTJSVariantClosure clo;
    clo.Object = clo.ObjThis = nullptr;

    try {
        // get the script engine
        tTJS *engine = TVPGetScriptEngine();
        if(!engine)
            return false; // the script engine had been shutdown

        // get System.exceptionHandler
        if(!TJSGetSystem_exceptionHandler_Object(clo))
            return false; // System.exceptionHandler cannot be
                          // retrieved

        // execute clo
        tTJSVariant obj;
        tTJSVariant msg(e.GetMessage());
        TJSGetExceptionObject(engine, &obj, msg);

        tTJSVariant *pval[] = { &obj };

        tTJSVariant res;

        clo.FuncCall(0, nullptr, nullptr, &res, 1, pval, nullptr);

        result = res.operator bool();
    } catch(eTJSScriptError &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(eTJS &e) {
        clo.Release();
        TVPShowScriptException(e);
    } catch(...) {
        clo.Release();
        throw;
    }
    clo.Release();

    return result;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPStartObjectHashMap() {
    // addref ObjectHashMap if the program is being debugged.
    if(TJSEnableDebugMode)
        TJSAddRefObjectHashMap();
}

//---------------------------------------------------------------------------
// TVPBeforeProcessUnhandledException
//---------------------------------------------------------------------------
void TVPBeforeProcessUnhandledException() { TVPDumpHWException(); }
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPShowScriptException
//---------------------------------------------------------------------------
/*
        These functions display the error location, reason, etc.
        And disable the script event dispatching to avoid massive
   occurrence of errors.
*/
extern ttstr TVPGetErrorDialogTitle();

//---------------------------------------------------------------------------
void TVPShowScriptException(eTJS &e) {
    TVPSetSystemEventDisabledState(true);
    TVPOnError();

    if(!TVPSystemUninitCalled) {
        ttstr errstr =
            (ttstr(TVPScriptExceptionRaised) + TJS_W("\n") + e.GetMessage());
        TVPAddLog(ttstr(TVPScriptExceptionRaised) + TJS_W("\n") +
                  e.GetMessage());
        TVPShowSimpleMessageBox(errstr, TVPGetErrorDialogTitle());
        // Application->MessageDlg( errstr.AsStdString(),
        // std::wstring(), mtError, mbOK );
        TVPTerminateSync(1);
    }
}

//---------------------------------------------------------------------------
void TVPShowScriptException(eTJSScriptError &e) {
    TVPSetSystemEventDisabledState(true);
    TVPOnError();

    if(!TVPSystemUninitCalled) {
        ttstr errstr =
            (ttstr(TVPScriptExceptionRaised) + TJS_W("\n") + e.GetMessage());
        TVPAddLog(ttstr(TVPScriptExceptionRaised) + TJS_W("\n") +
                  e.GetMessage());
        if(e.GetTrace().GetLen() != 0)
            TVPAddLog(ttstr(TJS_W("trace : ")) + e.GetTrace());
        TVPShowSimpleMessageBox(errstr, TVPGetErrorDialogTitle());
        //	Application->MessageDlg( errstr.AsStdString(),
        // Application->GetTitle(), mtStop, mbOK );

#ifdef TVP_ENABLE_EXECUTE_AT_EXCEPTION
        const tjs_char *scriptName = e.GetBlockNoAddRef()->GetName();
        if(scriptName != nullptr && scriptName[0] != 0) {
            ttstr path(scriptName);
            try {
                ttstr newpath = TVPGetPlacedPath(path);
                if(newpath.IsEmpty()) {
                    path = TVPNormalizeStorageName(path);
                } else {
                    path = newpath;
                }
                TVPGetLocalName(path);
                std::wstring scriptPath(path.AsStdString());
                tjs_int lineno = 1 +
                    e.GetBlockNoAddRef()->SrcPosToLine(e.GetPosition()) -
                    e.GetBlockNoAddRef()->GetLineOffset();

#if defined(WIN32) && defined(_DEBUG) && !defined(ENABLE_DEBUGGER)
                // デバッガ実行されている時、Visual Studio
                // で行ジャンプする時の指定をデバッグ出力に出して、break
                // で停止する
                if(::IsDebuggerPresent()) {
                    std::wstring debuglile(
                        std::wstring(L"2>") + path.AsStdString() + L"(" +
                        std::to_wstring(lineno) + L"): error :" +
                        errstr.AsStdString());
                    ::OutputDebugString(debuglile.c_str());
                    // ここで
                    // breakで停止した時、直前の出力行をダブルクリックすれば、例外箇所のスクリプトをVisual
                    // Studioで開ける
                    ::DebugBreak();
                }
#endif
                scriptPath =
                    std::wstring(L"\"") + scriptPath + std::wstring(L"\"");
                tTJSVariant val;
                if(TVPGetCommandLine(TJS_W("-exceptionexe"), &val)) {
                    ttstr exepath(val);
                    // exepath = ttstr(TJS_W("\"")) + exepath +
                    // ttstr(TJS_W("\""));
                    if(TVPGetCommandLine(TJS_W("-exceptionarg"), &val)) {
                        ttstr arg(val);
                        if(!exepath.IsEmpty() && !arg.IsEmpty()) {
                            std::wstring str(arg.AsStdString());
                            str = ApplicationSpecialPath::ReplaceStringAll(
                                str, std::wstring(L"%filepath%"), scriptPath);
                            str = ApplicationSpecialPath::ReplaceStringAll(
                                str, std::wstring(L"%line%"),
                                std::to_wstring(lineno));
                            // exepath = exepath + ttstr(str);
                            //_wsystem( exepath.c_str() );
                            arg = ttstr(str);
                            TVPAddLog(ttstr(TJS_W("(execute) ")) + exepath +
                                      ttstr(TJS_W(" ")) + arg);
                            TVPShellExecute(exepath, arg);
                        }
                    }
                }
            } catch(...) {
            }
        }
#endif
        TVPTerminateSync(1);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPInitializeStartupScript
//---------------------------------------------------------------------------
void TVPInitializeStartupScript() {
    TVPStartObjectHashMap();

    TVPExecuteStartupScript();
    if(TVPTerminateOnNoWindowStartup && TVPGetWindowCount() == 0) {
        // no window is created and main window is invisible
        Application->Terminate();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Scripts
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Scripts::ClassID = -1;

tTJSNC_Scripts::tTJSNC_Scripts() :
    inherited(TJS_W("Scripts")){
        // registration of native members

        TJS_BEGIN_NATIVE_MEMBERS(Scripts) TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(
                /*TJS class name*/ Scripts){ return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Scripts)
//----------------------------------------------------------------------

//-- methods

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ execStorage) {
    // execute script which stored in storage
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr name = *param[0];

    ttstr modestr;
    if(numparams >= 2 && param[1]->Type() != tvtVoid)
        modestr = *param[1];

    iTJSDispatch2 *context = numparams >= 3 && param[2]->Type() != tvtVoid
        ? param[2]->AsObjectNoAddRef()
        : nullptr;

    TVPExecuteStorage(name, context, result, false, modestr.c_str());

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ execStorage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ evalStorage) {
    // execute expression which stored in storage
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr name = *param[0];

    ttstr modestr;
    if(numparams >= 2 && param[1]->Type() != tvtVoid)
        modestr = *param[1];

    iTJSDispatch2 *context = numparams >= 3 && param[2]->Type() != tvtVoid
        ? param[2]->AsObjectNoAddRef()
        : nullptr;

    TVPExecuteStorage(name, context, result, true, modestr.c_str());

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ evalStorage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(
    /*func. name*/ compileStorage) // bytecode
{
    if(numparams < 2)
        return TJS_E_BADPARAMCOUNT;

    ttstr name = *param[0];
    ttstr output = *param[1];

    bool isresult = false;
    if(numparams >= 3 && (tjs_int)*param[2]) {
        isresult = true;
    }

    bool outputdebug = false;
    if(numparams >= 4 && (tjs_int)*param[3]) {
        outputdebug = true;
    }

    bool isexpression = false;
    if(numparams >= 5 && (tjs_int)*param[4]) {
        isexpression = true;
    }
    TVPCompileStorage(name, isresult, outputdebug, isexpression, output);

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ compileStorage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ exec) {
    // execute given string as a script
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr content = *param[0];

    ttstr name;
    tjs_int lineofs = 0;
    if(numparams >= 2 && param[1]->Type() != tvtVoid)
        name = *param[1];
    if(numparams >= 3 && param[2]->Type() != tvtVoid)
        lineofs = *param[2];

    iTJSDispatch2 *context = numparams >= 4 && param[3]->Type() != tvtVoid
        ? param[3]->AsObjectNoAddRef()
        : nullptr;

    if(TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, context, &name, lineofs);
    else
        TVPThrowInternalError;

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ exec)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ eval) {
    // execute given string as a script
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr content = *param[0];

    ttstr name;
    tjs_int lineofs = 0;
    if(numparams >= 2 && param[1]->Type() != tvtVoid)
        name = *param[1];
    if(numparams >= 3 && param[2]->Type() != tvtVoid)
        lineofs = *param[2];

    iTJSDispatch2 *context = numparams >= 4 && param[3]->Type() != tvtVoid
        ? param[3]->AsObjectNoAddRef()
        : nullptr;

    if(TVPScriptEngine)
        TVPScriptEngine->EvalExpression(content, result, context, &name,
                                        lineofs);
    else
        TVPThrowInternalError;

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ eval)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ dump) {
    // execute given string as a script
    TVPDumpScriptEngine();

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ dump)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getTraceString) {
    // get current stack trace as string
    tjs_int limit = 0;

    if(numparams >= 1 && param[0]->Type() != tvtVoid)
        limit = *param[0];

    if(result) {
        *result = TJSGetStackTraceString(limit);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getTraceString)
//----------------------------------------------------------------------
#ifdef TJS_DEBUG_DUMP_STRING
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ dumpStringHeap) {
    // dump all strings held by TJS2 framework
    TJSDumpStringHeap();

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ dumpStringHeap)
#endif
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/
                             setCallMissing) /* UNDOCUMENTED: subject
                                              * to change
                                              */
{
    // set to call "missing" method
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    iTJSDispatch2 *dsp = param[0]->AsObjectNoAddRef();

    if(dsp) {
        tTJSVariant missing(TJS_W("missing"));
        dsp->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &missing);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/
                                  setCallMissing) /* UNDOCUMENTED:
                                                     subject to change
                                                   */
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/
                             getClassNames) /* UNDOCUMENTED: subject
                                             * to change
                                             */
{
    // get class name as an array, last (most end) class first.
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    iTJSDispatch2 *dsp = param[0]->AsObjectNoAddRef();

    if(dsp) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        try {
            tjs_uint num = 0;
            while(true) {
                tTJSVariant val;
                tjs_error err = dsp->ClassInstanceInfo(TJS_CII_GET, num, &val);
                if(TJS_FAILED(err))
                    break;
                array->PropSetByNum(TJS_MEMBERENSURE, num, &val, array);
                num++;
            }
            if(result)
                *result = tTJSVariant(array, array);
        } catch(...) {
            array->Release();
            throw;
        }
        array->Release();
    } else {
        return TJS_E_FAIL;
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/
                                  getClassNames) /* UNDOCUMENTED:
                                                    subject to change
                                                  */
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(textEncoding){
    TJS_BEGIN_NATIVE_PROP_GETTER{ *result = TVPGetDefaultReadEncoding();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TVPSetDefaultReadEncoding(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(textEncoding)
//----------------------------------------------------------------------

TJS_END_NATIVE_MEMBERS
}

//---------------------------------------------------------------------------
tTJSNativeInstance *tTJSNC_Scripts::CreateNativeInstance() {
    // this class cannot create an instance
    TVPThrowExceptionMessage(TVPCannotCreateInstance);

    return nullptr;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Scripts
//---------------------------------------------------------------------------
tTJSNativeClass *TVPCreateNativeClass_Scripts() {
    auto *cls = new tTJSNC_Scripts();

    // setup some platform-specific members

    //----------------------------------------------------------------------

    // currently none

    //----------------------------------------------------------------------
    return cls;
}
//---------------------------------------------------------------------------
