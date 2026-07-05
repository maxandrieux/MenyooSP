#pragma once

#include <string>
#include "../../Menu/Routine.h"
#include "BodyguardRole.h"
#include "BodyguardManagement.h"

using TargetPed = int;

//TargetPed g_WeaponMenuPedOverride = 0;
namespace sub
{
    void BodyguardMainMenu();
}

namespace sub::BodyguardMenu
{
    extern int armor;
    extern int health;
    extern bool godmode;
    extern int blipIcon;

    // When enabled, blips use the per-role sprite/colour; otherwise the legacy blipIcon.
    extern bool g_roleBlipsEnabled;

    void ApplyBodyguardBlip(Ped ped, int icon);
    void RemoveBodyguardBlip(Ped ped);
    void RefreshAllBodyguardBlips();
    void UpdateBodyguardBlipsOnDeath();

    // Applies the role's weapon/accuracy/combat tuning to an already-spawned bodyguard.
    void ApplyRoleToBodyguard(Ped ped, BodyguardRole role);
    // Applies a blip using the role sprite/colour (when g_roleBlipsEnabled) or legacy blipIcon.
    void ApplyBodyguardBlipForRole(Ped ped, BodyguardRole role);

    // --- Active-bodyguards tools / stat presets persistent state (v1) ---
    extern int  g_armWeaponIndex;        // index into the Arm-All weapon list (default Pistol)
    extern bool g_autoArmNewBodyguards;  // default OFF; when ON, overrides the role weapon at spawn
    extern int  g_spawnWeaponIndex;      // index into the spawn weapon list (0 = None)
    extern int  g_presetIndex;           // selected stat preset
    extern bool g_presetApplyCurrent;    // default ON
    extern bool g_presetUseForNew;       // default OFF; copies health/armor/godmode defaults only (never touches spawn arming)

    // Applies the configured spawn weapon to a freshly spawned bodyguard (no-op if disabled/None).
    void ApplyAutoArmOnSpawn(Ped ped);

    // Revives one tracked bodyguard record if its ped still exists. Returns false
    // only when the body has already been recycled/despawned.
    bool ReviveOneBodyguard(BodyguardEntity& bg);

    // Renders the spawn-arming defaults (auto-arm toggle + spawn weapon texter).
    // Called from the Settings submenu, "Spawn Defaults" section.
    void AddSpawnArmingOptions();

    // "Active Bodyguards" submenu (weapons / health / stat presets / cleanup),
    // registered on BODYGUARD_SQUAD_TOOLS (historical enum id, never reorder).
    void BodyguardSquadTools();
}
