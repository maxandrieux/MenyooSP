#pragma once

#include <string>
#include <vector>

#include "BodyguardRole.h"

namespace sub::BodyguardMenu
{
    // A single line in a squad: a model, the role to apply, and how many to spawn.
    struct SquadMember
    {
        std::string  model;   // model name (verbatim, never translated)
        BodyguardRole role{ BodyguardRole::Rifleman };
        int          count{ 1 };
    };

    // A named squad definition. Persisted to menyooStuff/Squads.xml.
    struct SquadDef
    {
        std::string name;          // squad name (proper noun, never translated)
        bool        isDefault{ false };
        std::vector<SquadMember> members;
    };

    // Runtime list of squad definitions. Loaded lazily from Squads.xml.
    extern std::vector<SquadDef> g_squads;

    // Builds the canonical default squad list (kDefaultSquads).
    std::vector<SquadDef> DefaultSquadsVector();

    // Replaces g_squads with the default list (does not save).
    void LoadDefaultSquads();

    // Loads g_squads from Squads.xml; on missing/parse-fail, falls back to
    // defaults and writes them out.
    void LoadSquads();

    // Persists g_squads to Squads.xml (RoleKey for the role attribute).
    void SaveSquads();

    // Restores a single squad's members from the matching default (by index)
    // and saves. No-op when index is out of range or not a default squad.
    void ResetSquadToDefault(int index);

    // Restores every squad to the default list and saves.
    void ResetAllSquads();

    // Spawns every member of a squad (respecting MAX_BODYGUARDS), reusing the
    // shared SpawnBodyguardPed helper so BodyguardDb/s_bodyguards stay synced.
    void SpawnSquad(const SquadDef& def);

    // Submenu builders.
    void BodyguardSquadsMenu();
    void BodyguardSquadEdit();
    void BodyguardSquadMemberEdit();
}
