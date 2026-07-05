#pragma once

namespace sub::BodyguardMenu
{
    // Toggle persisted as "hud_enabled" in menyooConfig.ini [bodyguards].
    extern bool g_hudEnabled;

    // Per-frame HUD renderer. Called from TickBodyguards (BodyguardTick.cpp).
    void TickBodyguardHud();

    // Releases every registered headshot slot.
    void ReleaseAllBodyguardHeadshots();
}
