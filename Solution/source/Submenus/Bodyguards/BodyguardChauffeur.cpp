#include "BodyguardChauffeur.h"

#include "../../Natives/natives.h"
#include "../../Natives/natives2.h"
#include "../../Menu/Language.h"
#include "../../Menu/Menu.h"
#include "../../Scripting/Game.h"
#include "../../Scripting/GTAped.h"
#include "../../Scripting/GTAvehicle.h"
#include "../../Scripting/Model.h"
#include "../../Scripting/enums.h"

#include "BodyguardEscort.h"
#include "BodyguardRole.h"
#include "BodyguardTick.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sub::BodyguardMenu
{
    std::string g_chauffeurVehicleModel;
    bool g_chauffeurWarpPlayer = true;
}

namespace
{
    using namespace sub::BodyguardMenu;

    bool s_active = false;
    bool s_toWaypoint = false;
    Ped s_guardHandle = 0;
    Vehicle s_vehicle = 0;
    bool s_vehicleWasSpawned = false;
    Vector3 s_lastWaypoint = Vector3(0.f, 0.f, 0.f);
    bool s_arrived = false;
    int s_playerBoardRequestedAt = 0;
    int s_playerLeftSince = 0;
    int s_nextHeliWanderAt = 0;
    bool s_heliLandingIssued = false;
    Vehicle s_cleanupVehicle = 0;
    Blip s_vehicleBlip = 0;

    bool EntityVehicleExists(Vehicle veh)
    {
        return veh != 0 && ENTITY::DOES_ENTITY_EXIST(veh) && ENTITY::IS_ENTITY_A_VEHICLE(veh);
    }

    bool IsVehiclePhysicallyOccupied(Vehicle veh)
    {
        if (!EntityVehicleExists(veh))
            return false;

        int maxPassengers = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(veh);
        for (int seat = -1; seat < maxPassengers; ++seat)
        {
            Ped occ = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, seat, false);
            if (occ != 0 && ENTITY::DOES_ENTITY_EXIST(occ))
                return true;
        }
        return false;
    }

    bool IsPedInVehicle(Ped ped, Vehicle veh)
    {
        if (ped == 0 || !ENTITY::DOES_ENTITY_EXIST(ped) || !EntityVehicleExists(veh))
            return false;

        if (PED::GET_VEHICLE_PED_IS_IN(ped, false) == veh)
            return true;

        int maxPassengers = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(veh);
        for (int seat = -1; seat < maxPassengers; ++seat)
        {
            if (VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, seat, false) == ped)
                return true;
        }
        return false;
    }

    bool IsSeatAvailable(Vehicle veh, int seat)
    {
        Ped occ = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, seat, false);
        return occ == 0 || !ENTITY::DOES_ENTITY_EXIST(occ) || PED::IS_PED_DEAD_OR_DYING(occ, true);
    }

    // First free passenger seat, or -2 when the vehicle is full.
    // (SET_PED_INTO_VEHICLE does not support the -2 "any seat" convention of
    // TASK_ENTER_VEHICLE, so warps must resolve a concrete seat index.)
    int FirstFreePassengerSeat(Vehicle veh)
    {
        int maxPassengers = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(veh);
        for (int seat = 0; seat < maxPassengers; ++seat)
        {
            if (IsSeatAvailable(veh, seat))
                return seat;
        }
        return -2;
    }

    void UnlockVehicle(Vehicle veh)
    {
        if (!EntityVehicleExists(veh))
            return;

        VEHICLE::SET_VEHICLE_DOORS_LOCKED(veh, 0);
        for (int door = 0; door < 8; ++door)
            VEHICLE::SET_VEHICLE_INDIVIDUAL_DOORS_LOCKED(veh, door, 0);
        VEHICLE::SET_VEHICLE_DOORS_LOCKED_FOR_PLAYER(veh, PLAYER::PLAYER_ID(), false);
        VEHICLE::SET_VEHICLE_DOORS_LOCKED_FOR_ALL_PLAYERS(veh, false);
        VEHICLE::SET_VEHICLE_DOORS_LOCKED_FOR_NON_SCRIPT_PLAYERS(veh, false);
        VEHICLE::SET_VEHICLE_DOORS_LOCKED_FOR_ALL_TEAMS(veh, false);
        VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(veh, true);
        VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(veh, false);
        VEHICLE::SET_VEHICLE_IS_STOLEN(veh, false);
    }

    void AddVehicleBlip(Vehicle veh)
    {
        if (!EntityVehicleExists(veh))
            return;

        s_vehicleBlip = HUD::ADD_BLIP_FOR_ENTITY(veh);
        if (s_vehicleBlip)
        {
            HUD::SET_BLIP_SPRITE(s_vehicleBlip, 225);
            HUD::SET_BLIP_COLOUR(s_vehicleBlip, 3);
            HUD::SET_BLIP_AS_FRIENDLY(s_vehicleBlip, true);
        }
    }

    void RemoveVehicleBlip()
    {
        if (s_vehicleBlip && HUD::DOES_BLIP_EXIST(s_vehicleBlip))
            HUD::REMOVE_BLIP(&s_vehicleBlip);
        s_vehicleBlip = 0;
    }

    void CleanupPendingVehicle()
    {
        if (!EntityVehicleExists(s_cleanupVehicle))
        {
            s_cleanupVehicle = 0;
            return;
        }

        if (IsVehiclePhysicallyOccupied(s_cleanupVehicle))
            return;

        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(s_cleanupVehicle, true, true);
        GTAvehicle(s_cleanupVehicle).Delete();
        s_cleanupVehicle = 0;
    }

    BodyguardEntity* FindGuard(Ped ped)
    {
        for (auto& bg : BodyguardDb)
        {
            if (bg.Handle.Exists() && bg.Handle.GetHandle() == ped)
                return &bg;
        }
        return nullptr;
    }

    BodyguardEntity* PickChauffeur()
    {
        BodyguardEntity* fallback = nullptr;
        for (auto& bg : BodyguardDb)
        {
            if (!IsBodyguardAlive(bg) || bg.EscortVehicle.Exists() || bg.HoldPosition ||
                IsMedicBusy(bg.Handle.GetHandle()))
                continue;

            if (bg.Role == BodyguardRole::Driver)
                return &bg;
            if (!fallback)
                fallback = &bg;
        }
        return fallback;
    }

    void RemoveFromPlayerGroup(BodyguardEntity& bg)
    {
        if (!IsBodyguardAlive(bg) || bg.RemovedFromGroup)
            return;

        PED::REMOVE_PED_FROM_GROUP(bg.Handle.GetHandle());
        bg.RemovedFromGroup = true;
    }

    void RestorePlayerGroup(BodyguardEntity& bg)
    {
        if (!bg.Handle.Exists())
        {
            bg.RemovedFromGroup = false;
            return;
        }

        Ped ped = bg.Handle.GetHandle();
        if (bg.RemovedFromGroup && !PED::IS_PED_DEAD_OR_DYING(ped, true))
            PED::SET_PED_AS_GROUP_MEMBER(ped, PLAYER::GET_PLAYER_GROUP(PLAYER::PLAYER_ID()));
        PED::SET_PED_NEVER_LEAVES_GROUP(ped, bg.WasNeverLeavesGroup);
        bg.RemovedFromGroup = false;
    }

    bool GetWaypoint(Vector3& out)
    {
        Blip wp = HUD::GET_FIRST_BLIP_INFO_ID(8);
        if (!wp || !HUD::DOES_BLIP_EXIST(wp))
            return false;

        out = HUD::GET_BLIP_INFO_ID_COORD(wp);
        return true;
    }

    float Dist2D(Vector3 a, Vector3 b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return sqrtf(dx * dx + dy * dy);
    }

    int DrivingStyleValue()
    {
        if (g_escortDrivingStyleIndex >= 0 &&
            g_escortDrivingStyleIndex < (int)DrivingStyle::nameArray.size())
            return DrivingStyle::nameArray[g_escortDrivingStyleIndex].style;
        return 0;
    }

    Vehicle SpawnChauffeurVehicle(const std::string& modelName)
    {
        GTAmodel::Model m(modelName);
        if (!m.IsInCdImage() || !m.IsVehicle() || !m.Load(4000))
            return 0;

        Ped playerPed = PLAYER::PLAYER_PED_ID();
        GTAped player(playerPed);
        Vector3 ahead = player.GetOffsetInWorldCoords(Vector3(0.f, 8.f, 0.f));
        Vector3_t node{};
        float heading = player.GetHeading();
        if (!PATHFIND::GET_CLOSEST_VEHICLE_NODE_WITH_HEADING(ahead.x, ahead.y, ahead.z, &node, &heading, 1, 3.f, 0.f))
        {
            node.x = ahead.x;
            node.y = ahead.y;
            node.z = ahead.z;
        }

        Vehicle veh = VEHICLE::CREATE_VEHICLE(m.hash, node.x, node.y, node.z, heading, true, true, false);
        if (EntityVehicleExists(veh))
        {
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(veh, true, true);
            UnlockVehicle(veh);
            VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(veh, 5.f);
            VEHICLE::SET_VEHICLE_ENGINE_ON(veh, true, true, false);
            if (VEHICLE::IS_THIS_MODEL_A_HELI(ENTITY::GET_ENTITY_MODEL(veh)))
                VEHICLE::SET_HELI_BLADES_FULL_SPEED(veh);
            AddVehicleBlip(veh);
        }

        m.Unload();
        return veh;
    }

    void TaskDrive()
    {
        if (!s_active || s_guardHandle == 0 || !ENTITY::DOES_ENTITY_EXIST(s_guardHandle) || !EntityVehicleExists(s_vehicle))
            return;

        s_heliLandingIssued = false; // fresh drive task -> landing may be re-issued later

        const int style = DrivingStyleValue();
        const Hash model = ENTITY::GET_ENTITY_MODEL(s_vehicle);
        const bool isHeli = VEHICLE::IS_THIS_MODEL_A_HELI(model);

        if (isHeli)
        {
            Vector3 dest = s_lastWaypoint;
            if (!s_toWaypoint)
            {
                Ped playerPed = PLAYER::PLAYER_PED_ID();
                GTAped player(playerPed);
                dest = player.GetOffsetInWorldCoords(Vector3(0.f, 1000.f, 120.f));
                s_nextHeliWanderAt = MISC::GET_GAME_TIMER() + 30000;
            }

            TASK::TASK_HELI_MISSION(s_guardHandle, s_vehicle, 0, 0, dest.x, dest.y, dest.z, 4, 40.f, 40.f, -1.f, 120, 20, -1.f, 0);
            PED::SET_PED_KEEP_TASK(s_guardHandle, true);
            return;
        }

        if (s_toWaypoint)
        {
            TASK::TASK_VEHICLE_DRIVE_TO_COORD_LONGRANGE(s_guardHandle, s_vehicle, s_lastWaypoint.x, s_lastWaypoint.y, s_lastWaypoint.z, 20.f, style, 12.f);
        }
        else
        {
            TASK::TASK_VEHICLE_DRIVE_WANDER(s_guardHandle, s_vehicle, 17.f, style);
        }
        PED::SET_PED_KEEP_TASK(s_guardHandle, true);
    }
}

namespace sub::BodyguardMenu
{
    bool IsChauffeurPed(Ped ped)
    {
        return s_active && ped != 0 && ped == s_guardHandle;
    }

    bool IsChauffeurActive()
    {
        return s_active;
    }

    std::string GetChauffeurStatusLabel()
    {
        if (!s_active)
            return "Chauffeur: inactive";

        BodyguardEntity* bg = FindGuard(s_guardHandle);
        std::string name = (bg && !bg->Name.empty()) ? bg->Name : "active";
        return "Chauffeur: " + name;
    }

    void StartChauffeur(bool toWaypoint)
    {
        if (s_active)
            StopChauffeur();

        BodyguardEntity* guard = PickChauffeur();
        if (!guard)
        {
            Game::Print::PrintBottomCentre("~r~" + Language::TranslateToSelected("No available bodyguard"));
            return;
        }

        Ped playerPed = PLAYER::PLAYER_PED_ID();
        GTAped player(playerPed);
        Vehicle veh = 0;
        bool spawned = false;

        if (player.IsInVehicle())
        {
            veh = player.CurrentVehicle().GetHandle();
            if (VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, -1, false) == playerPed)
            {
                int seat = FirstFreePassengerSeat(veh);
                if (seat == -2)
                {
                    Game::Print::PrintBottomCentre("~r~" + Language::TranslateToSelected("No free escort seats"));
                    return;
                }
                PED::SET_PED_INTO_VEHICLE(playerPed, veh, seat);
            }
        }
        else
        {
            std::string model = g_chauffeurVehicleModel.empty() ? g_escortVehicleModel : g_chauffeurVehicleModel;
            veh = SpawnChauffeurVehicle(model);
            spawned = true;
        }

        if (!EntityVehicleExists(veh))
        {
            Game::Print::PrintBottomCentre("~r~" + Language::TranslateToSelected("Invalid vehicle model"));
            return;
        }

        s_toWaypoint = toWaypoint;
        if (s_toWaypoint && !GetWaypoint(s_lastWaypoint))
        {
            s_toWaypoint = false;
            Game::Print::PrintBottomLeft(Language::TranslateToSelected("No waypoint; driving around"));
        }

        s_guardHandle = guard->Handle.GetHandle();
        s_vehicle = veh;
        s_vehicleWasSpawned = spawned;
        s_active = true;
        s_arrived = false;
        s_playerBoardRequestedAt = MISC::GET_GAME_TIMER();
        s_playerLeftSince = 0;

        RemoveFromPlayerGroup(*guard);
        guard->HoldPosition = false;
        guard->Handle.RequestControl();
        TASK::CLEAR_PED_TASKS_IMMEDIATELY(s_guardHandle);
        PED::SET_PED_NEVER_LEAVES_GROUP(s_guardHandle, false);
        PED::SET_PED_KEEP_TASK(s_guardHandle, true);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(s_guardHandle, true);
        UnlockVehicle(veh);
        PED::SET_PED_INTO_VEHICLE(s_guardHandle, veh, -1);

        if (g_chauffeurWarpPlayer)
        {
            int seat = FirstFreePassengerSeat(veh);
            if (seat != -2)
                PED::SET_PED_INTO_VEHICLE(playerPed, veh, seat);
        }
        else
        {
            TASK::TASK_ENTER_VEHICLE(playerPed, veh, 20000, IsSeatAvailable(veh, 0) ? 0 : -2, 1.f, 1, 0);
        }

        TaskDrive();
        TryPlayBodyguardSpeech(s_guardHandle, "GENERIC_YES");
    }

    void StopChauffeur()
    {
        if (!s_active)
            return;

        BodyguardEntity* bg = FindGuard(s_guardHandle);
        if (EntityVehicleExists(s_vehicle) && s_guardHandle != 0 && ENTITY::DOES_ENTITY_EXIST(s_guardHandle))
        {
            TASK::TASK_VEHICLE_TEMP_ACTION(s_guardHandle, s_vehicle, 27, 2500);
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(s_guardHandle, false);
            TASK::CLEAR_PED_TASKS(s_guardHandle);
            if (s_vehicleWasSpawned)
                TASK::TASK_LEAVE_VEHICLE(s_guardHandle, s_vehicle, 0);
        }

        if (bg)
        {
            RestorePlayerGroup(*bg);
            bg->DriverTasked = false;
            bg->LastEscortPlayerInVehicle = false;
            bg->LastEscortTargetVehicle = 0;
        }

        if (s_vehicleWasSpawned && EntityVehicleExists(s_vehicle))
        {
            Ped playerPed = PLAYER::PLAYER_PED_ID();
            if (!IsPedInVehicle(playerPed, s_vehicle))
                s_cleanupVehicle = s_vehicle;
        }

        RemoveVehicleBlip();

        s_active = false;
        s_toWaypoint = false;
        s_guardHandle = 0;
        s_vehicle = 0;
        s_vehicleWasSpawned = false;
        s_lastWaypoint = Vector3(0.f, 0.f, 0.f);
        s_arrived = false;
        s_playerBoardRequestedAt = 0;
        s_playerLeftSince = 0;
        s_nextHeliWanderAt = 0;
        s_heliLandingIssued = false;
    }

    void TickChauffeur()
    {
        CleanupPendingVehicle();

        if (!s_active)
            return;

        const int now = MISC::GET_GAME_TIMER();
        if (s_guardHandle == 0 || !ENTITY::DOES_ENTITY_EXIST(s_guardHandle) || PED::IS_PED_DEAD_OR_DYING(s_guardHandle, true) ||
            !EntityVehicleExists(s_vehicle) || ENTITY::IS_ENTITY_DEAD(s_vehicle, FALSE) || !VEHICLE::IS_VEHICLE_DRIVEABLE(s_vehicle, FALSE))
        {
            StopChauffeur();
            Game::Print::PrintBottomCentre("~r~" + Language::TranslateToSelected("Chauffeur stopped"));
            return;
        }

        Ped playerPed = PLAYER::PLAYER_PED_ID();
        if (!IsPedInVehicle(playerPed, s_vehicle))
        {
            if (s_playerLeftSince == 0)
                s_playerLeftSince = now;
            if (now - s_playerLeftSince > 4000 && now - s_playerBoardRequestedAt > 4000)
                StopChauffeur();
            return;
        }
        s_playerLeftSince = 0;

        const bool isHeli = VEHICLE::IS_THIS_MODEL_A_HELI(ENTITY::GET_ENTITY_MODEL(s_vehicle));
        if (s_toWaypoint)
        {
            Vector3 currentWp;
            if (GetWaypoint(currentWp))
            {
                if (Dist2D(currentWp, s_lastWaypoint) > 25.f)
                {
                    s_lastWaypoint = currentWp;
                    s_arrived = false;
                    TaskDrive();
                }
            }
            else
            {
                s_toWaypoint = false;
                s_arrived = false;
                TaskDrive();
            }

            Vector3 vehPos = ENTITY::GET_ENTITY_COORDS(s_vehicle, TRUE);
            if (!s_arrived && Dist2D(vehPos, s_lastWaypoint) < (isHeli ? 60.f : 12.f))
            {
                if (isHeli)
                {
                    // Issue the land-at-coords mission only once: re-sending it
                    // every tick restarts the landing approach and the heli
                    // never touches down.
                    if (!s_heliLandingIssued)
                    {
                        TASK::TASK_HELI_MISSION(s_guardHandle, s_vehicle, 0, 0, s_lastWaypoint.x, s_lastWaypoint.y, s_lastWaypoint.z, 20, 25.f, 30.f, -1.f, 80, 10, -1.f, 0);
                        s_heliLandingIssued = true;
                    }
                }
                else
                {
                    TASK::TASK_VEHICLE_TEMP_ACTION(s_guardHandle, s_vehicle, 27, 3000);
                }

                if (!isHeli || VEHICLE::IS_VEHICLE_ON_ALL_WHEELS(s_vehicle) || ENTITY::GET_ENTITY_SPEED(s_vehicle) < 1.0f)
                {
                    s_arrived = true;
                    Game::Print::PrintBottomLeft(Language::TranslateToSelected("Arrived"));
                }
            }
        }
        else if (isHeli && now >= s_nextHeliWanderAt)
        {
            TaskDrive();
        }

        if (!s_arrived)
        {
            // Safety net (plan 2.4 step 5): if something external cleared the
            // driving task despite the blocking flag, re-issue it. Status 7 =
            // the ped no longer runs the given script task.
            const Hash expectedTask = isHeli
                ? GET_HASH_KEY("SCRIPT_TASK_HELI_MISSION")
                : (s_toWaypoint ? GET_HASH_KEY("SCRIPT_TASK_VEHICLE_DRIVE_TO_COORD_LONGRANGE")
                                : GET_HASH_KEY("SCRIPT_TASK_VEHICLE_DRIVE_WANDER"));
            if (TASK::GET_SCRIPT_TASK_STATUS(s_guardHandle, expectedTask) == 7)
                TaskDrive();
        }

        if (VEHICLE::GET_PED_IN_VEHICLE_SEAT(s_vehicle, -1, false) != s_guardHandle)
            PED::SET_PED_INTO_VEHICLE(s_guardHandle, s_vehicle, -1);
    }

    void BodyguardChauffeurMenu()
    {
        AddTitle("Chauffeur");
        std::string status = GetChauffeurStatusLabel();
        const std::string prefix = "Chauffeur: ";
        if (status == "Chauffeur: inactive")
            status = Language::TranslateToSelected("Chauffeur: ") + Language::TranslateToSelected("inactive");
        else if (status.rfind(prefix, 0) == 0)
            status = Language::TranslateToSelected("Chauffeur: ") + status.substr(prefix.size());
        AddBreak(status);

        bool bWaypoint = false;
        AddOption("Drive Me To Waypoint", bWaypoint);
        if (bWaypoint)
            StartChauffeur(true);

        bool bAround = false;
        AddOption("Drive Me Around", bAround);
        if (bAround)
            StartChauffeur(false);

        bool bStop = false;
        AddOption("Stop Chauffeur", bStop);
        if (bStop)
            StopChauffeur();

        AddBreak("--- Settings ---");
        AddToggle("Warp Into Vehicle", g_chauffeurWarpPlayer);

        static const std::vector<std::string> values = { "", "sultan", "baller", "bati", "frogger" };
        static const std::vector<std::string> labels = { "Same As Escort", "sultan", "baller", "bati", "frogger" };
        int modelIndex = 0;
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (values[i] == g_chauffeurVehicleModel)
            {
                modelIndex = (int)i;
                break;
            }
        }
        std::vector<std::string> modelChoices = labels;
        if (!g_chauffeurVehicleModel.empty() &&
            std::find(values.begin(), values.end(), g_chauffeurVehicleModel) == values.end())
        {
            modelChoices.push_back(g_chauffeurVehicleModel);
            modelIndex = (int)modelChoices.size() - 1;
        }

        bool mA = false, mR = false, mL = false;
        AddTexter("Chauffeur Vehicle Model", modelIndex, modelChoices, mA, mR, mL);
        if (mR || mL)
        {
            int newIndex = modelIndex + (mR ? 1 : -1);
            if (newIndex < 0) newIndex = (int)modelChoices.size() - 1;
            if (newIndex >= (int)modelChoices.size()) newIndex = 0;
            g_chauffeurVehicleModel = (newIndex < (int)values.size()) ? values[newIndex] : modelChoices[newIndex];
        }

        bool bCustom = false;
        AddOption("Custom Model...", bCustom);
        if (bCustom)
        {
            std::string input = Game::InputBox("", 64, "Chauffeur Vehicle Model", g_chauffeurVehicleModel);
            if (!input.empty())
            {
                GTAmodel::Model m(input);
                if (m.IsInCdImage() && m.IsVehicle())
                    g_chauffeurVehicleModel = input;
                else
                    Game::Print::PrintBottomCentre("~r~" + Language::TranslateToSelected("Invalid vehicle model"));
            }
        }

        AddBreak(Language::TranslateToSelected("Driving Style: shared with Escort"));
    }
}

#include "..\..\Menu\submenu_switch.h"
#include "..\..\Menu\submenu_enum.h"
REGISTER_SUBMENU(BODYGUARD_CHAUFFEUR, sub::BodyguardMenu::BodyguardChauffeurMenu)
