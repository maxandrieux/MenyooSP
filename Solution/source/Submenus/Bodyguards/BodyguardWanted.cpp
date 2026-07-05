/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
* Copyright (C) 2019  MAFINS
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*/
#include "BodyguardWanted.h"

#include "../../Menu/Menu.h"
#include "../../Menu/Routine.h"   // selfFreezeWantedLevel (DO NOT redefine)
#include "../../Natives/natives.h"

namespace sub::BodyguardMenu
{
    bool g_wantedLockEnabled = false;
    int  g_wantedLockLevel   = 0;

    void BodyguardWantedMenu()
    {
        AddTitle("Wanted Level");

        const Player p = PLAYER::PLAYER_ID();

        // Current wanted level — display only.
        AddNumber("Wanted Level", PLAYER::GET_PLAYER_WANTED_LEVEL(p), 0);

        // Set Wanted Level 0..5 (uses the LEFT/RIGHT arrows to pick a value, A to apply).
        static int s_setLevel = 0;
        if (s_setLevel < 0) s_setLevel = 0;
        if (s_setLevel > 5) s_setLevel = 5;
        bool setInput = false, setPlus = false, setMinus = false;
        AddTexter("Set Wanted Level", 0, { std::to_string(s_setLevel) }, setInput, setPlus, setMinus);
        if (setPlus)  s_setLevel = (s_setLevel >= 5) ? 5 : s_setLevel + 1;
        if (setMinus) s_setLevel = (s_setLevel <= 0) ? 0 : s_setLevel - 1;
        if (setInput)
        {
            PLAYER::SET_PLAYER_WANTED_LEVEL(p, s_setLevel, 0);
            PLAYER::SET_PLAYER_WANTED_LEVEL_NOW(p, 0);
        }

        bool bClear = false;
        AddOption("Clear Wanted Level", bClear);
        if (bClear)
            PLAYER::CLEAR_PLAYER_WANTED_LEVEL(p);

        bool bForceMax = false;
        AddOption("Force Max Wanted", bForceMax);
        if (bForceMax)
        {
            PLAYER::SET_PLAYER_WANTED_LEVEL(p, 5, 0);
            PLAYER::SET_PLAYER_WANTED_LEVEL_NOW(p, 0);
        }

        // Fake Wanted Level 0..5 — visual stars only, no actual police response.
        static int s_fakeLevel = 0;
        if (s_fakeLevel < 0) s_fakeLevel = 0;
        if (s_fakeLevel > 5) s_fakeLevel = 5;
        bool fakeInput = false, fakePlus = false, fakeMinus = false;
        AddTexter("Fake Wanted Level", 0, { std::to_string(s_fakeLevel) }, fakeInput, fakePlus, fakeMinus);
        if (fakePlus)  s_fakeLevel = (s_fakeLevel >= 5) ? 5 : s_fakeLevel + 1;
        if (fakeMinus) s_fakeLevel = (s_fakeLevel <= 0) ? 0 : s_fakeLevel - 1;
        if (fakePlus || fakeMinus || fakeInput)
            MISC::SET_FAKE_WANTED_LEVEL(s_fakeLevel);

        // Lock Level 0..5 bound to g_wantedLockLevel.
        if (g_wantedLockLevel < 0) g_wantedLockLevel = 0;
        if (g_wantedLockLevel > 5) g_wantedLockLevel = 5;
        bool lockLvlInput = false, lockLvlPlus = false, lockLvlMinus = false;
        AddTexter("Lock Level", 0, { std::to_string(g_wantedLockLevel) }, lockLvlInput, lockLvlPlus, lockLvlMinus);
        if (lockLvlPlus)  g_wantedLockLevel = (g_wantedLockLevel >= 5) ? 5 : g_wantedLockLevel + 1;
        if (lockLvlMinus) g_wantedLockLevel = (g_wantedLockLevel <= 0) ? 0 : g_wantedLockLevel - 1;
        // If the lock is active and the level changed, keep the existing writer in sync.
        if (g_wantedLockEnabled && (lockLvlPlus || lockLvlMinus || lockLvlInput))
            selfFreezeWantedLevel = (UINT8)g_wantedLockLevel;

        // Lock Wanted Level — reuses the existing per-frame enforcement via selfFreezeWantedLevel.
        bool bLockOn = false, bLockOff = false;
        AddToggle("Lock Wanted Level", g_wantedLockEnabled, bLockOn, bLockOff);
        if (bLockOn)
            selfFreezeWantedLevel = (UINT8)g_wantedLockLevel;
        if (bLockOff)
            selfFreezeWantedLevel = 0;

        AddBreak("Never Wanted (Player Options) overrides everything; Set/Clear are ignored while Lock is on.");
    }
}

#include "..\..\Menu\submenu_switch.h"
#include "..\..\Menu\submenu_enum.h"
REGISTER_SUBMENU(BODYGUARD_WANTED, sub::BodyguardMenu::BodyguardWantedMenu)
