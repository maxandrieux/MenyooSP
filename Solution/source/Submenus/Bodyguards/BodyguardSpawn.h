#pragma once
#include <string>
#include "../../Scripting/Model.h"
#include "../../Menu/Routine.h"
#include "../../Scripting/GTAped.h"
#include "BodyguardRole.h"

typedef int INT;
typedef signed char INT8;
typedef unsigned char UINT8;
typedef unsigned long DWORD;
typedef unsigned long Hash;

namespace GTAmodel {
    class Model;
}

namespace sub::BodyguardMenu
{
    extern int health;
    extern int armor;
    extern bool godmode;

    // Global default role applied to bodyguards spawned via AddOptionBodyGuardPed.
    extern BodyguardRole g_defaultSpawnRole;

    extern std::string _searchStr;

    void BodyguardSpawn();

    namespace BodyguardManagement
    {
        extern std::vector<Ped> s_bodyguards;
        static constexpr size_t MAX_BODYGUARDS = 7;
        void AddOptionBodyGuardPed(const std::string& text, const GTAmodel::Model& model);

        // Shared spawn helper: creates a bodyguard ped with the given role and keeps
        // BodyguardDb + s_bodyguards in sync. Returns the spawned ped (0 on failure).
        Ped SpawnBodyguardPed(const GTAmodel::Model& model, const std::string& text, BodyguardRole role, bool deferConvoyAssign = false);
    }
}
