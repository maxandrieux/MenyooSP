#pragma once

#include "BodyguardManagement.h"

namespace sub::BodyguardMenu
{
    extern bool g_medicEnabled;
    extern bool g_medicHealPlayer;
    extern bool g_voiceLinesEnabled;
    extern int g_formationSpacing;

    bool IsMedicBusy(Ped ped);
    bool IsBusyBodyguard(Ped ped);
    void SetBodyguardHoldPosition(BodyguardEntity& bg, bool hold);
    void SetAllBodyguardsHoldPosition(bool hold);
    void TryPlayBodyguardSpeech(Ped preferredPed, const char* speechName);

    // Per-frame bodyguard driver. Hosted by ThreadMenuLoops2() (Routine.cpp).
    // Early-outs when no bodyguards exist, otherwise throttles its sub-systems:
    //   ~400ms -> UpdateBodyguardBlipsOnDeath()
    //   every call -> TickCombatResponse() (itself a no-op when combat mode == 0)
    //   ~750ms -> TickEscort()
    void TickBodyguards();
}
