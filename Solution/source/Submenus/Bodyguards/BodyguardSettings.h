#pragma once

#include "BodyguardManagement.h"
#include "../../Menu/Menu.h"

namespace sub::BodyguardMenu
{
    void BodyguardList();
    void BodyguardOps_();

    // Selection is stored as a ped handle, NOT a raw pointer into BodyguardDb.
    // BodyguardDb is a std::vector: spawning or deleting can reallocate it and invalidate
    // any stored element pointer. Always resolve via GetSelectedBodyguard() each frame.
    extern Ped g_selectedBodyguardHandle;

    // Returns a pointer to the selected bodyguard in BodyguardDb, or nullptr if none/gone.
    // Valid only for the current frame (until BodyguardDb is next mutated) - never store it.
    BodyguardEntity* GetSelectedBodyguard();

    // Group formation (0 Default, 1 Circle Inward, 2 Circle North, 3 Line).
    // Persisted as "formation_index" in menyooConfig.ini [bodyguards].
    extern int g_formationIndex;
    // True when a persisted formation still has to be applied via SET_GROUP_FORMATION.
    // Set by ReadBodyguardConfig (no natives allowed there), consumed by TickBodyguards.
    extern bool g_formationApplyPending;
}
