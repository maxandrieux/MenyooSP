#pragma once

#include "../../Natives/types.h"

namespace sub::BodyguardMenu
{
    // Combat response mode: 0 = off, 1 = player target, 2 = retaliate,
    // 3 = wanted threats, 4 = full protection.
    // Backed by [bodyguards] combat_response_mode (long). Defined in BodyguardCombat.cpp.
    extern int g_combatResponseMode;
    extern int g_wantedThreatMinStars;

    // Per-frame combat driver. No-op when g_combatResponseMode == 0.
    // Called from TickBodyguards (BodyguardTick.cpp) every tick.
    void TickCombatResponse();

    Entity ResolvePlayerTargetEntity();
    void TaskAllBodyguardsOnTarget(Entity target);
    void CeaseFireAll();
}
