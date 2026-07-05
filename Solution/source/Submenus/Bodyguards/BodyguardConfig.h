#pragma once

#include <simpleini\SimpleIni.h>

namespace sub::BodyguardMenu
{
    // Reads/writes the [bodyguards] section of menyooConfig.ini.
    // Only simple settings are persisted here; squad DEFINITIONS live in Squads.xml,
    // and runtime handles (Ped/Vehicle/Blip) are NEVER persisted.
    void ReadBodyguardConfig(CSimpleIniA& ini);
    void SaveBodyguardConfig(CSimpleIniA& ini);
}
