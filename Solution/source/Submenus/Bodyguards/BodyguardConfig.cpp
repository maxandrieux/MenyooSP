#include "BodyguardConfig.h"

#include "BodyguardRole.h"
#include "BodyguardSpawn.h"   // g_defaultSpawnRole
#include "BodyguardMenu.h"    // g_roleBlipsEnabled
#include "BodyguardEscort.h"  // g_escort*
#include "BodyguardCombat.h"  // g_combatResponseMode
#include "BodyguardChauffeur.h" // g_chauffeur*
#include "BodyguardHud.h"     // g_hudEnabled
#include "BodyguardWanted.h"  // g_wantedLockEnabled / g_wantedLockLevel
#include "BodyguardSettings.h" // g_formationIndex / g_formationApplyPending
#include "BodyguardMenu.h"    // spawn defaults
#include "BodyguardTick.h"    // medic/voice/spacing
#include "BodyguardSquads.h"  // LoadSquads
#include "../../Menu/Routine.h" // selfFreezeWantedLevel (re-arm lock on load)

namespace sub::BodyguardMenu
{
    static const char* kSection = "bodyguards";

    void ReadBodyguardConfig(CSimpleIniA& ini)
    {
        // default_spawn_role round-trips through the TECHNICAL role key.
        {
            std::string current = RoleKey(g_defaultSpawnRole);
            std::string val = ini.GetValue(kSection, "default_spawn_role", current.c_str());
            g_defaultSpawnRole = RoleFromKey(val);
        }

        g_roleBlipsEnabled = ini.GetBoolValue(kSection, "role_blips_enabled", g_roleBlipsEnabled);
        g_hudEnabled = ini.GetBoolValue(kSection, "hud_enabled", g_hudEnabled);

        g_escortUseMySeatsFirst = ini.GetBoolValue(kSection, "escort_use_my_seats_first", g_escortUseMySeatsFirst);
        g_escortSpawnIfFull = ini.GetBoolValue(kSection, "escort_spawn_if_full", g_escortSpawnIfFull);
        g_escortAutoAssignNewSpawns = ini.GetBoolValue(kSection, "escort_auto_assign", g_escortAutoAssignNewSpawns);
        g_escortReboardAfterCombat = ini.GetBoolValue(kSection, "escort_reboard_after_combat", g_escortReboardAfterCombat);
        g_escortVehicleGodmode = ini.GetBoolValue(kSection, "escort_vehicle_godmode", g_escortVehicleGodmode);
        g_escortCatchupTeleport = ini.GetBoolValue(kSection, "escort_catchup_teleport", g_escortCatchupTeleport);
        g_escortRealisticBoarding = ini.GetBoolValue(kSection, "escort_realistic_boarding", g_escortRealisticBoarding);
        g_escortVehicleModel = ini.GetValue(kSection, "escort_vehicle_model", g_escortVehicleModel.c_str());
        g_escortDrivingStyleIndex = (int)ini.GetLongValue(kSection, "escort_driving_style_index", g_escortDrivingStyleIndex);
        g_escortHeliModel = ini.GetValue(kSection, "escort_heli_model", g_escortHeliModel.c_str());

        g_chauffeurVehicleModel = ini.GetValue(kSection, "chauffeur_vehicle_model", g_chauffeurVehicleModel.c_str());
        g_chauffeurWarpPlayer = ini.GetBoolValue(kSection, "chauffeur_warp_player", g_chauffeurWarpPlayer);

        g_combatResponseMode = (int)ini.GetLongValue(kSection, "combat_response_mode", g_combatResponseMode);
        if (g_combatResponseMode < 0 || g_combatResponseMode > 4)
            g_combatResponseMode = 4;
        g_wantedThreatMinStars = (int)ini.GetLongValue(kSection, "combat_wanted_min_stars", g_wantedThreatMinStars);
        if (g_wantedThreatMinStars < 1 || g_wantedThreatMinStars > 5)
            g_wantedThreatMinStars = 2;

        g_medicEnabled = ini.GetBoolValue(kSection, "medic_enabled", g_medicEnabled);
        g_medicHealPlayer = ini.GetBoolValue(kSection, "medic_heal_player", g_medicHealPlayer);
        g_voiceLinesEnabled = ini.GetBoolValue(kSection, "voice_lines_enabled", g_voiceLinesEnabled);

        health = (int)ini.GetLongValue(kSection, "default_health", health);
        armor = (int)ini.GetLongValue(kSection, "default_armor", armor);
        godmode = ini.GetBoolValue(kSection, "default_godmode", godmode);
        g_autoArmNewBodyguards = ini.GetBoolValue(kSection, "auto_arm_new", g_autoArmNewBodyguards);
        g_spawnWeaponIndex = (int)ini.GetLongValue(kSection, "spawn_weapon_index", g_spawnWeaponIndex);
        if (g_spawnWeaponIndex < 0 || g_spawnWeaponIndex > 9)
            g_spawnWeaponIndex = 0;
        g_armWeaponIndex = (int)ini.GetLongValue(kSection, "arm_weapon_index", g_armWeaponIndex);
        if (g_armWeaponIndex < 0 || g_armWeaponIndex > 8)
            g_armWeaponIndex = 0;

        g_wantedLockEnabled = ini.GetBoolValue(kSection, "wanted_lock_enabled", g_wantedLockEnabled);
        g_wantedLockLevel = (int)ini.GetLongValue(kSection, "wanted_lock_level", g_wantedLockLevel);

        g_formationIndex = (int)ini.GetLongValue(kSection, "formation_index", g_formationIndex);
        if (g_formationIndex < 0 || g_formationIndex > 3)
            g_formationIndex = 0;
        g_formationSpacing = (int)ini.GetLongValue(kSection, "formation_spacing", g_formationSpacing);
        if (g_formationSpacing < 0 || g_formationSpacing > 10)
            g_formationSpacing = 0;
        // SET_GROUP_FORMATION is a native, forbidden here: defer to the first
        // TickBodyguards call (same pattern as the wanted-lock re-arm below).
        // Index 0 is the game default, nothing to re-apply.
        if (g_formationIndex != 0 || g_formationSpacing > 0)
            g_formationApplyPending = true;

        // Re-arm the wanted lock if it was persisted ON, so it takes effect from
        // load without needing the user to re-toggle it (the menu toggle is the
        // only other writer of selfFreezeWantedLevel).
        if (g_wantedLockEnabled)
            selfFreezeWantedLevel = (UINT8)g_wantedLockLevel;

        // Load saved/default squads at startup so a squad can be spawned before
        // the Squads menu has been opened. Safe here: pure file/vector work, no natives.
        LoadSquads();
    }

    void SaveBodyguardConfig(CSimpleIniA& ini)
    {
        ini.SetValue(kSection, "default_spawn_role", RoleKey(g_defaultSpawnRole));
        ini.SetBoolValue(kSection, "role_blips_enabled", g_roleBlipsEnabled);
        ini.SetBoolValue(kSection, "hud_enabled", g_hudEnabled);

        ini.SetBoolValue(kSection, "escort_use_my_seats_first", g_escortUseMySeatsFirst);
        ini.SetBoolValue(kSection, "escort_spawn_if_full", g_escortSpawnIfFull);
        ini.SetBoolValue(kSection, "escort_auto_assign", g_escortAutoAssignNewSpawns);
        ini.SetBoolValue(kSection, "escort_reboard_after_combat", g_escortReboardAfterCombat);
        ini.SetBoolValue(kSection, "escort_vehicle_godmode", g_escortVehicleGodmode);
        ini.SetBoolValue(kSection, "escort_catchup_teleport", g_escortCatchupTeleport);
        ini.SetBoolValue(kSection, "escort_realistic_boarding", g_escortRealisticBoarding);
        ini.SetValue(kSection, "escort_vehicle_model", g_escortVehicleModel.c_str());
        ini.SetLongValue(kSection, "escort_driving_style_index", g_escortDrivingStyleIndex);
        ini.SetValue(kSection, "escort_heli_model", g_escortHeliModel.c_str());

        ini.SetValue(kSection, "chauffeur_vehicle_model", g_chauffeurVehicleModel.c_str());
        ini.SetBoolValue(kSection, "chauffeur_warp_player", g_chauffeurWarpPlayer);

        ini.SetLongValue(kSection, "combat_response_mode", g_combatResponseMode);
        ini.SetLongValue(kSection, "combat_wanted_min_stars", g_wantedThreatMinStars);

        ini.SetBoolValue(kSection, "medic_enabled", g_medicEnabled);
        ini.SetBoolValue(kSection, "medic_heal_player", g_medicHealPlayer);
        ini.SetBoolValue(kSection, "voice_lines_enabled", g_voiceLinesEnabled);

        ini.SetLongValue(kSection, "default_health", health);
        ini.SetLongValue(kSection, "default_armor", armor);
        ini.SetBoolValue(kSection, "default_godmode", godmode);
        ini.SetBoolValue(kSection, "auto_arm_new", g_autoArmNewBodyguards);
        ini.SetLongValue(kSection, "spawn_weapon_index", g_spawnWeaponIndex);
        ini.SetLongValue(kSection, "arm_weapon_index", g_armWeaponIndex);

        ini.SetBoolValue(kSection, "wanted_lock_enabled", g_wantedLockEnabled);
        ini.SetLongValue(kSection, "wanted_lock_level", g_wantedLockLevel);

        ini.SetLongValue(kSection, "formation_index", g_formationIndex);
        ini.SetLongValue(kSection, "formation_spacing", g_formationSpacing);
    }
}
