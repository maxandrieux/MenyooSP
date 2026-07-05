#include "BodyguardEscort.h"

#include "../../Natives/types.h"
#include "../../Natives/natives2.h"
#include "../../Natives/natives.h"

#include "../../Menu/Menu.h"
#include "../../Menu/Language.h"
#include "../../Scripting/Game.h"
#include "../../Scripting/GTAped.h"
#include "../../Scripting/GTAvehicle.h"
#include "../../Scripting/Model.h"
#include "../../Scripting/enums.h"

#include "BodyguardManagement.h"
#include "BodyguardSpawn.h"
#include "BodyguardMenu.h"
#include "BodyguardRole.h"
#include "BodyguardChauffeur.h"
#include "BodyguardTick.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace sub::BodyguardMenu
{
    bool g_escortUseMySeatsFirst = true;
    bool g_escortSpawnIfFull = false;
    bool g_escortAutoAssignNewSpawns = false;
    bool g_escortReboardAfterCombat = true;
    bool g_escortVehicleGodmode = false;
    bool g_escortCatchupTeleport = true;
    bool g_escortRealisticBoarding = false;
    std::string g_escortVehicleModel = "police";
    int g_escortDrivingStyleIndex = 0;
    std::string g_escortHeliModel = "buzzard2";

    static const std::vector<std::string> kEscortQuickModels = { "police", "sultan", "riot", "insurgent" };
    static const std::vector<std::string> kEscortHeliQuickModels = { "buzzard2", "polmav", "maverick", "frogger" };

    namespace
    {
        struct SpawnedEscortVehicle
        {
            Vehicle veh{ 0 };
            int emptySinceMs{ 0 };
            bool deleteWhenEmpty{ false }; // set by the explicit "Clear Escort Assignments" button
        };

        static std::vector<SpawnedEscortVehicle> s_spawnedEscortVehicles;
        static constexpr int kEscortVehicleEmptyGraceMs = 12000;
        static bool s_lastAppliedVehGodmode = false;
        static bool s_vehGodmodeDirty = true;

        bool EntityVehicleExists(Vehicle veh)
        {
            return veh != 0 && ENTITY::DOES_ENTITY_EXIST(veh) && ENTITY::IS_ENTITY_A_VEHICLE(veh);
        }

        void ApplyEscortVehicleGodmode(Vehicle veh)
        {
            if (!EntityVehicleExists(veh))
                return;

            ENTITY::SET_ENTITY_INVINCIBLE(veh, g_escortVehicleGodmode);
            ENTITY::SET_ENTITY_PROOFS(
                veh,
                g_escortVehicleGodmode,
                false,
                g_escortVehicleGodmode,
                g_escortVehicleGodmode,
                g_escortVehicleGodmode,
                g_escortVehicleGodmode,
                g_escortVehicleGodmode,
                g_escortVehicleGodmode);
            VEHICLE::SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED(veh, !g_escortVehicleGodmode);
        }

        void AddEscortVehicleBlip(Vehicle veh)
        {
            if (!EntityVehicleExists(veh))
                return;

            Blip b = HUD::ADD_BLIP_FOR_ENTITY(veh);
            if (!b)
                return;

            HUD::SET_BLIP_SPRITE(b, 225);
            HUD::SET_BLIP_COLOUR(b, 3);
            HUD::SET_BLIP_AS_FRIENDLY(b, true);
        }

        void RemoveEscortVehicleBlip(Vehicle veh)
        {
            if (!EntityVehicleExists(veh))
                return;

            Blip b = HUD::GET_BLIP_FROM_ENTITY(veh);
            if (b && HUD::DOES_BLIP_EXIST(b))
                HUD::REMOVE_BLIP(&b);
        }

        bool IsSpawnedEscortVehicle(Vehicle veh)
        {
            return std::find_if(s_spawnedEscortVehicles.begin(), s_spawnedEscortVehicles.end(),
                [veh](const SpawnedEscortVehicle& rec)
                {
                    return rec.veh == veh;
                }) != s_spawnedEscortVehicles.end();
        }

        bool IsVehicleWrecked(Vehicle veh)
        {
            return EntityVehicleExists(veh) &&
                (ENTITY::IS_ENTITY_DEAD(veh, FALSE) || !VEHICLE::IS_VEHICLE_DRIVEABLE(veh, FALSE));
        }

        float DistanceSq(Vector3 a, Vector3 b)
        {
            float dx = a.x - b.x;
            float dy = a.y - b.y;
            float dz = a.z - b.z;
            return dx * dx + dy * dy + dz * dz;
        }

        float DistSq(Entity a, Entity b)
        {
            return DistanceSq(ENTITY::GET_ENTITY_COORDS(a, TRUE), ENTITY::GET_ENTITY_COORDS(b, TRUE));
        }

        void ReapplyEscortGodmodeIfChanged()
        {
            if (!s_vehGodmodeDirty && s_lastAppliedVehGodmode == g_escortVehicleGodmode)
                return;

            for (const auto& rec : s_spawnedEscortVehicles)
                ApplyEscortVehicleGodmode(rec.veh);

            s_lastAppliedVehGodmode = g_escortVehicleGodmode;
            s_vehGodmodeDirty = false;
        }

        void PurgeSpawnedEscortVehicles()
        {
            s_spawnedEscortVehicles.erase(
                std::remove_if(s_spawnedEscortVehicles.begin(), s_spawnedEscortVehicles.end(),
                    [](const SpawnedEscortVehicle& rec)
                    {
                        return !EntityVehicleExists(rec.veh);
                    }),
                s_spawnedEscortVehicles.end());
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

        int GetPedSeatInVehicle(Ped ped, Vehicle veh)
        {
            if (ped == 0 || !ENTITY::DOES_ENTITY_EXIST(ped) || !EntityVehicleExists(veh))
                return -2;

            int maxPassengers = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(veh);
            for (int seat = -1; seat < maxPassengers; ++seat)
            {
                if (VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, seat, false) == ped)
                    return seat;
            }

            return -2;
        }

        bool IsVehiclePhysicallyOccupied(Vehicle veh)
        {
            if (!EntityVehicleExists(veh))
                return false;

            int maxPassengers = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(veh);
            for (int seat = -1; seat < maxPassengers; ++seat)
            {
                Ped occupant = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, seat, false);
                if (occupant != 0 && ENTITY::DOES_ENTITY_EXIST(occupant))
                    return true;
            }

            return false;
        }

        bool AnyAliveGuardAssignedTo(Vehicle veh)
        {
            for (auto& bg : BodyguardDb)
            {
                if (IsBodyguardAlive(bg) &&
                    bg.EscortVehicle.Exists() &&
                    bg.EscortVehicle.GetHandle() == veh)
                    return true;
            }

            return false;
        }

        void DeleteEmptySpawnedEscortVehicles()
        {
            PurgeSpawnedEscortVehicles();
            const int now = MISC::GET_GAME_TIMER();

            for (size_t i = 0; i < s_spawnedEscortVehicles.size(); )
            {
                SpawnedEscortVehicle& rec = s_spawnedEscortVehicles[i];
                Vehicle veh = rec.veh;
                if (!EntityVehicleExists(veh))
                {
                    s_spawnedEscortVehicles.erase(s_spawnedEscortVehicles.begin() + i);
                    continue;
                }

                if (IsVehicleWrecked(veh) && !AnyAliveGuardAssignedTo(veh))
                {
                    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(veh, true, true);
                    RemoveEscortVehicleBlip(veh);
                    GTAvehicle gv(veh);
                    gv.Delete();
                    s_spawnedEscortVehicles.erase(s_spawnedEscortVehicles.begin() + i);
                    continue;
                }

                if (IsVehiclePhysicallyOccupied(veh) || AnyAliveGuardAssignedTo(veh))
                {
                    rec.emptySinceMs = 0;
                    ++i;
                    continue;
                }

                // Explicit user clear: delete as soon as the vehicle has emptied,
                // bypassing the 12 s grace (the grace only covers the automatic
                // tick purge). Occupied vehicles are still protected above.
                if (rec.deleteWhenEmpty)
                {
                    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(veh, true, true);
                    RemoveEscortVehicleBlip(veh);
                    GTAvehicle gv(veh);
                    gv.Delete();
                    s_spawnedEscortVehicles.erase(s_spawnedEscortVehicles.begin() + i);
                    continue;
                }

                if (rec.emptySinceMs == 0)
                {
                    rec.emptySinceMs = now;
                    ++i;
                    continue;
                }

                if (now - rec.emptySinceMs >= kEscortVehicleEmptyGraceMs)
                {
                    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(veh, true, true);
                    RemoveEscortVehicleBlip(veh);
                    GTAvehicle gv(veh);
                    gv.Delete();
                    s_spawnedEscortVehicles.erase(s_spawnedEscortVehicles.begin() + i);
                    continue;
                }

                ++i;
            }
        }

        void UnlockVehicleForBodyguards(Vehicle veh)
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

        bool IsSeatAvailableForPed(Ped ped, Vehicle veh, int seat)
        {
            Ped occupant = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, seat, false);
            return occupant == 0 ||
                occupant == ped ||
                !ENTITY::DOES_ENTITY_EXIST(occupant) ||
                PED::IS_PED_DEAD_OR_DYING(occupant, true);
        }

        void RemoveFromPlayerGroupForEscort(BodyguardEntity& bg)
        {
            if (!IsBodyguardAlive(bg) || bg.RemovedFromGroup)
                return;

            Ped ped = bg.Handle.GetHandle();
            PED::REMOVE_PED_FROM_GROUP(ped);
            bg.RemovedFromGroup = true;
        }

        void RestorePlayerGroupAfterEscort(BodyguardEntity& bg)
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

        bool ForcePedIntoSeat(Ped ped, Vehicle veh, int seat)
        {
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                if (!ENTITY::DOES_ENTITY_EXIST(ped) || !EntityVehicleExists(veh))
                    return false;

                UnlockVehicleForBodyguards(veh);
                if (!IsSeatAvailableForPed(ped, veh, seat))
                    return false;

                TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped);
                TASK::TASK_WARP_PED_INTO_VEHICLE(ped, veh, seat);
                PED::SET_PED_INTO_VEHICLE(ped, veh, seat);

                if (VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, seat, false) == ped)
                    return true;

                WAIT(0);
            }

            return VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, seat, false) == ped;
        }

        bool AdoptExistingSeat(BodyguardEntity& bg, Vehicle veh)
        {
            if (!IsBodyguardAlive(bg) || !EntityVehicleExists(veh))
                return false;

            int seat = GetPedSeatInVehicle(bg.Handle.GetHandle(), veh);
            if (seat == -2)
                return false;

            RemoveFromPlayerGroupForEscort(bg);
            PED::SET_PED_NEVER_LEAVES_GROUP(bg.Handle.GetHandle(), false);
            PED::SET_PED_KEEP_TASK(bg.Handle.GetHandle(), true);
            bg.EscortVehicle = GTAentity(veh);
            bg.EscortSeat = seat;
            bg.DriverTasked = false;
            bg.LastEscortPlayerInVehicle = false;
            bg.LastEscortTargetVehicle = 0;
            bg.NextReboardAt = 0;
            bg.ReboardAttempts = 0;
            return true;
        }

        bool SeatBodyguard(BodyguardEntity& bg, Vehicle veh, int seat)
        {
            if (!IsBodyguardAlive(bg) || !EntityVehicleExists(veh))
                return false;

            Ped ped = bg.Handle.GetHandle();
            bg.Handle.RequestControl();
            UnlockVehicleForBodyguards(veh);

            bool wasRemoved = bg.RemovedFromGroup;
            RemoveFromPlayerGroupForEscort(bg);
            TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped);
            PED::SET_PED_NEVER_LEAVES_GROUP(ped, false);
            PED::SET_PED_KEEP_TASK(ped, true);

            Vector3 vehPos = ENTITY::GET_ENTITY_COORDS(veh, TRUE);
            Vector3 pedPos = ENTITY::GET_ENTITY_COORDS(ped, TRUE);
            if (g_escortRealisticBoarding && DistanceSq(pedPos, vehPos) < 25.f * 25.f)
            {
                TASK::TASK_ENTER_VEHICLE(ped, veh, 8000, seat, 2.0f, 1, 0);
                bg.EscortVehicle = GTAentity(veh);
                bg.EscortSeat = seat;
                bg.DriverTasked = false;
                bg.LastEscortPlayerInVehicle = false;
                bg.LastEscortTargetVehicle = 0;
                bg.NextReboardAt = MISC::GET_GAME_TIMER() + 8000;
                bg.ReboardAttempts = 0;
                return true;
            }

            if (!ForcePedIntoSeat(ped, veh, seat))
            {
                if (!wasRemoved)
                    RestorePlayerGroupAfterEscort(bg);
                return false;
            }

            bg.EscortVehicle = GTAentity(veh);
            bg.EscortSeat = seat;
            bg.DriverTasked = false;
            bg.LastEscortPlayerInVehicle = false;
            bg.LastEscortTargetVehicle = 0;
            bg.NextReboardAt = 0;
            bg.ReboardAttempts = 0;
            return true;
        }

        bool TrySeatAnyPending(std::vector<BodyguardEntity*>& pending, Vehicle veh, int seat)
        {
            for (size_t i = 0; i < pending.size(); ++i)
            {
                if (SeatBodyguard(*pending[i], veh, seat))
                {
                    pending.erase(pending.begin() + i);
                    return true;
                }
            }

            return false;
        }

        int FillPassengerSeats(Vehicle veh, std::vector<BodyguardEntity*>& pending)
        {
            if (!EntityVehicleExists(veh) || pending.empty())
                return 0;

            int assigned = 0;
            int maxPassengers = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(veh);
            UnlockVehicleForBodyguards(veh);

            for (int seat = 0; seat < maxPassengers && !pending.empty(); ++seat)
            {
                if (!IsSeatAvailableForPed(0, veh, seat))
                    continue;

                if (TrySeatAnyPending(pending, veh, seat))
                    ++assigned;
            }

            return assigned;
        }

        bool FillDriverSeat(Vehicle veh, std::vector<BodyguardEntity*>& pending)
        {
            if (!EntityVehicleExists(veh) || pending.empty() || !IsSeatAvailableForPed(0, veh, -1))
                return false;

            for (int pass = 0; pass < 2; ++pass)
            {
                for (size_t i = 0; i < pending.size(); ++i)
                {
                    if (pass == 0 && pending[i]->Role != BodyguardRole::Driver)
                        continue;

                    if (SeatBodyguard(*pending[i], veh, -1))
                    {
                        pending.erase(pending.begin() + i);
                        return true;
                    }
                }
            }

            return false;
        }

        Vehicle SpawnOneEscortVehicle(GTAped& player, const GTAmodel::Model& m, int convoyIndex)
        {
            // First vehicle 6 m ahead of the player; later ones queue up BEHIND,
            // starting 8 m back. (A naive "6 - 8*i" would put vehicle #2 at -2 m,
            // overlapping the player / the player's own vehicle, which spans ~±2.5 m.)
            const float offsetY = (convoyIndex == 0) ? 6.f : -8.f * (float)convoyIndex;
            Vector3 pos = player.GetOffsetInWorldCoords(Vector3(0.f, offsetY, 0.f));
            float heading = player.GetHeading();
            Vehicle escortVeh = VEHICLE::CREATE_VEHICLE(m.hash, pos.x, pos.y, pos.z, heading, true, true, false);
            if (escortVeh == 0 || !ENTITY::DOES_ENTITY_EXIST(escortVeh))
                return 0;

            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(escortVeh, true, true);
            UnlockVehicleForBodyguards(escortVeh);
            ApplyEscortVehicleGodmode(escortVeh);
            VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(escortVeh, 5.0f);
            VEHICLE::SET_VEHICLE_ENGINE_ON(escortVeh, true, true, false);
            s_spawnedEscortVehicles.push_back({ escortVeh, 0 });
            AddEscortVehicleBlip(escortVeh);
            WAIT(0);
            return escortVeh;
        }

        Vehicle SpawnOneEscortHelicopter(GTAped& player, const GTAmodel::Model& m)
        {
            Vector3 pos = player.GetOffsetInWorldCoords(Vector3(0.f, -35.f, 35.f));
            float heading = player.GetHeading();
            Vehicle heli = VEHICLE::CREATE_VEHICLE(m.hash, pos.x, pos.y, pos.z, heading, true, true, false);
            if (!EntityVehicleExists(heli))
                return 0;

            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(heli, true, true);
            UnlockVehicleForBodyguards(heli);
            ApplyEscortVehicleGodmode(heli);
            VEHICLE::SET_VEHICLE_ENGINE_ON(heli, true, true, false);
            VEHICLE::SET_HELI_BLADES_FULL_SPEED(heli);
            s_spawnedEscortVehicles.push_back({ heli, 0 });
            AddEscortVehicleBlip(heli);
            WAIT(0);
            return heli;
        }

        bool IsTrackedAliveBodyguard(Ped ped)
        {
            for (auto& bg : BodyguardDb)
            {
                if (IsBodyguardAlive(bg) && bg.Handle.GetHandle() == ped)
                    return true;
            }

            return false;
        }

        bool EnsureEscortVehicleDriver(Vehicle veh)
        {
            if (!EntityVehicleExists(veh))
                return false;

            Ped driver = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, -1, false);
            if (driver != 0 && ENTITY::DOES_ENTITY_EXIST(driver) && !PED::IS_PED_DEAD_OR_DYING(driver, true))
                return true;

            BodyguardEntity* fallback = nullptr;
            BodyguardEntity* preferred = nullptr;
            for (auto& bg : BodyguardDb)
            {
                if (!IsBodyguardAlive(bg) ||
                    !bg.EscortVehicle.Exists() ||
                    bg.EscortVehicle.GetHandle() != veh ||
                    !IsPedInVehicle(bg.Handle.GetHandle(), veh))
                    continue;

                if (bg.Role == BodyguardRole::Driver)
                    preferred = &bg;
                if (!fallback)
                    fallback = &bg;
            }

            BodyguardEntity* picked = preferred ? preferred : fallback;
            if (!picked)
                return false;

            return SeatBodyguard(*picked, veh, -1);
        }

        bool IsDriverTaskCurrent(const BodyguardEntity& bg, bool playerInVehicle, Vehicle targetVeh)
        {
            return bg.DriverTasked &&
                bg.LastEscortPlayerInVehicle == playerInVehicle &&
                bg.LastEscortTargetVehicle == targetVeh;
        }

        void MarkDriverTaskState(BodyguardEntity& bg, bool playerInVehicle, Vehicle targetVeh)
        {
            bg.DriverTasked = true;
            bg.LastEscortPlayerInVehicle = playerInVehicle;
            bg.LastEscortTargetVehicle = targetVeh;
        }

        // "Busy fighting" must not rely on IS_PED_IN_COMBAT(ped, player): a guard is
        // never in combat WITH the player, so that always returns false. Combine the
        // any-target form with direct activity checks instead.
        bool IsPedBusyFighting(Ped ped)
        {
            return PED::IS_PED_IN_COMBAT(ped, 0) ||
                PED::IS_PED_SHOOTING(ped) ||
                PED::IS_PED_IN_MELEE_COMBAT(ped);
        }

        void QueueReboardOrClear(BodyguardEntity& bg, Vehicle veh, int now, Ped playerPed)
        {
            (void)playerPed;
            if (!g_escortReboardAfterCombat || IsPedBusyFighting(bg.Handle.GetHandle()))
                return;

            if (now < bg.NextReboardAt)
                return;

            bg.NextReboardAt = now + 2500;
            ++bg.ReboardAttempts;

            Ped ped = bg.Handle.GetHandle();
            bg.Handle.RequestControl();
            UnlockVehicleForBodyguards(veh);
            PED::SET_PED_KEEP_TASK(ped, true);

            if (VEHICLE::IS_THIS_MODEL_A_HELI(ENTITY::GET_ENTITY_MODEL(veh)) &&
                !VEHICLE::IS_VEHICLE_ON_ALL_WHEELS(veh))
            {
                if (IsSeatAvailableForPed(ped, veh, bg.EscortSeat) &&
                    ForcePedIntoSeat(ped, veh, bg.EscortSeat))
                {
                    bg.ReboardAttempts = 0;
                    bg.DriverTasked = false;
                    return;
                }

                if (bg.ReboardAttempts > 3)
                    ClearEscortState(bg);
                return;
            }

            if (bg.ReboardAttempts >= 3 && IsSeatAvailableForPed(ped, veh, bg.EscortSeat))
            {
                if (ForcePedIntoSeat(ped, veh, bg.EscortSeat))
                {
                    bg.ReboardAttempts = 0;
                    bg.DriverTasked = false;
                    return;
                }
            }
            else
            {
                TASK::TASK_ENTER_VEHICLE(ped, veh, 3000, bg.EscortSeat, 2.0f, 1, 0);
            }

            if (bg.ReboardAttempts > 6)
                ClearEscortState(bg);
        }

        int CountAssignedBodyguards()
        {
            int count = 0;
            for (auto& bg : BodyguardDb)
            {
                if (IsBodyguardAlive(bg) &&
                    bg.EscortVehicle.Exists() &&
                    EntityVehicleExists(bg.EscortVehicle.GetHandle()) &&
                    IsPedInVehicle(bg.Handle.GetHandle(), bg.EscortVehicle.GetHandle()))
                    ++count;
            }

            return count;
        }

        int SpawnEscortHelicopter()
        {
            GTAmodel::Model m(g_escortHeliModel);
            if (!m.IsInCdImage() || !m.IsVehicle() || !VEHICLE::IS_THIS_MODEL_A_HELI(m.hash) || !m.Load(4000))
            {
                Game::Print::PrintBottomCentre("~r~" + Language::TranslateToSelected("Invalid vehicle model"));
                return 0;
            }

            Ped playerPed = PLAYER::PLAYER_PED_ID();
            GTAped player(playerPed);
            Vehicle heli = SpawnOneEscortHelicopter(player, m);
            m.Unload();
            if (!EntityVehicleExists(heli))
                return 0;

            std::vector<BodyguardEntity*> pending;
            for (auto& bg : BodyguardDb)
            {
                if (!IsBodyguardAlive(bg) || bg.EscortVehicle.Exists() || bg.HoldPosition || IsBusyBodyguard(bg.Handle.GetHandle()))
                    continue;
                pending.push_back(&bg);
            }

            if (pending.empty())
                return 0;

            FillDriverSeat(heli, pending);
            FillPassengerSeats(heli, pending);
            return CountAssignedBodyguards();
        }
    }

    void CleanupEscortVehicles()
    {
        DeleteEmptySpawnedEscortVehicles();
    }

    bool HasActiveEscortConvoy()
    {
        PurgeSpawnedEscortVehicles();
        if (!s_spawnedEscortVehicles.empty())
            return true;

        for (auto& bg : BodyguardDb)
        {
            if (IsBodyguardAlive(bg) && bg.EscortVehicle.Exists())
                return true;
        }

        return false;
    }

    void AssignBodyguardsToEscort()
    {
        PurgeSpawnedEscortVehicles();
        DeleteEmptySpawnedEscortVehicles();

        // A new assignment re-arms normal purge rules on surviving spawned
        // vehicles (an Assign after a Clear reuses them, grace included).
        for (auto& rec : s_spawnedEscortVehicles)
            rec.deleteWhenEmpty = false;

        Ped playerPed = PLAYER::PLAYER_PED_ID();
        GTAped player(playerPed);
        Vehicle playerVeh = 0;
        if (player.IsInVehicle())
            playerVeh = player.CurrentVehicle().GetHandle();

        std::vector<BodyguardEntity*> pending;
        for (auto& bg : BodyguardDb)
        {
            if (!IsBodyguardAlive(bg))
                continue;
            if (bg.HoldPosition || IsBusyBodyguard(bg.Handle.GetHandle()))
                continue;

            if (bg.EscortVehicle.Exists())
            {
                Vehicle assignedVeh = bg.EscortVehicle.GetHandle();
                if (EntityVehicleExists(assignedVeh) && IsPedInVehicle(bg.Handle.GetHandle(), assignedVeh))
                    continue;

                ClearEscortState(bg);
            }

            if (g_escortUseMySeatsFirst && EntityVehicleExists(playerVeh) && IsPedInVehicle(bg.Handle.GetHandle(), playerVeh))
            {
                AdoptExistingSeat(bg, playerVeh);
                continue;
            }

            pending.push_back(&bg);
        }

        const size_t initialPending = pending.size();
        if (pending.empty())
        {
            Game::Print::PrintBottomCentre("No bodyguards to assign");
            return;
        }

        if (g_escortSpawnIfFull || !s_spawnedEscortVehicles.empty())
        {
            std::stable_partition(pending.begin(), pending.end(),
                [](const BodyguardEntity* bg)
                {
                    return bg->Role != BodyguardRole::Driver;
                });
        }

        if (g_escortUseMySeatsFirst && EntityVehicleExists(playerVeh))
            FillPassengerSeats(playerVeh, pending);

        for (auto& rec : s_spawnedEscortVehicles)
        {
            Vehicle veh = rec.veh;
            if (pending.empty())
                break;

            if (!EntityVehicleExists(veh))
                continue;

            FillDriverSeat(veh, pending);
            FillPassengerSeats(veh, pending);
        }

        if (!pending.empty() && g_escortSpawnIfFull)
        {
            GTAmodel::Model m(g_escortVehicleModel);
            if (m.IsInCdImage() && m.IsVehicle() && m.Load(4000))
            {
                int spawnedVehicles = 0;
                const int kMaxEscortVehiclesPerAssign = 4;
                while (!pending.empty() && spawnedVehicles < kMaxEscortVehiclesPerAssign)
                {
                    const size_t before = pending.size();
                    Vehicle escortVeh = SpawnOneEscortVehicle(player, m, spawnedVehicles);
                    if (!EntityVehicleExists(escortVeh))
                        break;
                    ++spawnedVehicles;

                    FillDriverSeat(escortVeh, pending);
                    FillPassengerSeats(escortVeh, pending);

                    if (pending.size() == before)
                    {
                        RemoveEscortVehicleBlip(escortVeh);
                        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(escortVeh, true, true);
                        GTAvehicle(escortVeh).Delete();
                        s_spawnedEscortVehicles.erase(
                            std::remove_if(s_spawnedEscortVehicles.begin(), s_spawnedEscortVehicles.end(),
                                [escortVeh](const SpawnedEscortVehicle& rec)
                                {
                                    return rec.veh == escortVeh;
                                }),
                            s_spawnedEscortVehicles.end());
                        break;
                    }
                }
            }
            else
            {
                Game::Print::PrintBottomCentre("~r~" + Language::TranslateToSelected("Invalid vehicle model"));
            }
            m.Unload();
        }

        const size_t assigned = initialPending - pending.size();
        if (assigned == 0)
            Game::Print::PrintBottomCentre("No free escort seats");
        else if (assigned < initialPending)
            Game::Print::PrintBottomCentre("~r~" + Language::TranslateToSelected("Bodyguards assigned: ") + std::to_string(assigned) + "/" + std::to_string(initialPending));
        else
            Game::Print::PrintBottomLeft(Language::TranslateToSelected("Bodyguards assigned: ") + std::to_string(assigned) + "/" + std::to_string(initialPending));
    }

    void TickEscort()
    {
        PurgeSpawnedEscortVehicles();
        DeleteEmptySpawnedEscortVehicles();
        ReapplyEscortGodmodeIfChanged();

        Ped playerPed = PLAYER::PLAYER_PED_ID();
        GTAped player(playerPed);
        bool playerInVehicle = player.IsInVehicle();
        Vehicle playerVeh = 0;
        if (playerInVehicle)
            playerVeh = player.CurrentVehicle().GetHandle();

        const int now = MISC::GET_GAME_TIMER();

        static Vehicle s_lastPlayerVeh = 0;
        static int s_playerVehChangedAt = 0;
        if (playerVeh != s_lastPlayerVeh)
        {
            if (s_lastPlayerVeh != 0 && !IsSpawnedEscortVehicle(s_lastPlayerVeh))
            {
                for (auto& bg : BodyguardDb)
                {
                    if (bg.EscortVehicle.Exists() && bg.EscortVehicle.GetHandle() == s_lastPlayerVeh)
                    {
                        if (IsBodyguardAlive(bg) && IsPedInVehicle(bg.Handle.GetHandle(), s_lastPlayerVeh))
                        {
                            TASK::TASK_LEAVE_VEHICLE(bg.Handle.GetHandle(), s_lastPlayerVeh, 0);
                            PED::SET_PED_KEEP_TASK(bg.Handle.GetHandle(), true);
                        }
                        ClearEscortState(bg);
                    }
                }
            }
            s_lastPlayerVeh = playerVeh;
            s_playerVehChangedAt = now;
        }

        if (playerVeh != 0 && s_playerVehChangedAt != 0 &&
            now - s_playerVehChangedAt > 2000)
        {
            s_playerVehChangedAt = 0;
            if (g_escortAutoAssignNewSpawns)
                AssignBodyguardsToEscort();
        }

        int drivingStyle = 0;
        if (g_escortDrivingStyleIndex >= 0 &&
            g_escortDrivingStyleIndex < (int)DrivingStyle::nameArray.size())
            drivingStyle = DrivingStyle::nameArray[g_escortDrivingStyleIndex].style;

        static int s_playerOnFootSince = 0;
        if (playerInVehicle)
            s_playerOnFootSince = 0;
        else if (s_playerOnFootSince == 0)
            s_playerOnFootSince = now;
        const bool playerLongOnFoot =
            !playerInVehicle && (now - s_playerOnFootSince) > 8000;

        for (auto& rec : s_spawnedEscortVehicles)
        {
            Vehicle veh = rec.veh;
            EnsureEscortVehicleDriver(veh);
        }

        if (g_escortCatchupTeleport)
        {
            static std::map<Vehicle, int> s_lastVehicleWarpAt;
            constexpr float kCatchupDistSq = 150.f * 150.f;
            for (auto& rec : s_spawnedEscortVehicles)
            {
                Vehicle veh = rec.veh;
                if (!EntityVehicleExists(veh))
                    continue;
                // Never ground-warp a helicopter: the road-node warp + on-ground
                // snap would slam a flying heli onto the street (or into a tunnel).
                // Its follow mission flies it back on its own.
                if (VEHICLE::IS_THIS_MODEL_A_HELI(ENTITY::GET_ENTITY_MODEL(veh)))
                    continue;
                Ped driver = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, -1, false);
                if (driver == 0 || !IsTrackedAliveBodyguard(driver))
                    continue;
                if (DistSq(veh, playerPed) <= kCatchupDistSq)
                    continue;
                if (ENTITY::IS_ENTITY_ON_SCREEN(veh))
                    continue;
                if (now - s_lastVehicleWarpAt[veh] < 5000)
                    continue;

                Vector3 behind = player.GetOffsetInWorldCoords(Vector3(0.f, -20.f, 0.f));
                Vector3_t warpPos{};
                float warpHeading = player.GetHeading();
                int numLanes = 0;
                if (PATHFIND::GET_NTH_CLOSEST_VEHICLE_NODE_WITH_HEADING(
                        behind.x, behind.y, behind.z, 1, &warpPos, &warpHeading, &numLanes, 1, 3.f, 0.f))
                {
                    ENTITY::SET_ENTITY_COORDS(veh, warpPos.x, warpPos.y, warpPos.z, false, false, false, false);
                    ENTITY::SET_ENTITY_HEADING(veh, warpHeading);
                }
                else
                {
                    ENTITY::SET_ENTITY_COORDS(veh, behind.x, behind.y, behind.z, false, false, false, false);
                    ENTITY::SET_ENTITY_HEADING(veh, player.GetHeading());
                }
                VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(veh, 5.0f);
                s_lastVehicleWarpAt[veh] = now;

                for (auto& bg : BodyguardDb)
                {
                    if (bg.EscortVehicle.Exists() && bg.EscortVehicle.GetHandle() == veh)
                        bg.DriverTasked = false;
                }
            }
        }

        for (auto& bg : BodyguardDb)
        {
            if (!IsBodyguardAlive(bg))
                continue;
            // Busy guards (active chauffeur / medic in care) and Hold Position
            // guards are legitimately RemovedFromGroup without an escort vehicle:
            // the orphan-state cleanup below must NOT catch them, or it would
            // put them back in the vanilla group every tick (task tug-of-war).
            if (IsBusyBodyguard(bg.Handle.GetHandle()) || bg.HoldPosition)
                continue;
            if (!bg.EscortVehicle.Exists())
            {
                if (bg.RemovedFromGroup || bg.EscortSeat != -2)
                    ClearEscortState(bg);
                continue;
            }

            Ped ped = bg.Handle.GetHandle();
            Vehicle veh = bg.EscortVehicle.GetHandle();
            if (!EntityVehicleExists(veh))
            {
                ClearEscortState(bg);
                continue;
            }

            if (IsVehicleWrecked(veh))
            {
                ClearEscortState(bg);
                continue;
            }

            if (!IsPedInVehicle(ped, veh))
            {
                QueueReboardOrClear(bg, veh, now, playerPed);
                continue;
            }

            bg.ReboardAttempts = 0;
            bg.NextReboardAt = 0;

            int physicalSeat = GetPedSeatInVehicle(ped, veh);
            if (physicalSeat != -2 && physicalSeat != bg.EscortSeat)
            {
                bg.EscortSeat = physicalSeat;
                bg.DriverTasked = false;
            }

            if (bg.EscortSeat != -1)
            {
                if (playerLongOnFoot && !IsSpawnedEscortVehicle(veh))
                {
                    Vector3 pp = ENTITY::GET_ENTITY_COORDS(playerPed, TRUE);
                    Vector3 vp = ENTITY::GET_ENTITY_COORDS(veh, TRUE);
                    float dx = pp.x - vp.x;
                    float dy = pp.y - vp.y;
                    float dz = pp.z - vp.z;
                    if (dx * dx + dy * dy + dz * dz > 15.0f * 15.0f)
                    {
                        TASK::TASK_LEAVE_VEHICLE(ped, veh, 0);
                        PED::SET_PED_KEEP_TASK(ped, true);
                        ClearEscortState(bg);
                    }
                }
                continue;
            }

            PED::SET_PED_NEVER_LEAVES_GROUP(ped, false);
            PED::SET_PED_KEEP_TASK(ped, true);

            Vehicle targetVeh = playerInVehicle ? playerVeh : 0;
            const bool isHeli = VEHICLE::IS_THIS_MODEL_A_HELI(ENTITY::GET_ENTITY_MODEL(veh));

            if (isHeli)
            {
                // Reuse the driver-task cache for the air branch too: re-issuing
                // TASK_HELI_MISSION every escort tick resets the heli AI (stutter).
                // Courtesy landing is cached with the sentinel targetVeh == veh
                // (never a legit follow key: landing only happens on foot, where
                // the follow key is (false, 0)); leaving/entering the landing
                // state changes the key, which re-tasks exactly once.
                if (playerLongOnFoot)
                {
                    if (IsDriverTaskCurrent(bg, false, veh))
                        continue;
                    Vector3 pos = ENTITY::GET_ENTITY_COORDS(playerPed, TRUE);
                    TASK::TASK_HELI_MISSION(ped, veh, 0, 0, pos.x, pos.y, pos.z, 20, 35.f, 40.f, -1.f, 80, 10, -1.f, 0);
                    MarkDriverTaskState(bg, false, veh);
                    continue;
                }

                if (IsDriverTaskCurrent(bg, playerInVehicle, targetVeh))
                    continue;
                TASK::TASK_HELI_MISSION(ped, veh, playerInVehicle ? playerVeh : 0, playerInVehicle ? 0 : playerPed, 0.f, 0.f, 0.f, 8, 35.f, 40.f, -1.f, 80, 40, -1.f, 0);
                MarkDriverTaskState(bg, playerInVehicle, targetVeh);
                continue;
            }

            if (IsDriverTaskCurrent(bg, playerInVehicle, targetVeh))
                continue;

            if (playerInVehicle && playerVeh != 0)
            {
                TASK::TASK_VEHICLE_ESCORT(ped, veh, playerVeh, -1, 30.f, drivingStyle, 8.f, 5, 20.f);
            }
            else
            {
                TASK::TASK_VEHICLE_FOLLOW(ped, veh, playerPed, 15.f, drivingStyle, 10);
            }

            MarkDriverTaskState(bg, playerInVehicle, targetVeh);
        }
    }

    void TickEscortCatchupOnFoot()
    {
        if (!g_escortCatchupTeleport)
            return;

        Ped playerPed = PLAYER::PLAYER_PED_ID();
        if (playerPed == 0 || !ENTITY::DOES_ENTITY_EXIST(playerPed))
            return;

        GTAped player(playerPed);
        constexpr float kCatchupDistSq = 150.f * 150.f;
        int spreadIndex = 0;
        for (auto& bg : BodyguardDb)
        {
            if (!IsBodyguardAlive(bg))
                continue;
            if (bg.EscortVehicle.Exists())
                continue;
            // Never warp a Hold Position sentinel (he is far away on purpose)
            // nor a busy guard (chauffeur boarding / medic walking to a patient).
            if (bg.HoldPosition || IsBusyBodyguard(bg.Handle.GetHandle()))
                continue;

            Ped ped = bg.Handle.GetHandle();
            if (PED::GET_VEHICLE_PED_IS_IN(ped, FALSE) != 0)
                continue;
            if (IsPedBusyFighting(ped))
                continue;
            if (DistSq(ped, playerPed) <= kCatchupDistSq)
                continue;
            if (ENTITY::IS_ENTITY_ON_SCREEN(ped))
                continue;

            float lateral = (spreadIndex % 2) ? 2.f : -2.f;
            float rear = -3.f - (float)(spreadIndex / 2);
            Vector3 warpPos = player.GetOffsetInWorldCoords(Vector3(lateral, rear, 0.f));
            ENTITY::SET_ENTITY_COORDS(ped, warpPos.x, warpPos.y, warpPos.z, false, false, false, false);
            ++spreadIndex;
        }
    }

    void ClearEscortState(BodyguardEntity& bg)
    {
        Vehicle veh = bg.EscortVehicle.Exists() ? bg.EscortVehicle.GetHandle() : 0;

        if (bg.Handle.Exists())
        {
            Ped ped = bg.Handle.GetHandle();
            // Only make the guard exit a mod-spawned escort vehicle. In the player's
            // own vehicle the guard can stay seated: once back in the group, the
            // vanilla group AI tolerates members riding with the leader.
            if (EntityVehicleExists(veh) && IsSpawnedEscortVehicle(veh) && IsPedInVehicle(ped, veh))
            {
                TASK::TASK_LEAVE_VEHICLE(ped, veh, 0);
                PED::SET_PED_KEEP_TASK(ped, true);
            }
            RestorePlayerGroupAfterEscort(bg);
        }
        else
        {
            bg.RemovedFromGroup = false;
        }

        bg.DriverTasked = false;
        bg.LastEscortPlayerInVehicle = false;
        bg.LastEscortTargetVehicle = 0;
        bg.NextReboardAt = 0;
        bg.ReboardAttempts = 0;
        bg.EscortVehicle = GTAentity(0);
        bg.EscortSeat = -2;

        DeleteEmptySpawnedEscortVehicles();
    }

    static int ClearAllEscortAssignments()
    {
        // Explicit user action: emptied module-spawned vehicles must disappear
        // immediately, without the 12 s tick-purge grace. A vehicle that still
        // has occupants is never deleted; it goes as soon as the last one exits.
        for (auto& rec : s_spawnedEscortVehicles)
            rec.deleteWhenEmpty = true;

        int cleared = 0;
        for (auto& bg : BodyguardDb)
        {
            if (!bg.EscortVehicle.Exists())
                continue;

            ClearEscortState(bg);
            ++cleared;
        }

        DeleteEmptySpawnedEscortVehicles();
        return cleared;
    }

    void BodyguardEscortMenu()
    {
        AddTitle("Escort Vehicle");

        const int assigned = CountAssignedBodyguards();
        const int alive = (int)std::count_if(BodyguardDb.begin(), BodyguardDb.end(),
            [](const BodyguardEntity& bg)
            {
                return IsBodyguardAlive(bg);
            });
        AddBreak(Language::TranslateToSelected("Assigned: ") + std::to_string(assigned) + "/" + std::to_string(alive) + " - " + g_escortVehicleModel);

        bool bAssign = false;
        AddOption("Assign Bodyguards To Escort", bAssign);
        if (bAssign)
            AssignBodyguardsToEscort();

        bool bClear = false;
        AddOption("Clear Escort Assignments", bClear);
        if (bClear)
        {
            int cleared = ClearAllEscortAssignments();
            Game::Print::PrintBottomLeft(Language::TranslateToSelected("Escort assignments cleared: ") + std::to_string(cleared));
        }

        bool bHeli = false;
        AddOption("Spawn Escort Helicopter", bHeli);
        if (bHeli)
        {
            int assigned = SpawnEscortHelicopter();
            Game::Print::PrintBottomLeft(Language::TranslateToSelected("Bodyguards assigned: ") + std::to_string(assigned));
        }

        AddBreak("--- Settings ---");
        AddToggle("Use My Seats First", g_escortUseMySeatsFirst);
        AddToggle("Spawn Escort Vehicle If Full", g_escortSpawnIfFull);
        AddToggle("Auto-Assign New Spawns", g_escortAutoAssignNewSpawns);
        AddToggle("Reboard After Combat", g_escortReboardAfterCombat);
        AddToggle("Escort Vehicle Godmode", g_escortVehicleGodmode);
        AddToggle("Catch-Up Teleport", g_escortCatchupTeleport);
        AddToggle("Realistic Boarding", g_escortRealisticBoarding);

        std::vector<std::string> modelChoices = kEscortQuickModels;
        int modelIndex = -1;
        for (size_t i = 0; i < kEscortQuickModels.size(); ++i)
        {
            if (kEscortQuickModels[i] == g_escortVehicleModel)
            {
                modelIndex = (int)i;
                break;
            }
        }
        if (modelIndex < 0)
        {
            modelChoices.push_back(g_escortVehicleModel);
            modelIndex = (int)modelChoices.size() - 1;
        }

        bool mA = false, mR = false, mL = false;
        AddTexter("Escort Vehicle Model", modelIndex, modelChoices, mA, mR, mL);
        if (mR || mL)
        {
            int newIndex = modelIndex + (mR ? 1 : -1);
            if (newIndex < 0) newIndex = (int)modelChoices.size() - 1;
            if (newIndex >= (int)modelChoices.size()) newIndex = 0;
            g_escortVehicleModel = modelChoices[newIndex];
        }

        bool bCustom = false;
        AddOption("Custom Model...", bCustom);
        if (bCustom)
        {
            std::string input = Game::InputBox("", 64, "Escort Vehicle Model", g_escortVehicleModel);
            if (!input.empty())
            {
                GTAmodel::Model m(input);
                if (m.IsInCdImage() && m.IsVehicle())
                {
                    g_escortVehicleModel = input;
                    Game::Print::PrintBottomLeft("Escort model set");
                }
                else
                {
                    Game::Print::PrintBottomCentre("~r~" + Language::TranslateToSelected("Invalid vehicle model"));
                }
            }
        }

        std::vector<std::string> heliChoices = kEscortHeliQuickModels;
        int heliIndex = -1;
        for (size_t i = 0; i < kEscortHeliQuickModels.size(); ++i)
        {
            if (kEscortHeliQuickModels[i] == g_escortHeliModel)
            {
                heliIndex = (int)i;
                break;
            }
        }
        if (heliIndex < 0)
        {
            heliChoices.push_back(g_escortHeliModel);
            heliIndex = (int)heliChoices.size() - 1;
        }

        bool hA = false, hR = false, hL = false;
        AddTexter("Escort Helicopter Model", heliIndex, heliChoices, hA, hR, hL);
        if (hR || hL)
        {
            int newIndex = heliIndex + (hR ? 1 : -1);
            if (newIndex < 0) newIndex = (int)heliChoices.size() - 1;
            if (newIndex >= (int)heliChoices.size()) newIndex = 0;
            g_escortHeliModel = heliChoices[newIndex];
        }

        std::vector<std::string> styleNames;
        styleNames.reserve(DrivingStyle::nameArray.size());
        for (const auto& ds : DrivingStyle::nameArray)
            styleNames.push_back(ds.name);

        if (g_escortDrivingStyleIndex < 0)
            g_escortDrivingStyleIndex = 0;
        if (g_escortDrivingStyleIndex >= (int)styleNames.size())
            g_escortDrivingStyleIndex = (int)styleNames.size() - 1;

        bool sA = false, sR = false, sL = false;
        AddTexter("Escort Driving Style", g_escortDrivingStyleIndex, styleNames, sA, sR, sL);
        if (sR)
        {
            g_escortDrivingStyleIndex++;
            if (g_escortDrivingStyleIndex >= (int)styleNames.size())
                g_escortDrivingStyleIndex = 0;
        }
        else if (sL)
        {
            g_escortDrivingStyleIndex--;
            if (g_escortDrivingStyleIndex < 0)
                g_escortDrivingStyleIndex = (int)styleNames.size() - 1;
        }
    }
}

#include "..\..\Menu\submenu_switch.h"
#include "..\..\Menu\submenu_enum.h"
REGISTER_SUBMENU(BODYGUARD_ESCORT, sub::BodyguardMenu::BodyguardEscortMenu)
