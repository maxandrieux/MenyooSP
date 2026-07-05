/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
* Copyright (C) 2019  MAFINS
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*/
#pragma once

namespace sub::BodyguardMenu
{
    // When ON, mirrors g_wantedLockLevel into the EXISTING extern selfFreezeWantedLevel
    // (declared in Routine.h) so the existing per-frame enforcement at Routine.cpp:3432
    // keeps the wanted level pinned. We deliberately reuse that single writer instead of
    // adding a second one. When OFF, selfFreezeWantedLevel is reset to 0.
    extern bool g_wantedLockEnabled;
    extern int  g_wantedLockLevel;

    void BodyguardWantedMenu();
}
