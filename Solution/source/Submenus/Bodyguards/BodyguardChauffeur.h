#pragma once

#include <string>

#include "BodyguardManagement.h"

namespace sub::BodyguardMenu
{
    extern std::string g_chauffeurVehicleModel; // empty = reuse g_escortVehicleModel
    extern bool g_chauffeurWarpPlayer;

    bool IsChauffeurPed(Ped ped);
    bool IsChauffeurActive();
    std::string GetChauffeurStatusLabel();
    void StartChauffeur(bool toWaypoint);
    void StopChauffeur();
    void TickChauffeur();
    void BodyguardChauffeurMenu();
}
