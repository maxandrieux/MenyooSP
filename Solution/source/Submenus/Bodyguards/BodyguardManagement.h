#pragma once

#include <vector>
#include <string>

#include "..\..\Scripting/GTAentity.h"
#include "..\..\Natives/types.h"
#include "BodyguardRole.h"

namespace sub::BodyguardMenu
{
    class BodyguardEntity
    {
    public:
        EntityType Type{};
        std::string Name;
        std::string HashName;
        GTAentity Handle;

        // --- v2 fields (appended; do not reorder the members above) ---
        BodyguardRole Role{ BodyguardRole::Rifleman };
        GTAentity EscortVehicle;            // runtime handle — NEVER persisted
        int EscortSeat{ -2 };               // -1 driver, 0..N passengers, -2 = unassigned
        bool WasNeverLeavesGroup{ true };   // restore state for escort driving
        bool RemovedFromGroup{ false };
        bool DriverTasked{ false };
        bool LastEscortPlayerInVehicle{ false };
        Vehicle LastEscortTargetVehicle{ 0 };
        int NextReboardAt{ 0 };
        int ReboardAttempts{ 0 };
        bool CombatTasked{ false };         // runtime only, never persisted
        Ped LastCombatTarget{ 0 };          // runtime handle — NEVER persisted
        int LastCombatTaskAt{ 0 };
        bool HoldPosition{ false };         // runtime only, never persisted
    };

    extern std::vector<BodyguardEntity> BodyguardDb;

    bool operator==(const BodyguardEntity& a, const BodyguardEntity& b);
    bool operator!=(const BodyguardEntity& a, const BodyguardEntity& b);

    // A bodyguard is "alive" only when its handle still exists in the world and is not dead.
    // Exists() alone is not enough: a dead ped still exists for a while.
    inline bool IsBodyguardAlive(const BodyguardEntity& bg)
    {
        return bg.Handle.Exists() && !bg.Handle.IsDead();
    }

    namespace BodyguardManagement
    {
        unsigned int GetNumberOfBodyguardsSpawned(const EntityType&);
        int GetBodyguardIndexInDb(const GTAentity&);
        int GetBodyguardIndexInDb(const BodyguardEntity&);
        void RemoveBodyguardFromDb(const BodyguardEntity&);
        void DeleteBodyguard(BodyguardEntity&);
        void AddBodyguardToDb(BodyguardEntity ent);

        // Purges every tracking list (BodyguardDb + s_bodyguards) of the given ped handle.
        // Captures the handle by value, so it is safe even when the argument is a reference
        // into BodyguardDb that is about to be erased.
        void RemoveBodyguardByHandle(Ped handle);

        // Removes only dead/missing bodyguards from tracking (and their blips). Returns count removed.
        int CleanupDeadBodyguards();

        // Removes every tracked bodyguard: deletes peds, removes blips, clears all tracking and selection.
        void DismissAllBodyguards();

        // Diagnostics: logs a one-line snapshot (db size / s_bodyguards size / selected handle,
        // plus a desync warning) to the Bodyguard debug log. Call after any squad state change.
        void DbgLogSquadState(const std::string& tag);

        // Draws an arrow above the specified entity to highlight it in the world
        void ShowArrowAboveEntity(const GTAentity& ent, RGBA colour = {255, 0, 0, 190});
    }
}
