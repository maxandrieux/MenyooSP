#pragma once

#include <string>

#include "BodyguardManagement.h"

namespace sub::BodyguardMenu
{
    // --- Escort persistent settings (defined in BodyguardEscort.cpp) ---
    // Persist in menyooConfig.ini [bodyguards]; NEVER persist runtime handles.
    extern bool g_escortUseMySeatsFirst;     // fill the player's own vehicle first
    extern bool g_escortSpawnIfFull;         // spawn a dedicated escort vehicle when needed
    extern bool g_escortAutoAssignNewSpawns; // auto-seat new guards when an escort convoy exists
    extern bool g_escortReboardAfterCombat;  // re-seat assigned guards after combat pulls them out
    extern bool g_escortVehicleGodmode;      // protect mod-spawned escort vehicles
    extern bool g_escortCatchupTeleport;     // teleport distant convoy/foot guards back off-screen
    extern bool g_escortRealisticBoarding;   // ask nearby guards to enter naturally before warp fallback
    extern std::string g_escortVehicleModel; // model name used for the spawned escort vehicle
    extern int g_escortDrivingStyleIndex;    // index into DrivingStyle::nameArray (NOT the style value)
    extern std::string g_escortHeliModel;    // model name used by Spawn Escort Helicopter

    // Seats un-seated alive bodyguards into the player's vehicle and/or a spawned escort vehicle.
    void AssignBodyguardsToEscort();

    // Per-tick driving task maintenance for escort drivers. Throttling is done by the caller.
    void TickEscort();

    // Per-tick catch-up for non-escort guards on foot. Throttling is done by the caller.
    void TickEscortCatchupOnFoot();

    bool HasActiveEscortConvoy();

    // Purges dead handles and deletes now-empty mod-spawned escort vehicles.
    // Safe to call with no bodyguards tracked (used by the tick and Dismiss All).
    void CleanupEscortVehicles();

    // Restores a bodyguard's group/escort state. Deletes only a spawned (now-empty) escort vehicle.
    void ClearEscortState(BodyguardEntity& bg);

    // Escort Vehicle submenu (registered under SUB::BODYGUARD_ESCORT).
    void BodyguardEscortMenu();
}
