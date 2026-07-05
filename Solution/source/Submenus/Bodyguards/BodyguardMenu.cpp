#include "BodyguardMenu.h"

#include "../../Menu/Menu.h"
#include "../../Scripting/Game.h"
#include "../../Natives/natives2.h"
#include "../../Submenus/Spooner/Submenus.h"
#include "../../Submenus/PedAnimation.h"
#include "../../Submenus/PedModelChanger.h"
#include "../../Submenus/PedSpeech.h"
#include "../../Menu/Language.h"

#include <functional>
#include "../../Menu/Routine.h"
#include "../../Misc/MeteorShower.h"
#include "../../Scripting/GTAvehicle.h"
#include "../../Scripting/Model.h"
#include "../../Submenus/Spooner/MenuOptions.h"
#include "../../Scripting/ModelNames.h"
#include "../../Util/StringManip.h"
#include "../../Util\ExePath.h"
#include "../../Submenus/Spooner/EntityManagement.h"
#include "../../Submenus/Spooner/SpoonerEntity.h"

#include "BodyguardManagement.h"
#include "BodyguardSpawn.h"

#include "../../Scripting/GTAblip.h"
#include "../../Scripting/GTAped.h"
#include "../../Scripting/enums.h"
#include "../../Natives/natives.h"
#include "BodyguardSettings.h"
#include "BodyguardEscort.h"
#include "BodyguardCombat.h"
#include "BodyguardChauffeur.h"
#include "BodyguardTick.h"
#include "BodyguardDebug.h"
using namespace sub::BodyguardMenu;

namespace sub::BodyguardMenu
{
    int armor = 200;
    int health = 200;
    bool godmode = true;
    int blipIcon = 1; // 1 = Standard, 280 = Friend, 480 = VIP

    int  g_armWeaponIndex = 0;
    bool g_autoArmNewBodyguards = false;
    int  g_spawnWeaponIndex = 0;
    int  g_presetIndex = 0;
    bool g_presetApplyCurrent = true;
    bool g_presetUseForNew = false;

    // v2 role state (single definition TU: BodyguardMenu.cpp).
    BodyguardRole g_defaultSpawnRole = BodyguardRole::Rifleman;
    bool g_roleBlipsEnabled = true;
}

static constexpr int BLIP_COLOUR_BLUELIGHT = 3;
static constexpr int BG_COMBAT_ATTR_CAN_USE_COVER = 0;
static constexpr int BG_COMBAT_ATTR_CAN_USE_VEHICLES = 1;
static constexpr int BG_COMBAT_ATTR_CAN_DO_DRIVEBYS = 2;
static constexpr int BG_COMBAT_ATTR_CAN_FIGHT_ARMED_WHEN_NOT_ARMED = 5;
static constexpr int BG_COMBAT_ATTR_ALWAYS_FIGHT = 46;

void sub::BodyguardMenu::RemoveBodyguardBlip(Ped ped)
{
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped))
        return;

    Blip blip = GET_BLIP_FROM_ENTITY(ped);
    if (!blip)
        return;

    GTAblip gtaBlip(blip);
    if (gtaBlip.Exists())
        gtaBlip.Remove();
}

void sub::BodyguardMenu::ApplyBodyguardBlip(Ped ped, int icon)
{
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped))
        return;

    RemoveBodyguardBlip(ped);

    Blip blip = ADD_BLIP_FOR_ENTITY(ped);
    if (!blip)
        return;

    SET_BLIP_SPRITE(blip, icon);
    SET_BLIP_SCALE(blip, 0.80f);
    SET_BLIP_COLOUR(blip, BLIP_COLOUR_BLUELIGHT);
    SET_BLIP_AS_FRIENDLY(blip, true);
}

// Internal: apply a blip with an explicit sprite + colour (used by the role blips).
static void ApplyBodyguardBlipSpriteColour(Ped ped, int sprite, int colour)
{
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped))
        return;

    sub::BodyguardMenu::RemoveBodyguardBlip(ped);

    Blip blip = ADD_BLIP_FOR_ENTITY(ped);
    if (!blip)
        return;

    SET_BLIP_SPRITE(blip, sprite);
    SET_BLIP_SCALE(blip, 0.80f);
    SET_BLIP_COLOUR(blip, colour);
    SET_BLIP_AS_FRIENDLY(blip, true);
}

void sub::BodyguardMenu::ApplyRoleToBodyguard(Ped ped, BodyguardRole role)
{
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped))
        return;

    const BodyguardRoleDef& def = RoleDef(role);
    GTAped gp(ped);
    gp.SetWeapon(def.defaultWeapon);
    gp.SetAccuracy(def.accuracy);
    PED::SET_PED_SHOOT_RATE(ped, def.shootRate);
    PED::SET_PED_COMBAT_ABILITY(ped, def.combatAbility);
    PED::SET_PED_COMBAT_RANGE(ped, def.combatRange);
    PED::SET_PED_COMBAT_MOVEMENT(ped, role == BodyguardRole::Sniper ? 1 : 2);
    PED::SET_PED_COMBAT_ATTRIBUTES(ped, BG_COMBAT_ATTR_CAN_USE_COVER, true);
    PED::SET_PED_COMBAT_ATTRIBUTES(ped, BG_COMBAT_ATTR_CAN_USE_VEHICLES, true);
    PED::SET_PED_COMBAT_ATTRIBUTES(ped, BG_COMBAT_ATTR_CAN_DO_DRIVEBYS, true);
    PED::SET_PED_COMBAT_ATTRIBUTES(ped, BG_COMBAT_ATTR_CAN_FIGHT_ARMED_WHEN_NOT_ARMED, true);
    PED::SET_PED_COMBAT_ATTRIBUTES(ped, BG_COMBAT_ATTR_ALWAYS_FIGHT, true);
    PED::SET_PED_FIRING_PATTERN(ped, def.firingPattern);
    PED::SET_PED_HIGHLY_PERCEPTIVE(ped, true);
    PED::SET_PED_SEEING_RANGE(ped, 120.0f);
    PED::SET_PED_HEARING_RANGE(ped, 120.0f);
    PED::SET_CAN_ATTACK_FRIENDLY(ped, false, false);

    if (role == BodyguardRole::Driver)
    {
        PED::SET_DRIVER_ABILITY(ped, 1.0f);
        PED::SET_DRIVER_AGGRESSIVENESS(ped, 0.55f);
    }
}

void sub::BodyguardMenu::ApplyBodyguardBlipForRole(Ped ped, BodyguardRole role)
{
    if (g_roleBlipsEnabled)
    {
        const BodyguardRoleDef& def = RoleDef(role);
        ApplyBodyguardBlipSpriteColour(ped, def.blipSprite, def.blipColour);
    }
    else
    {
        ApplyBodyguardBlip(ped, sub::BodyguardMenu::blipIcon);
    }
}

void sub::BodyguardMenu::RefreshAllBodyguardBlips()
{
    for (unsigned int i = 0; i < sub::BodyguardMenu::BodyguardDb.size(); ++i)
    {
        auto& bg = sub::BodyguardMenu::BodyguardDb[i];
        if (!bg.Handle.Exists())
            continue;

        Ped ped = bg.Handle.GetHandle();
        if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped))
            continue;

        int hp = ENTITY::GET_ENTITY_HEALTH(ped);
        if (hp <= 0)
            ApplyBodyguardBlip(ped, 274); // dead blip
        else
            ApplyBodyguardBlipForRole(ped, bg.Role);
    }
}

void sub::BodyguardMenu::UpdateBodyguardBlipsOnDeath()
{
    for (auto& bg : BodyguardDb)
    {
        if (!bg.Handle.Exists())
            continue;

        Ped ped = bg.Handle.GetHandle();
        if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped))
            continue;

        int hp = ENTITY::GET_ENTITY_HEALTH(ped);
        Blip blip = GET_BLIP_FROM_ENTITY(ped);

        if (hp <= 0)
        {
            // Dead -> show the dead blip (274) if not already shown.
            if (!blip || GET_BLIP_SPRITE(blip) != 274)
            {
                ApplyBodyguardBlip(ped, 274);
                TryPlayBodyguardSpeech(0, "GENERIC_FRIGHTENED_HIGH");
            }
        }
        else
        {
            // Alive -> restore the role/normal blip if it was showing the dead sprite.
            if (blip && GET_BLIP_SPRITE(blip) == 274)
                ApplyBodyguardBlipForRole(ped, bg.Role);
        }
    }
}

namespace
{
    struct BgWeapon { const char* label; Hash hash; };

    // Arm-All list (default = Pistol). No "None" entry.
    const BgWeapon kArmWeapons[] =
    {
        { "Pistol",        WEAPON_PISTOL        },
        { "Combat Pistol", WEAPON_COMBATPISTOL  },
        { "Micro SMG",     WEAPON_MICROSMG      },
        { "SMG",           WEAPON_SMG           },
        { "Carbine Rifle", WEAPON_CARBINERIFLE  },
        { "Assault Rifle", WEAPON_ASSAULTRIFLE  },
        { "Pump Shotgun",  WEAPON_PUMPSHOTGUN   },
        { "Combat MG",     WEAPON_COMBATMG      },
        { "RPG",           WEAPON_RPG           },
    };
    const int kArmWeaponCount = (int)(sizeof(kArmWeapons) / sizeof(kArmWeapons[0]));

    // Spawn list: index 0 = None (no weapon), then the same weapons.
    const BgWeapon kSpawnWeapons[] =
    {
        { "None",          0                    },
        { "Pistol",        WEAPON_PISTOL        },
        { "Combat Pistol", WEAPON_COMBATPISTOL  },
        { "Micro SMG",     WEAPON_MICROSMG      },
        { "SMG",           WEAPON_SMG           },
        { "Carbine Rifle", WEAPON_CARBINERIFLE  },
        { "Assault Rifle", WEAPON_ASSAULTRIFLE  },
        { "Pump Shotgun",  WEAPON_PUMPSHOTGUN   },
        { "Combat MG",     WEAPON_COMBATMG      },
        { "RPG",           WEAPON_RPG           },
    };
    const int kSpawnWeaponCount = (int)(sizeof(kSpawnWeapons) / sizeof(kSpawnWeapons[0]));

    struct BgPreset { const char* label; Hash weapon; int health; int armor; bool godmode; int accuracy; };
    const BgPreset kPresets[] =
    {
        { "Police",   WEAPON_COMBATPISTOL, 200, 100, false, 50 },
        { "Military", WEAPON_CARBINERIFLE, 300, 200, false, 75 },
        { "Gang",     WEAPON_MICROSMG,     150,   0, false, 35 },
        { "FIB",      WEAPON_ASSAULTRIFLE, 250, 150, false, 65 },
        { "Heavy",    WEAPON_COMBATMG,     400, 200, true,  60 },
    };
    const int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

    void GiveWeaponToPed(Ped ped, Hash weapon)
    {
        if (weapon == 0) return;
        GTAped gp(ped);
        gp.SetWeapon(weapon); // gives and equips
    }

    int ArmAllAliveBodyguards(Hash weapon)
    {
        int n = 0;
        for (auto& bg : sub::BodyguardMenu::BodyguardDb)
        {
            if (!sub::BodyguardMenu::IsBodyguardAlive(bg)) continue;
            Ped ped = bg.Handle.GetHandle();
            bg.Handle.RequestControl();
            GiveWeaponToPed(ped, weapon);
            ++n;
        }
        return n;
    }

    int HealAllAliveBodyguards()
    {
        int n = 0;
        for (auto& bg : sub::BodyguardMenu::BodyguardDb)
        {
            if (!bg.Handle.Exists()) continue;
            Ped ped = bg.Handle.GetHandle();
            const bool dying = PED::IS_PED_DEAD_OR_DYING(ped, true) != 0;
            if (!sub::BodyguardMenu::IsBodyguardAlive(bg) && !dying) continue;
            bg.Handle.RequestControl();
            if (dying)
            {
                PED::RESURRECT_PED(ped);
                TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped);
            }
            ENTITY::SET_ENTITY_MAX_HEALTH(ped, sub::BodyguardMenu::health);
            ENTITY::SET_ENTITY_HEALTH(ped, sub::BodyguardMenu::health, 0);
            ++n;
        }
        return n;
    }

    int RefillArmorAllAliveBodyguards()
    {
        int n = 0;
        for (auto& bg : sub::BodyguardMenu::BodyguardDb)
        {
            if (!sub::BodyguardMenu::IsBodyguardAlive(bg)) continue;
            Ped ped = bg.Handle.GetHandle();
            bg.Handle.RequestControl();
            PED::SET_PED_ARMOUR(ped, sub::BodyguardMenu::armor);
            ++n;
        }
        return n;
    }

}

bool sub::BodyguardMenu::ReviveOneBodyguard(BodyguardEntity& bg)
{
    if (!bg.Handle.Exists())
        return false;

    Ped ped = bg.Handle.GetHandle();
    bg.Handle.RequestControl();
    PED::RESURRECT_PED(ped);
    TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped);
    ENTITY::SET_ENTITY_MAX_HEALTH(ped, sub::BodyguardMenu::health);
    ENTITY::SET_ENTITY_HEALTH(ped, sub::BodyguardMenu::health, 0);
    PED::SET_PED_ARMOUR(ped, sub::BodyguardMenu::armor);
    if (sub::BodyguardMenu::godmode) SetPedInvincibleOn(ped);
    else SetPedInvincibleOff(ped);
    PED::SET_PED_AS_GROUP_MEMBER(ped, PLAYER::GET_PLAYER_GROUP(PLAYER::PLAYER_ID()));
    PED::SET_PED_NEVER_LEAVES_GROUP(ped, true);
    bg.RemovedFromGroup = false;
    bg.HoldPosition = false;
    sub::BodyguardMenu::ApplyRoleToBodyguard(ped, bg.Role);
    sub::BodyguardMenu::ApplyBodyguardBlipForRole(ped, bg.Role);
    return true;
}

namespace
{
    int ReviveAllDeadBodyguards(int& missing)
    {
        int n = 0;
        missing = 0;
        for (auto& bg : sub::BodyguardMenu::BodyguardDb)
        {
            if (!bg.Handle.Exists())
            {
                ++missing;
                continue;
            }

            Ped ped = bg.Handle.GetHandle();
            const bool needsRevive = !IsBodyguardAlive(bg) || PED::IS_PED_DEAD_OR_DYING(ped, true);
            if (!needsRevive)
                continue;

            if (sub::BodyguardMenu::ReviveOneBodyguard(bg))
                ++n;
        }
        return n;
    }

    int ApplyPresetToCurrentSquad(const BgPreset& p)
    {
        int n = 0;
        for (auto& bg : sub::BodyguardMenu::BodyguardDb)
        {
            if (!sub::BodyguardMenu::IsBodyguardAlive(bg)) continue;
            Ped ped = bg.Handle.GetHandle();
            bg.Handle.RequestControl();
            ENTITY::SET_ENTITY_MAX_HEALTH(ped, p.health);
            ENTITY::SET_ENTITY_HEALTH(ped, p.health, 0);
            PED::SET_PED_ARMOUR(ped, p.armor);
            if (p.godmode) SetPedInvincibleOn(ped);
            else           SetPedInvincibleOff(ped);
            GTAped gp(ped);
            gp.SetAccuracy(p.accuracy);
            gp.SetWeapon(p.weapon);
            ++n;
        }
        return n;
    }
}

void sub::BodyguardMenu::ApplyAutoArmOnSpawn(Ped ped)
{
    if (!g_autoArmNewBodyguards) return;
    if (g_spawnWeaponIndex <= 0 || g_spawnWeaponIndex >= kSpawnWeaponCount) return;
    GiveWeaponToPed(ped, kSpawnWeapons[g_spawnWeaponIndex].hash);
    dbg::Log(std::string("AUTOARM ped=") + std::to_string(ped) + " weapon=" + kSpawnWeapons[g_spawnWeaponIndex].label);
}

// Spawn-arming defaults (auto-arm + spawn weapon). Rendered inside the Settings
// submenu ("Spawn Defaults" section) so every future-spawn option lives in one place;
// the weapon tables stay private to this TU.
void sub::BodyguardMenu::AddSpawnArmingOptions()
{
    AddToggle("Auto-Arm New Bodyguards (overrides role weapon)", g_autoArmNewBodyguards);

    if (g_spawnWeaponIndex < 0 || g_spawnWeaponIndex >= kSpawnWeaponCount) g_spawnWeaponIndex = 0;
    bool sInput = false, sPlus = false, sMinus = false;
    AddTexter("Spawn Weapon", 0, { kSpawnWeapons[g_spawnWeaponIndex].label }, sInput, sPlus, sMinus);
    if (sPlus)  g_spawnWeaponIndex = (g_spawnWeaponIndex + 1) % kSpawnWeaponCount;
    if (sMinus) g_spawnWeaponIndex = (g_spawnWeaponIndex == 0 ? kSpawnWeaponCount - 1 : g_spawnWeaponIndex - 1);
}

void sub::BodyguardMenu::BodyguardSquadTools()
{
    AddTitle("Active Bodyguards");

    AddBreak("--- Weapons ---");

    if (g_armWeaponIndex < 0 || g_armWeaponIndex >= kArmWeaponCount) g_armWeaponIndex = 0;

    bool wInput = false, wPlus = false, wMinus = false;
    AddTexter("Weapon", 0, { kArmWeapons[g_armWeaponIndex].label }, wInput, wPlus, wMinus);
    if (wPlus)  g_armWeaponIndex = (g_armWeaponIndex + 1) % kArmWeaponCount;
    if (wMinus) g_armWeaponIndex = (g_armWeaponIndex == 0 ? kArmWeaponCount - 1 : g_armWeaponIndex - 1);

    bool bArm = false;
    AddOption("Arm All Bodyguards", bArm);
    if (bArm)
    {
        int n = ArmAllAliveBodyguards(kArmWeapons[g_armWeaponIndex].hash);
        Game::Print::PrintBottomLeft(Language::TranslateToSelected("Bodyguards armed: ") + std::to_string(n));
        sub::BodyguardMenu::BodyguardManagement::DbgLogSquadState(std::string("ARM_ALL weapon=") + kArmWeapons[g_armWeaponIndex].label + " affected=" + std::to_string(n));
    }

    AddBreak("--- Health ---");

    bool bHeal = false;
    AddOption("Heal All Bodyguards", bHeal);
    if (bHeal)
    {
        int n = HealAllAliveBodyguards();
        Game::Print::PrintBottomLeft(Language::TranslateToSelected("Bodyguards healed: ") + std::to_string(n));
        sub::BodyguardMenu::BodyguardManagement::DbgLogSquadState("HEAL_ALL affected=" + std::to_string(n));
    }

    bool bRefill = false;
    AddOption("Refill Armor All Bodyguards", bRefill);
    if (bRefill)
    {
        int n = RefillArmorAllAliveBodyguards();
        Game::Print::PrintBottomLeft(Language::TranslateToSelected("Armor refilled: ") + std::to_string(n));
        sub::BodyguardMenu::BodyguardManagement::DbgLogSquadState("REFILL_ARMOR affected=" + std::to_string(n));
    }

    bool bRevive = false;
    AddOption("Revive Dead", bRevive);
    if (bRevive)
    {
        int missing = 0;
        int n = ReviveAllDeadBodyguards(missing);
        std::string msg = Language::TranslateToSelected("Bodyguards revived: ") + std::to_string(n);
        if (missing > 0)
            msg += " ~o~(" + std::to_string(missing) + Language::TranslateToSelected(" bodies missing") + ")";
        Game::Print::PrintBottomLeft(msg);
        sub::BodyguardMenu::BodyguardManagement::DbgLogSquadState("REVIVE_DEAD affected=" + std::to_string(n) + " missing=" + std::to_string(missing));
    }

    AddBreak("--- Stat Presets ---");

    if (g_presetIndex < 0 || g_presetIndex >= kPresetCount) g_presetIndex = 0;

    bool pInput = false, pPlus = false, pMinus = false;
    AddTexter("Preset", 0, { kPresets[g_presetIndex].label }, pInput, pPlus, pMinus);
    if (pPlus)  g_presetIndex = (g_presetIndex + 1) % kPresetCount;
    if (pMinus) g_presetIndex = (g_presetIndex == 0 ? kPresetCount - 1 : g_presetIndex - 1);

    AddToggle("Apply To Current Squad", g_presetApplyCurrent);
    AddToggle("Use For New Bodyguards", g_presetUseForNew);

    bool bApply = false;
    AddOption("Apply Preset", bApply);
    if (bApply)
    {
        const BgPreset& p = kPresets[g_presetIndex];
        int n = 0;
        if (g_presetApplyCurrent)
            n = ApplyPresetToCurrentSquad(p);

        if (g_presetUseForNew)
        {
            // Stats only. Spawn arming (auto-arm + weapon) is configured exclusively
            // in Settings > Spawn Defaults — a preset must never toggle it silently.
            sub::BodyguardMenu::health  = p.health;
            sub::BodyguardMenu::armor   = p.armor;
            sub::BodyguardMenu::godmode = p.godmode;
        }

        Game::Print::PrintBottomLeft(Language::TranslateToSelected("Preset '") + Language::TranslateToSelected(p.label) + Language::TranslateToSelected("' applied: ") + std::to_string(n));
        sub::BodyguardMenu::BodyguardManagement::DbgLogSquadState(std::string("PRESET name=") + p.label
            + " applyCur=" + (g_presetApplyCurrent ? "1" : "0")
            + " useNew=" + (g_presetUseForNew ? "1" : "0")
            + " affected=" + std::to_string(n));
    }

    AddBreak("--- Cleanup ---");

    bool bCleanup = false;
    AddOption("Cleanup Dead Bodyguards", bCleanup);
    if (bCleanup)
    {
        int n = sub::BodyguardMenu::BodyguardManagement::CleanupDeadBodyguards();
        Game::Print::PrintBottomLeft(Language::TranslateToSelected("Dead or missing bodyguards removed: ") + std::to_string(n));
    }

    bool bDismiss = false;
    AddOption("Dismiss All Bodyguards", bDismiss);
    if (bDismiss)
    {
        sub::BodyguardMenu::BodyguardManagement::DismissAllBodyguards();
        Game::Print::PrintBottomLeft("All bodyguards dismissed.");
    }
}

namespace sub
{
    void BodyguardMainMenu()
    {
        bool bTeleportBodyguards = false;

        AddTitle("Bodyguards");

        AddOption("Spawn Bodyguard", null, nullFunc, SUB::BODYGUARD_SPAWN);
        AddOption("Bodyguard List", null, nullFunc, SUB::BODYGUARD_LIST);
        AddOption("Active Bodyguards", null, nullFunc, SUB::BODYGUARD_SQUAD_TOOLS);
        AddOption("Squads", null, nullFunc, SUB::BODYGUARD_SQUADS);
        AddOption("Escort Vehicle", null, nullFunc, SUB::BODYGUARD_ESCORT);
        AddOption("Chauffeur", null, nullFunc, SUB::BODYGUARD_CHAUFFEUR);
        AddOption("Settings", null, nullFunc, SUB::BODYGUARD_SETTINGS);

        AddBreak("--- Actions ---");
        bool bAttack = false;
        AddOption("Attack My Target", bAttack);
        if (bAttack)
        {
            Entity target = sub::BodyguardMenu::ResolvePlayerTargetEntity();
            if (target == 0)
                Game::Print::PrintBottomCentre(Language::TranslateToSelected("No target"));
            else
            {
                sub::BodyguardMenu::TaskAllBodyguardsOnTarget(target);
                sub::BodyguardMenu::TryPlayBodyguardSpeech(0, "GENERIC_YES");
            }
        }

        bool bCease = false;
        AddOption("Cease Fire", bCease);
        if (bCease)
        {
            sub::BodyguardMenu::CeaseFireAll();
            sub::BodyguardMenu::TryPlayBodyguardSpeech(0, "GENERIC_YES");
        }

        bool anyFollowing = false;
        for (auto& bg : sub::BodyguardMenu::BodyguardDb)
        {
            if (sub::BodyguardMenu::IsBodyguardAlive(bg) && !bg.HoldPosition)
            {
                anyFollowing = true;
                break;
            }
        }
        bool bHoldAll = false;
        AddOption(anyFollowing ? "Hold Positions (All)" : "Follow Me (All)", bHoldAll);
        if (bHoldAll)
        {
            sub::BodyguardMenu::SetAllBodyguardsHoldPosition(anyFollowing);
            sub::BodyguardMenu::TryPlayBodyguardSpeech(0, "GENERIC_YES");
        }

        AddOption("Bring Bodyguards To Self", bTeleportBodyguards);

        if (bTeleportBodyguards)
        {
            Ped playerPed = PLAYER_PED_ID();
            if (ENTITY::DOES_ENTITY_EXIST(playerPed))
            {
                Vector3 playerPos = ENTITY::GET_ENTITY_COORDS(playerPed, true);
                Vector3 forward = ENTITY::GET_ENTITY_FORWARD_VECTOR(playerPed);

                float baseDist = 3.0f;
                float spacing = 0.75f;
                int placed = 0;

                for (unsigned int i = 0; i < sub::BodyguardMenu::BodyguardDb.size(); ++i)
                {
                    auto& bg = sub::BodyguardMenu::BodyguardDb[i];
                    if (!bg.Handle.Exists())
                        continue;

                    Ped ped = bg.Handle.GetHandle();

                    Vector3 targetPos =
                        playerPos +
                        (forward * baseDist) +
                        Vector3(0.0f, 0.0f, 0.2f) +
                        (forward * (spacing * placed));

                    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(
                        ped,
                        targetPos.x,
                        targetPos.y,
                        targetPos.z,
                        false, false, false
                    );

                    placed++;
                }

                Game::Print::PrintBottomLeft("Bodyguards teleported.");
            }
        }
    }
}


#include "..\..\Menu\submenu_switch.h"
#include "..\..\Menu\submenu_enum.h"
REGISTER_SUBMENU(BODYGUARDMAINMENU,         sub::BodyguardMainMenu)
REGISTER_SUBMENU(BODYGUARD_SPAWN,           sub::BodyguardMenu::BodyguardSpawn)
REGISTER_SUBMENU(BODYGUARD_SQUAD_TOOLS,     sub::BodyguardMenu::BodyguardSquadTools)
// BODYGUARD_SQUAD_MAINTENANCE / BODYGUARD_MANAGE_SQUAD / BODYGUARD_SQUAD_PRESETS:
// merged into the "Active Bodyguards" submenu (BODYGUARD_SQUAD_TOOLS, formerly
// labelled "Squad Tools"); enum ids kept (never reorder), handlers removed.
