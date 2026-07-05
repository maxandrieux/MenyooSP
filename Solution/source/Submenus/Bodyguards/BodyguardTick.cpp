#include "BodyguardTick.h"

#include "../../Natives/natives.h"
#include "../../Menu/Language.h"
#include "../../Scripting/Game.h"

#include "BodyguardManagement.h"
#include "BodyguardMenu.h"
#include "BodyguardCombat.h"
#include "BodyguardEscort.h"
#include "BodyguardChauffeur.h"
#include "BodyguardHud.h"
#include "BodyguardSettings.h" // g_formationIndex / g_formationApplyPending
#include "BodyguardDebug.h"
#include "BodyguardRole.h"

#include <algorithm>
#include <cfloat>

namespace sub::BodyguardMenu
{
    bool g_medicEnabled = true;
    bool g_medicHealPlayer = false;
    bool g_voiceLinesEnabled = true;
    int g_formationSpacing = 0;
}

namespace
{
    using namespace sub::BodyguardMenu;

    struct MedicState
    {
        Ped medic{ 0 };
        Entity patient{ 0 };
        bool patientIsPlayer{ false };
        int startedAt{ 0 };
        int animStartedAt{ 0 };
        bool animStarted{ false };
    };

    static MedicState s_medic;
    static int s_nextSpeechAt = 0;

    float DistanceSq(Entity a, Entity b)
    {
        Vector3 ap = ENTITY::GET_ENTITY_COORDS(a, TRUE);
        Vector3 bp = ENTITY::GET_ENTITY_COORDS(b, TRUE);
        float dx = ap.x - bp.x;
        float dy = ap.y - bp.y;
        float dz = ap.z - bp.z;
        return dx * dx + dy * dy + dz * dz;
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

    BodyguardEntity* FindByPed(Ped ped)
    {
        for (auto& bg : BodyguardDb)
        {
            if (bg.Handle.Exists() && bg.Handle.GetHandle() == ped)
                return &bg;
        }
        return nullptr;
    }

    BodyguardEntity* FindPatient()
    {
        BodyguardEntity* best = nullptr;
        float bestDist = FLT_MAX;
        for (auto& patient : BodyguardDb)
        {
            if (!patient.Handle.Exists())
                continue;
            if (patient.Role == BodyguardRole::Medic)
                continue;

            Ped ped = patient.Handle.GetHandle();
            const int maxHp = (std::max)(1, ENTITY::GET_ENTITY_MAX_HEALTH(ped));
            const int hp = ENTITY::GET_ENTITY_HEALTH(ped);
            const bool needsHelp = !IsBodyguardAlive(patient) || PED::IS_PED_DEAD_OR_DYING(ped, true) || hp < (maxHp * 40 / 100);
            if (!needsHelp)
                continue;

            for (auto& medic : BodyguardDb)
            {
                if (!IsBodyguardAlive(medic) || medic.Role != BodyguardRole::Medic || medic.Handle.GetHandle() == ped ||
                    medic.EscortVehicle.Exists() || medic.HoldPosition || IsChauffeurPed(medic.Handle.GetHandle()))
                    continue;

                float d = DistanceSq(medic.Handle.GetHandle(), ped);
                if (d < 60.f * 60.f && d < bestDist)
                {
                    bestDist = d;
                    best = &patient;
                }
            }
        }
        return best;
    }

    BodyguardEntity* FindNearestMedic(Entity patient, bool patientIsPlayer)
    {
        (void)patientIsPlayer;
        BodyguardEntity* best = nullptr;
        float bestDist = FLT_MAX;
        for (auto& medic : BodyguardDb)
        {
            if (!IsBodyguardAlive(medic) || medic.Role != BodyguardRole::Medic ||
                medic.EscortVehicle.Exists() || medic.HoldPosition || IsChauffeurPed(medic.Handle.GetHandle()))
                continue;

            float d = DistanceSq(medic.Handle.GetHandle(), patient);
            if (d < 60.f * 60.f && d < bestDist)
            {
                bestDist = d;
                best = &medic;
            }
        }
        return best;
    }

    void ResetMedic(bool restore)
    {
        if (restore && s_medic.medic != 0)
        {
            BodyguardEntity* medic = FindByPed(s_medic.medic);
            if (medic)
            {
                RestorePlayerGroup(*medic);
                if (IsBodyguardAlive(*medic))
                    PED::SET_PED_KEEP_TASK(medic->Handle.GetHandle(), FALSE);
            }
        }
        s_medic = MedicState();
    }

    void FinishMedicCare()
    {
        BodyguardEntity* medic = FindByPed(s_medic.medic);
        if (!medic)
        {
            ResetMedic(false);
            return;
        }

        if (s_medic.patientIsPlayer)
        {
            Ped playerPed = PLAYER::PLAYER_PED_ID();
            int maxHp = (std::max)(1, ENTITY::GET_ENTITY_MAX_HEALTH(playerPed));
            ENTITY::SET_ENTITY_HEALTH(playerPed, maxHp, 0);
        }
        else
        {
            BodyguardEntity* patient = FindByPed((Ped)s_medic.patient);
            if (patient)
            {
                if (!IsBodyguardAlive(*patient) || PED::IS_PED_DEAD_OR_DYING(patient->Handle.GetHandle(), true))
                    ReviveOneBodyguard(*patient);
                else
                {
                    Ped ped = patient->Handle.GetHandle();
                    ENTITY::SET_ENTITY_HEALTH(ped, ENTITY::GET_ENTITY_MAX_HEALTH(ped), 0);
                    PED::SET_PED_ARMOUR(ped, armor);
                }
            }
        }

        TryPlayBodyguardSpeech(medic->Handle.GetHandle(), "GENERIC_YES");
        ResetMedic(true);
    }

    void TickMedic()
    {
        if (!g_medicEnabled)
        {
            ResetMedic(true);
            return;
        }

        const int now = MISC::GET_GAME_TIMER();
        const char* animDict = "mini@cpr@char_a@cpr_str";
        const char* animName = "cpr_pumpchest";

        if (s_medic.medic == 0)
        {
            Entity patientEntity = 0;
            bool patientIsPlayer = false;

            BodyguardEntity* patient = FindPatient();
            if (patient)
                patientEntity = patient->Handle.GetHandle();
            else if (g_medicHealPlayer)
            {
                Ped playerPed = PLAYER::PLAYER_PED_ID();
                int maxHp = (std::max)(1, ENTITY::GET_ENTITY_MAX_HEALTH(playerPed));
                if (ENTITY::GET_ENTITY_HEALTH(playerPed) < (maxHp * 50 / 100))
                {
                    patientEntity = playerPed;
                    patientIsPlayer = true;
                }
            }

            if (patientEntity == 0 || !ENTITY::DOES_ENTITY_EXIST(patientEntity))
                return;

            BodyguardEntity* medic = FindNearestMedic(patientEntity, patientIsPlayer);
            if (!medic)
                return;

            s_medic.medic = medic->Handle.GetHandle();
            s_medic.patient = patientEntity;
            s_medic.patientIsPlayer = patientIsPlayer;
            s_medic.startedAt = now;
            RemoveFromPlayerGroup(*medic);
            TASK::TASK_GO_TO_ENTITY(s_medic.medic, patientEntity, -1, 1.5f, 3.f, 0.f, 0);
            PED::SET_PED_KEEP_TASK(s_medic.medic, true);
            return;
        }

        BodyguardEntity* medic = FindByPed(s_medic.medic);
        if (!medic || !IsBodyguardAlive(*medic) || s_medic.patient == 0 || !ENTITY::DOES_ENTITY_EXIST(s_medic.patient) ||
            now - s_medic.startedAt > 20000)
        {
            ResetMedic(true);
            return;
        }

        if (DistanceSq(s_medic.medic, s_medic.patient) > 2.5f * 2.5f)
        {
            TASK::TASK_GO_TO_ENTITY(s_medic.medic, s_medic.patient, -1, 1.5f, 3.f, 0.f, 0);
            return;
        }

        STREAMING::REQUEST_ANIM_DICT(animDict);
        if (!STREAMING::HAS_ANIM_DICT_LOADED(animDict))
            return;

        if (!s_medic.animStarted)
        {
            TASK::TASK_PLAY_ANIM(s_medic.medic, animDict, animName, 4.f, -4.f, 4000, 1, 0.f, FALSE, FALSE, FALSE);
            s_medic.animStarted = true;
            s_medic.animStartedAt = now;
            return;
        }

        if (now - s_medic.animStartedAt >= 4000)
            FinishMedicCare();
    }

    void TickHoldPositions()
    {
        for (auto& bg : BodyguardDb)
        {
            if (!bg.HoldPosition)
                continue;

            if (!IsBodyguardAlive(bg) || bg.EscortVehicle.Exists() || IsBusyBodyguard(bg.Handle.GetHandle()))
                continue;

            Ped ped = bg.Handle.GetHandle();
            if (PED::IS_PED_IN_COMBAT(ped, 0) || PED::IS_PED_SHOOTING(ped))
                continue;

            // Already standing guard: do not restart the scenario every pass
            // (re-issuing TASK_START_SCENARIO_IN_PLACE resets the animation).
            if (PED::IS_PED_USING_ANY_SCENARIO(ped))
                continue;

            RemoveFromPlayerGroup(bg);
            PED::SET_PED_KEEP_TASK(ped, true);
            TASK::TASK_START_SCENARIO_IN_PLACE(ped, "WORLD_HUMAN_GUARD_STAND", 0, true);
        }
    }
}

namespace sub::BodyguardMenu
{
    bool IsMedicBusy(Ped ped)
    {
        return s_medic.medic != 0 && ped == s_medic.medic;
    }

    bool IsBusyBodyguard(Ped ped)
    {
        return IsChauffeurPed(ped) || IsMedicBusy(ped);
    }

    void SetBodyguardHoldPosition(BodyguardEntity& bg, bool hold)
    {
        if (!bg.Handle.Exists())
            return;
        if (hold && bg.EscortVehicle.Exists())
        {
            Game::Print::PrintBottomCentre("~r~" + Language::TranslateToSelected("Escort guards cannot hold position"));
            return;
        }

        bg.HoldPosition = hold;
        Ped ped = bg.Handle.GetHandle();
        bg.Handle.RequestControl();
        if (hold)
        {
            RemoveFromPlayerGroup(bg);
            TASK::CLEAR_PED_TASKS(ped);
            PED::SET_PED_KEEP_TASK(ped, true);
            TASK::TASK_START_SCENARIO_IN_PLACE(ped, "WORLD_HUMAN_GUARD_STAND", 0, true);
        }
        else
        {
            TASK::CLEAR_PED_TASKS(ped);
            RestorePlayerGroup(bg);
        }
    }

    void SetAllBodyguardsHoldPosition(bool hold)
    {
        for (auto& bg : BodyguardDb)
        {
            if (IsBodyguardAlive(bg))
                SetBodyguardHoldPosition(bg, hold);
        }
    }

    void TryPlayBodyguardSpeech(Ped preferredPed, const char* speechName)
    {
        if (!g_voiceLinesEnabled || !speechName)
            return;

        const int now = MISC::GET_GAME_TIMER();
        if (now < s_nextSpeechAt)
            return;

        Ped speaker = preferredPed;
        if (speaker == 0 || !ENTITY::DOES_ENTITY_EXIST(speaker) || PED::IS_PED_DEAD_OR_DYING(speaker, true))
        {
            for (auto& bg : BodyguardDb)
            {
                if (IsBodyguardAlive(bg))
                {
                    speaker = bg.Handle.GetHandle();
                    break;
                }
            }
        }

        if (speaker == 0 || !ENTITY::DOES_ENTITY_EXIST(speaker))
            return;

        AUDIO::PLAY_PED_AMBIENT_SPEECH_NATIVE(speaker, speechName, "SPEECH_PARAMS_FORCE_SHOUTED", 0);
        s_nextSpeechAt = now + 3000;
    }

    void TickBodyguards()
    {
        const DWORD now = MISC::GET_GAME_TIMER();
        static DWORD s_nextEscortTick = 0;
        static DWORD s_nextChauffeurTick = 0;
        static DWORD s_nextMedicTick = 0;
        static DWORD s_nextHoldTick = 0;

        TickBodyguardHud();

        // Persisted formation deferred by ReadBodyguardConfig (no natives allowed
        // there). Applied once, before the empty-DB early-out, so it is armed even
        // if the player recruits their first bodyguard later in the session.
        // g_formationIndex values match the SET_GROUP_FORMATION ids.
        if (g_formationApplyPending)
        {
            g_formationApplyPending = false;
            int group = PLAYER::GET_PLAYER_GROUP(PLAYER::PLAYER_ID());
            PED::SET_GROUP_FORMATION(group, g_formationIndex);
            if (g_formationSpacing > 0)
                PED::SET_GROUP_FORMATION_SPACING(group, (float)g_formationSpacing, -1.f, -1.f);
        }

        if (now >= s_nextChauffeurTick)
        {
            s_nextChauffeurTick = now + 500;
            TickChauffeur();
        }

        // Without tracked bodyguards there is nothing to drive, but a spawned escort
        // vehicle may still be emptying out (Dismiss All / last guard deleted) —
        // keep the throttled cleanup running so it does not leak into the world.
        if (BodyguardDb.empty())
        {
            if (now >= s_nextEscortTick)
            {
                s_nextEscortTick = now + 750;
                CleanupEscortVehicles();
            }
            return;
        }

        // ~400ms: refresh death/alive blip sprites.
        static DWORD s_nextBlipTick = 0;
        if (now >= s_nextBlipTick)
        {
            s_nextBlipTick = now + 400;
            UpdateBodyguardBlipsOnDeath();
        }

        // Every call: combat response driver (no-op when g_combatResponseMode == 0).
        TickCombatResponse();

        if (now >= s_nextMedicTick)
        {
            s_nextMedicTick = now + 1000;
            TickMedic();
        }

        if (now >= s_nextHoldTick)
        {
            s_nextHoldTick = now + 2000;
            TickHoldPositions();
        }

        // ~750ms: escort driving task maintenance.
        if (now >= s_nextEscortTick)
        {
            s_nextEscortTick = now + 750;
            TickEscort();
            TickEscortCatchupOnFoot();
        }
    }
}
