#include "BodyguardCombat.h"

#include "../../Natives/natives.h"
#include "../../Natives/natives2.h"

#include "../../Scripting/World.h"
#include "../../Scripting/GTAped.h"

#include "BodyguardManagement.h"
#include "BodyguardDebug.h"
#include "BodyguardRole.h"
#include "BodyguardChauffeur.h"
#include "BodyguardTick.h"

#include <cfloat>

namespace sub::BodyguardMenu
{
    // 0 = off, 1 = player target, 2 = retaliate, 3 = wanted threats,
    // 4 = full protection. Persisted in menyooConfig.ini [bodyguards].
    int g_combatResponseMode = 4;
    int g_wantedThreatMinStars = 2;
}

namespace
{
    using namespace sub::BodyguardMenu;
    static Ped s_squadTarget = 0;
    static Ped s_pendingTarget = 0;
    static int s_pendingPriority = 99;
    static int s_combatSuppressedUntil = 0;

    // Returns true if the given ped must NOT be attacked by bodyguards
    // (the player, another player, a group member, a tracked bodyguard, or a
    // ped on friendly terms with the player). Logic compares handles / enums
    // only — never translated labels.
    bool IsFriendlyToPlayer(Ped targetPed, Ped playerPed)
    {
        if (targetPed == 0 || !ENTITY::DOES_ENTITY_EXIST(targetPed))
            return true; // nothing to attack -> treat as friendly (reject)
        if (PED::IS_PED_DEAD_OR_DYING(targetPed, true) || ENTITY::GET_ENTITY_HEALTH(targetPed) <= 0)
            return true;

        if (targetPed == playerPed)
            return true;
        if (PED::IS_PED_A_PLAYER(targetPed))
            return true;

        Player player = PLAYER::PLAYER_ID();
        if (PED::IS_PED_GROUP_MEMBER(targetPed, PLAYER::GET_PLAYER_GROUP(player)))
            return true;

        // Tracked bodyguard?
        if (sub::BodyguardMenu::BodyguardManagement::GetBodyguardIndexInDb(GTAentity(targetPed)) >= 0)
            return true;

        // Relationship: 0 = Companion, 1 = Respect, 2 = Like -> do not attack.
        int rel = PED::GET_RELATIONSHIP_BETWEEN_PEDS(targetPed, playerPed);
        if (rel == 0 || rel == 1 || rel == 2)
            return true;

        return false;
    }

    // For a vehicle target, reject if ANY occupant is friendly (player / group
    // member / tracked bodyguard). Iterates seats -1 (driver) .. maxPassengers.
    bool VehicleHasFriendlyOccupant(Vehicle veh, Ped playerPed)
    {
        if (veh == 0 || !ENTITY::DOES_ENTITY_EXIST(veh))
            return false;

        int maxPass = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(veh);
        for (int seat = -1; seat < maxPass; ++seat)
        {
            Ped occ = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, seat, FALSE);
            if (occ == 0 || !ENTITY::DOES_ENTITY_EXIST(occ))
                continue;
            if (IsFriendlyToPlayer(occ, playerPed))
                return true;
        }
        return false;
    }

    // Resolve a vehicle to an attackable occupant: driver first, then passengers.
    Ped FirstOccupant(Vehicle veh)
    {
        if (veh == 0 || !ENTITY::DOES_ENTITY_EXIST(veh))
            return 0;

        Ped driver = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, -1, FALSE);
        if (driver != 0 && ENTITY::DOES_ENTITY_EXIST(driver))
            return driver;

        int maxPass = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(veh);
        for (int seat = 0; seat < maxPass; ++seat)
        {
            Ped occ = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, seat, FALSE);
            if (occ != 0 && ENTITY::DOES_ENTITY_EXIST(occ))
                return occ;
        }
        return 0;
    }

    bool IsLawResponsePed(Ped ped)
    {
        if (ped == 0 || !ENTITY::DOES_ENTITY_EXIST(ped))
            return false;

        if (PED::IS_PED_IN_ANY_POLICE_VEHICLE(ped))
            return true;

        const Hash relGroup = PED::GET_PED_RELATIONSHIP_GROUP_HASH(ped);
        static const Hash kLawGroups[] = {
            GET_HASH_KEY("COP"),
            GET_HASH_KEY("ARMY"),
            GET_HASH_KEY("SECURITY_GUARD"),
            GET_HASH_KEY("PRIVATE_SECURITY")
        };

        for (Hash group : kLawGroups)
        {
            if (relGroup == group)
                return true;
        }

        const Hash model = ENTITY::GET_ENTITY_MODEL(ped);
        static const Hash kLawModels[] = {
            GET_HASH_KEY("s_m_y_cop_01"),
            GET_HASH_KEY("s_f_y_cop_01"),
            GET_HASH_KEY("s_m_m_cop_01"),
            GET_HASH_KEY("s_m_y_hwaycop_01"),
            GET_HASH_KEY("s_m_y_swat_01"),
            GET_HASH_KEY("s_m_m_snowcop_01"),
            GET_HASH_KEY("s_m_m_fibsec_01")
        };

        for (Hash lawModel : kLawModels)
        {
            if (model == lawModel)
                return true;
        }

        return false;
    }

    float DistanceSq(Entity a, Entity b)
    {
        Vector3 ap = ENTITY::GET_ENTITY_COORDS(a, TRUE);
        Vector3 bp = ENTITY::GET_ENTITY_COORDS(b, TRUE);
        float dx = ap.x - bp.x;
        float dy = ap.y - bp.y;
        float dz = ap.z - bp.z;
        return dx * dx + dy * dy + dz * dz;
    }

    bool IsBodyguardDriver(const BodyguardEntity& bg, Ped bgPed, Vehicle veh)
    {
        if (bg.EscortSeat == -1)
            return true;

        Ped driver = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, -1, FALSE);
        return driver != 0 && driver == bgPed;
    }

    bool IsPedStillFighting(Ped ped)
    {
        return PED::IS_PED_IN_COMBAT(ped, 0) ||
               PED::IS_PED_SHOOTING(ped) ||
               PED::IS_PED_IN_MELEE_COMBAT(ped);
    }

    void ProposeSquadTarget(Ped target, int priority)
    {
        if (target == 0)
            return;
        if (priority < s_pendingPriority)
        {
            s_pendingTarget = target;
            s_pendingPriority = priority;
        }
    }

    void TaskBodyguardOnTarget(BodyguardEntity& bg, Ped targetPed)
    {
        Ped bgPed = bg.Handle.GetHandle();
        if (bgPed == targetPed ||
            bgPed == 0 ||
            !ENTITY::DOES_ENTITY_EXIST(bgPed) ||
            targetPed == 0 ||
            !ENTITY::DOES_ENTITY_EXIST(targetPed) ||
            PED::IS_PED_DEAD_OR_DYING(targetPed, true))
            return;

        bg.Handle.RequestControl();

        if (bg.LastCombatTarget == targetPed && PED::IS_PED_IN_COMBAT(bgPed, targetPed))
            return;

        if (IsBusyBodyguard(bgPed))
            return;

        const BodyguardRoleDef& roleDef = RoleDef(bg.Role);
        Vehicle veh = PED::GET_VEHICLE_PED_IS_IN(bgPed, FALSE);
        if (veh != 0 && ENTITY::DOES_ENTITY_EXIST(veh))
        {
            Vector3 targetPos = ENTITY::GET_ENTITY_COORDS(targetPed, TRUE);
            VEHICLE::SET_VEHICLE_SHOOT_AT_TARGET(bgPed, targetPed, targetPos.x, targetPos.y, targetPos.z);

            if (IsBodyguardDriver(bg, bgPed, veh))
            {
                TASK::TASK_DRIVE_BY(bgPed, targetPed, 0, 0.f, 0.f, 0.f, 85.f, roleDef.accuracy, TRUE, roleDef.firingPattern);
            }
            else
            {
                TASK::TASK_VEHICLE_SHOOT_AT_PED(bgPed, targetPed, 20.f);
                TASK::TASK_DRIVE_BY(bgPed, targetPed, 0, 0.f, 0.f, 0.f, 85.f, roleDef.accuracy, TRUE, roleDef.firingPattern);
            }

            PED::SET_PED_KEEP_TASK(bgPed, TRUE);
            bg.CombatTasked = true;
            bg.LastCombatTarget = targetPed;
            bg.LastCombatTaskAt = MISC::GET_GAME_TIMER();
            return;
        }

        if (bg.EscortVehicle.Exists())
            return;

        TASK::TASK_COMBAT_PED(bgPed, targetPed, 0, 16);
        PED::SET_PED_KEEP_TASK(bgPed, TRUE);
        bg.CombatTasked = true;
        bg.LastCombatTarget = targetPed;
        bg.LastCombatTaskAt = MISC::GET_GAME_TIMER();
    }

    // Task every alive bodyguard (except the target itself) to attack targetPed.
    void TaskAllBodyguardsOnTargetInternal(Ped targetPed)
    {
        s_squadTarget = targetPed;
        Ped speaker = 0;
        for (auto& bg : sub::BodyguardMenu::BodyguardDb)
        {
            if (!sub::BodyguardMenu::IsBodyguardAlive(bg))
                continue;
            if (speaker == 0 && !IsBusyBodyguard(bg.Handle.GetHandle()))
                speaker = bg.Handle.GetHandle();
            TaskBodyguardOnTarget(bg, targetPed);
        }
        TryPlayBodyguardSpeech(speaker, (MISC::GET_GAME_TIMER() & 1) ? "GENERIC_INSULT_HIGH" : "COVER_ME");
    }

    void ReleaseFinishedCombatTasks(bool forceRelease)
    {
        static int s_nextCheck = 0;
        int now = MISC::GET_GAME_TIMER();
        if (now < s_nextCheck)
            return;
        s_nextCheck = now + 1000;

        const bool targetGone = s_squadTarget == 0 ||
            !ENTITY::DOES_ENTITY_EXIST(s_squadTarget) ||
            PED::IS_PED_DEAD_OR_DYING(s_squadTarget, true);
        if (!forceRelease && !targetGone)
            return;
        if (targetGone)
            s_squadTarget = 0;

        for (auto& bg : sub::BodyguardMenu::BodyguardDb)
        {
            if (!bg.CombatTasked)
                continue;
            if (!sub::BodyguardMenu::IsBodyguardAlive(bg))
            {
                bg.CombatTasked = false;
                bg.LastCombatTarget = 0;
                continue;
            }

            Ped ped = bg.Handle.GetHandle();
            if (!forceRelease && IsPedStillFighting(ped))
                continue;

            if (bg.EscortVehicle.Exists())
            {
                bg.DriverTasked = false;
                bg.CombatTasked = false;
                bg.LastCombatTarget = 0;
                continue;
            }

            bg.Handle.RequestControl();
            PED::SET_PED_KEEP_TASK(ped, FALSE);
            TASK::CLEAR_PED_TASKS(ped);
            bg.CombatTasked = false;
            bg.LastCombatTarget = 0;
        }
    }

    // MODE 1 - "Player Target": resolve the player's targeted/aimed entity and
    // sic the squad on it (unless friendly). Re-tasks only when the target changes
    // to avoid AI stutter.
    void TickShootOnTarget(Ped playerPed)
    {
        static Ped s_lastTarget = 0;
        static int s_lastTaskAt = 0;
        static int s_nextScan = 0;

        int now = MISC::GET_GAME_TIMER();
        if (now < s_nextScan)
            return;
        s_nextScan = now + 200;

        Player player = PLAYER::PLAYER_ID();
        Entity target = 0;
        if (!PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(player, &target))
            PLAYER::GET_PLAYER_TARGET_ENTITY(player, &target);

        if (target == 0 || !ENTITY::DOES_ENTITY_EXIST(target))
        {
            s_lastTarget = 0;
            return;
        }

        Ped targetPed = 0;
        if (ENTITY::IS_ENTITY_A_PED(target))
        {
            targetPed = target;
        }
        else if (ENTITY::IS_ENTITY_A_VEHICLE(target))
        {
            // Reject the whole vehicle if any occupant is friendly.
            if (VehicleHasFriendlyOccupant(target, playerPed))
                return;
            targetPed = FirstOccupant(target);
            if (targetPed == 0)
                return; // empty vehicle -> nothing to attack
        }
        else
        {
            return;
        }

        if (IsFriendlyToPlayer(targetPed, playerPed))
            return;

        if (targetPed == s_lastTarget && now - s_lastTaskAt < 3000)
            return;

        ProposeSquadTarget(targetPed, 0);
        s_lastTarget = targetPed;
        s_lastTaskAt = now;
    }

    // MODE 2 — "Retaliate": every ~500 ms, scan nearby peds for one that has just
    // damaged the player and sic the whole squad on it (excluding any friendly).
    // Self-contained via World::GetNearbyPeds (uses the world ped pool directly,
    // so it does not depend on the TickSubsystems globals).
    void TickRetaliate(Ped playerPed)
    {
        static int s_nextScan = 0;
        int now = MISC::GET_GAME_TIMER();
        if (now < s_nextScan)
            return;
        s_nextScan = now + 500;

        std::vector<GTAped> peds;
        World::GetNearbyPeds(peds, GTAped(playerPed), 60.0f, 128);
        for (auto& gp : peds)
        {
            Ped cand = gp.GetHandle();
            if (cand == 0 || cand == playerPed || !ENTITY::DOES_ENTITY_EXIST(cand))
                continue;
            if (IsFriendlyToPlayer(cand, playerPed))
                continue;
            // HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(player, candidate): did this ped hurt us?
            if (ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(playerPed, cand, TRUE))
            {
                ProposeSquadTarget(cand, 1);
                ENTITY::CLEAR_ENTITY_LAST_DAMAGE_ENTITY(playerPed);
                break;
            }
        }
    }

    Ped FindWantedThreat(Ped playerPed)
    {
        if (PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) < g_wantedThreatMinStars)
            return 0;

        std::vector<GTAped> peds;
        World::GetNearbyPeds(peds, GTAped(playerPed), 90.0f, 128);

        Ped best = 0;
        float bestScore = FLT_MAX;
        for (auto& gp : peds)
        {
            Ped cand = gp.GetHandle();
            if (cand == 0 || cand == playerPed || !ENTITY::DOES_ENTITY_EXIST(cand))
                continue;
            if (IsFriendlyToPlayer(cand, playerPed))
                continue;

            const bool damagedPlayer = ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(playerPed, cand, TRUE) != 0;
            const bool fightingPlayer = PED::IS_PED_IN_COMBAT(cand, playerPed) != 0 || PED::IS_PED_SHOOTING(cand) != 0;
            const bool lawResponse = IsLawResponsePed(cand);

            if (!damagedPlayer && !fightingPlayer && !lawResponse)
                continue;

            float priority = damagedPlayer ? 0.f : (fightingPlayer ? 1.f : 2.f);
            float score = priority * 1000000.f + DistanceSq(playerPed, cand);
            if (score < bestScore)
            {
                bestScore = score;
                best = cand;
            }
        }

        return best;
    }

    void TickWantedThreats(Ped playerPed)
    {
        static int s_nextScan = 0;
        static Ped s_lastTarget = 0;
        static int s_lastTaskAt = 0;

        int now = MISC::GET_GAME_TIMER();
        if (now < s_nextScan)
            return;
        s_nextScan = now + 700;

        Ped target = FindWantedThreat(playerPed);
        if (target == 0)
        {
            s_lastTarget = 0;
            return;
        }

        if (target == s_lastTarget && now - s_lastTaskAt < 2500)
            return;

        ProposeSquadTarget(target, 2);
        s_lastTarget = target;
        s_lastTaskAt = now;
    }
}

namespace sub::BodyguardMenu
{
    Entity ResolvePlayerTargetEntity()
    {
        Ped playerPed = PLAYER::PLAYER_PED_ID();
        if (playerPed == 0 || !ENTITY::DOES_ENTITY_EXIST(playerPed))
            return 0;

        Player player = PLAYER::PLAYER_ID();
        Entity target = 0;
        if (!PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(player, &target))
            PLAYER::GET_PLAYER_TARGET_ENTITY(player, &target);

        if (target == 0 || !ENTITY::DOES_ENTITY_EXIST(target))
            return 0;

        if (ENTITY::IS_ENTITY_A_PED(target))
        {
            Ped ped = target;
            return IsFriendlyToPlayer(ped, playerPed) ? 0 : ped;
        }

        if (ENTITY::IS_ENTITY_A_VEHICLE(target))
        {
            if (VehicleHasFriendlyOccupant(target, playerPed))
                return 0;
            Ped occupant = FirstOccupant(target);
            return IsFriendlyToPlayer(occupant, playerPed) ? 0 : occupant;
        }

        return 0;
    }

    void TaskAllBodyguardsOnTarget(Entity target)
    {
        if (target == 0 || !ENTITY::DOES_ENTITY_EXIST(target))
            return;

        Ped playerPed = PLAYER::PLAYER_PED_ID();
        Ped targetPed = 0;
        if (ENTITY::IS_ENTITY_A_PED(target))
            targetPed = target;
        else if (ENTITY::IS_ENTITY_A_VEHICLE(target))
        {
            if (VehicleHasFriendlyOccupant(target, playerPed))
                return;
            targetPed = FirstOccupant(target);
        }

        if (targetPed == 0 || IsFriendlyToPlayer(targetPed, playerPed))
            return;

        TaskAllBodyguardsOnTargetInternal(targetPed);
    }

    void CeaseFireAll()
    {
        const int now = MISC::GET_GAME_TIMER();
        s_combatSuppressedUntil = now + 5000;
        s_squadTarget = 0;
        s_pendingTarget = 0;
        s_pendingPriority = 99;

        for (auto& bg : BodyguardDb)
        {
            bg.CombatTasked = false;
            bg.LastCombatTarget = 0;
            bg.LastCombatTaskAt = 0;

            if (!IsBodyguardAlive(bg))
                continue;

            Ped ped = bg.Handle.GetHandle();
            // Never clear the tasks of a busy guard (active chauffeur / medic in
            // care): they are not fighting, and clearing would silently drop their
            // driving/heal task with no re-issue path.
            if (IsBusyBodyguard(ped))
                continue;

            bg.Handle.RequestControl();
            PED::SET_PED_KEEP_TASK(ped, FALSE);
            TASK::CLEAR_PED_TASKS(ped);
            if (bg.EscortVehicle.Exists())
                bg.DriverTasked = false;
        }
    }

    void TickCombatResponse()
    {
        // Nothing to command if no bodyguards are tracked.
        if (sub::BodyguardMenu::BodyguardDb.empty())
            return;

        ReleaseFinishedCombatTasks(g_combatResponseMode == 0);
        if (g_combatResponseMode == 0)
        {
            s_pendingTarget = 0;
            s_pendingPriority = 99;
            return;
        }

        if (MISC::GET_GAME_TIMER() < s_combatSuppressedUntil)
        {
            s_pendingTarget = 0;
            s_pendingPriority = 99;
            return;
        }

        Ped playerPed = PLAYER::PLAYER_PED_ID();
        if (playerPed == 0 || !ENTITY::DOES_ENTITY_EXIST(playerPed))
            return;

        if (g_combatResponseMode == 1)
            TickShootOnTarget(playerPed);
        else if (g_combatResponseMode == 2)
            TickRetaliate(playerPed);
        else if (g_combatResponseMode == 3)
            TickWantedThreats(playerPed);
        else if (g_combatResponseMode >= 4)
        {
            TickShootOnTarget(playerPed);
            TickRetaliate(playerPed);
            TickWantedThreats(playerPed);
        }

        if (s_pendingTarget != 0)
        {
            TaskAllBodyguardsOnTargetInternal(s_pendingTarget);
            s_pendingTarget = 0;
            s_pendingPriority = 99;
        }
    }
}
