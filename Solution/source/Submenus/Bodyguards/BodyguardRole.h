#pragma once
#include "../../Natives/types.h"        // Hash
#include "../../Scripting/enums.h"      // WEAPON_*, FiringPattern
#include "../../Scripting/BlipEnums.h"  // BlipIcon
#include <string>
#include <vector>

namespace sub::BodyguardMenu
{
    enum class BodyguardRole : int { Rifleman = 0, Shotgun = 1, Sniper = 2, Heavy = 3, Medic = 4, Driver = 5, Count };

    struct BodyguardRoleDef {
        const char* key;           // TECHNICAL key for enum/XML (English, never translated)
        Hash defaultWeapon;        // WEAPON_* value
        int  accuracy;             // SET_PED_ACCURACY 0..100
        int  shootRate;            // SET_PED_SHOOT_RATE 0..1000
        int  combatAbility;        // SET_PED_COMBAT_ABILITY 0..2
        int  combatRange;          // SET_PED_COMBAT_RANGE 0..2 (0 near,2 far)
        Hash firingPattern;        // FiringPattern::*
        int  blipSprite;           // SET_BLIP_SPRITE id
        int  blipColour;           // SET_BLIP_COLOUR id
    };

    // EXACT verified values. inline => one shared definition across all TUs (C++17+).
    inline const BodyguardRoleDef kRoleDefs[] = {
        { "Rifleman", WEAPON_CARBINERIFLE, 60, 700, 2, 1, FiringPattern::FullAuto,   BlipIcon::Standard,           3  },
        { "Shotgun",  WEAPON_PUMPSHOTGUN,  45, 350, 2, 0, FiringPattern::SingleShot, BlipIcon::Standard,           3  },
        { "Sniper",   WEAPON_SNIPERRIFLE,  95, 220, 1, 2, FiringPattern::SingleShot, BlipIcon::Standard,           29 },
        { "Heavy",    WEAPON_RPG,          50, 280, 2, 2, FiringPattern::SingleShot, BlipIcon::Standard,           49 },
        { "Medic",    WEAPON_PISTOL,       50, 300, 1, 1, FiringPattern::SingleShot, BlipIcon::Health,             2  }, // 153, green
        { "Driver",   WEAPON_MICROSMG,     40, 550, 1, 1, FiringPattern::FullAuto,   BlipIcon::PersonalVehicleCar, 5  }, // 225, yellow
    };
    static_assert(sizeof(kRoleDefs) / sizeof(kRoleDefs[0]) == (size_t)BodyguardRole::Count, "kRoleDefs desynced from BodyguardRole enum");

    inline const BodyguardRoleDef& RoleDef(BodyguardRole r) { int i = (int)r; if (i < 0 || i >= (int)BodyguardRole::Count) i = 0; return kRoleDefs[i]; }
    inline const char* RoleKey(BodyguardRole r) { return RoleDef(r).key; }
    inline BodyguardRole RoleFromKey(const std::string& k) { for (int i = 0; i < (int)BodyguardRole::Count; ++i) if (k == kRoleDefs[i].key) return (BodyguardRole)i; return BodyguardRole::Rifleman; }

    // Display labels for UI texters ONLY (translated via French.json); never used in logic comparisons:
    inline const std::vector<std::string> kRoleDisplayLabels = { "Rifleman", "Shotgun", "Sniper", "Heavy", "Medic", "Driver" };
}
