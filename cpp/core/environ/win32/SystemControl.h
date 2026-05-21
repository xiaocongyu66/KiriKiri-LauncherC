//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// System Main Window (Controller)
//---------------------------------------------------------------------------
#ifndef SystemControlH
#define SystemControlH
//---------------------------------------------------------------------------
#include <string>
#include "TVPTimer.h"
//---------------------------------------------------------------------------
class tTVPSystemControl {
private: // ÉÜÅ[ÉUÅ[êÈåæ
    bool ContinuousEventCalling;
    bool AutoShowConsoleOnError;

    bool EventEnable;

    uint32_t LastCompactedTick;
    uint32_t LastCloseClickedTick;
    uint32_t LastShowModalWindowSentTick;
    uint32_t LastRehashedTick;

    uint32_t MixedIdleTick{};

    TVPTimer SystemWatchTimer;

public:
    tTVPSystemControl();

    void InvokeEvents();
    void CallDeliverAllEventsOnIdle();

    void BeginContinuousEvent();
    void EndContinuousEvent();

    void NotifyCloseClicked();
    void NotifyEventDelivered();

    void SetEventEnabled(bool b) { EventEnable = b; }
    [[nodiscard]] bool GetEventEnabled() const { return EventEnable; }

    bool ApplicationIdle();

private:
    void DeliverEvents();

public:
    void SystemWatchTimerTimer();
};
extern tTVPSystemControl *TVPSystemControl;
extern bool TVPSystemControlAlive;

// Registers a callback that the system uses to query the PSB image-cache
// usage of the psbfile plugin. The plugin calls this with its own callback
// at module load; passing nullptr clears it. Imported from KrKr2-Next.
void TVPRegisterPSBCacheInfoCallback(bool (*cb)(size_t &usedBytes, size_t &limitBytes));

#endif
