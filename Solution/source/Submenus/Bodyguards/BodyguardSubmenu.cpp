#include "BodyguardManagement.h"
#include "BodyguardSettings.h"
#include "BodyguardSpawn.h"
#include "../../Menu/submenu_enum.h"
#include "../../Menu/Menu.h"
#include "../../Scripting/Game.h"
#include "../../Scripting/GTAped.h"
#include "../../Submenus/PedComponentChanger.h"
#include "BodyguardMenu.h"
#include "BodyguardTick.h"
#include "../../Submenus/WeaponOptions.h"
#include "../../Scripting/Camera.h"
#include "../../Scripting/World.h"
#include "../..//Natives/natives.h"
#include "../../Util/StringManip.h"

namespace sub
{
    void ComponentChanger();
}

namespace sub::BodyguardMenu
{
    void SetEnt242()
    {
        BodyguardEntity* sel = GetSelectedBodyguard();
        if (sel && sel->Handle.Exists())
            g_Ped1 = sel->Handle.Handle();
    }
    void BodyguardEntityOps()
    {
        BodyguardEntity* SelectedBodyguard = GetSelectedBodyguard();
        // Determine the title dynamically
        std::string title = "Bodyguard";

        if (SelectedBodyguard)
        {
            if (SelectedBodyguard->Handle.Exists())
            {
                // Prefer a friendly name if provided
                if (!SelectedBodyguard->Name.empty())
                {
                    title = SelectedBodyguard->Name;
                }
                // Otherwise use a stored hash-name (if present)
                else if (!SelectedBodyguard->HashName.empty())
                {
                    title = SelectedBodyguard->HashName;
                }
                // Fallback: use the model hash as hex string
                else
                {
                    auto model = SelectedBodyguard->Handle.Model();
                    title = IntToHexString(model.hash, true);
                }
            }
            else
            {
                // Ped doesn't exist — show that in the title so it's obvious
                title = "Bodyguard (missing)";
            }
        }

        AddTitle(title);

        // Keep the rest of your existing logic unchanged
        if (!SelectedBodyguard)
        {
            AddOption("No bodyguard selected");
            return;
        }

        if (!SelectedBodyguard->Handle.Exists())
        {
            AddOption("Bodyguard no longer exists");
            return;
        }

        // Per-bodyguard role: re-applies role tuning + role blip on change.
        {
            int roleIdx = (int)SelectedBodyguard->Role;
            if (roleIdx < 0 || roleIdx >= (int)BodyguardRole::Count) roleIdx = 0;
            bool roleInput = false, role_plus = false, role_minus = false;
            AddTexter("Role", 0, { kRoleDisplayLabels[roleIdx] }, roleInput, role_plus, role_minus);
            if (role_plus)  roleIdx = (roleIdx + 1) % (int)BodyguardRole::Count;
            if (role_minus) roleIdx = (roleIdx == 0 ? (int)BodyguardRole::Count - 1 : roleIdx - 1);
            if (role_plus || role_minus)
            {
                SelectedBodyguard->Role = (BodyguardRole)roleIdx;
                Ped ped = SelectedBodyguard->Handle.GetHandle();
                SelectedBodyguard->Handle.RequestControl();
                ApplyRoleToBodyguard(ped, SelectedBodyguard->Role);
                ApplyBodyguardBlipForRole(ped, SelectedBodyguard->Role);
            }
        }

        AddOption("Wardrobe", null, SetEnt242, SUB::COMPONENTS);
        if (g_cam_componentChanger.Exists())
        {
            g_cam_componentChanger.SetActive(false);
            g_cam_componentChanger.Destroy();
            World::SetRenderingCamera(0);
        }
        AddOption("Voice Changer", null, SetEnt242, SUB::VOICECHANGER);
        AddOption("Weapons", null, nullFunc, SUB::BODYGUARD_WEAPONOPS);
        AddOption("Loadouts", null, SetEnt242, SUB::WEAPONOPS_LOADOUTS);

        AddBreak("--- Actions ---");

        bool bHold = false;
        AddOption(SelectedBodyguard->HoldPosition ? "Follow Me" : "Hold Position", bHold);
        if (bHold)
        {
            SetBodyguardHoldPosition(*SelectedBodyguard, !SelectedBodyguard->HoldPosition);
            TryPlayBodyguardSpeech(SelectedBodyguard->Handle.GetHandle(), "GENERIC_YES");
        }

        bool bHeal = false;
        AddOption("Heal Bodyguard", bHeal);
        if (bHeal)
        {
            Ped ped = SelectedBodyguard->Handle.GetHandle();
            SelectedBodyguard->Handle.RequestControl();
            ENTITY::SET_ENTITY_MAX_HEALTH(ped, sub::BodyguardMenu::health);
            ENTITY::SET_ENTITY_HEALTH(ped, sub::BodyguardMenu::health, 0);
            PED::SET_PED_ARMOUR(ped, sub::BodyguardMenu::armor);
            Game::Print::PrintBottomLeft("Bodyguard healed");
        }

        bool bBring = false;
        AddOption("Bring Bodyguard To Self", bBring);
        if (bBring)
        {
            Ped playerPed = PLAYER::PLAYER_PED_ID();
            if (ENTITY::DOES_ENTITY_EXIST(playerPed))
            {
                Vector3 playerPos = ENTITY::GET_ENTITY_COORDS(playerPed, true);
                Vector3 forward = ENTITY::GET_ENTITY_FORWARD_VECTOR(playerPed);
                Vector3 targetPos = playerPos + (forward * 3.0f) + Vector3(0.0f, 0.0f, 0.2f);

                Ped ped = SelectedBodyguard->Handle.GetHandle();
                SelectedBodyguard->Handle.RequestControl();
                ENTITY::SET_ENTITY_COORDS_NO_OFFSET(
                    ped,
                    targetPos.x,
                    targetPos.y,
                    targetPos.z,
                    false, false, false
                );
                Game::Print::PrintBottomLeft("Bodyguard teleported");
            }
        }

        AddBreak("--- Danger ---");

        bool bDelete = false;
        AddOption("Delete Bodyguard", bDelete);
        if (bDelete)
        {
            sub::BodyguardMenu::BodyguardManagement::DeleteBodyguard(*SelectedBodyguard);
            g_selectedBodyguardHandle = 0;
            Menu::SetPreviousMenu();
            return;
        }
    }
    void BodyguardWeaponOps()
    {
        BodyguardEntity* SelectedBodyguard = GetSelectedBodyguard();
        if (!SelectedBodyguard || !SelectedBodyguard->Handle.Exists())
            return;

        Ped ped = SelectedBodyguard->Handle.GetHandle();

        g_WeaponOpsPedOverride = ped;
        g_WeaponOpsPlayerOverride = -1;
        g_WeaponMenuPedOverride = ped;


        WeaponIndivs_catind::Sub_CategoriesList();

        g_WeaponOpsPedOverride = 0;
        g_WeaponOpsPlayerOverride = -1;
        g_WeaponMenuPedOverride = 0;
    }
    void BodyguardWeaponLoadoutOps()
    {
        BodyguardEntity* SelectedBodyguard = GetSelectedBodyguard();
        if (!SelectedBodyguard || !SelectedBodyguard->Handle.Exists())
            return;

        Ped ped = SelectedBodyguard->Handle.GetHandle();

        g_WeaponOpsPedOverride = ped;
        g_WeaponOpsPlayerOverride = -1;
        g_WeaponMenuPedOverride = ped;

        if (g_WeaponOpsPedOverride != 0)
        {
            g_Ped1 = g_WeaponOpsPedOverride;
            g_Ped2 = g_WeaponOpsPlayerOverride;
        }
        else
        {
            g_Ped1 = PLAYER::PLAYER_PED_ID();
            g_Ped2 = PLAYER::PLAYER_ID();
        }

        WeaponsLoadouts_catind::Sub_Loadouts_InItem();

        g_WeaponOpsPedOverride = 0;
        g_WeaponOpsPlayerOverride = -1;
        g_WeaponMenuPedOverride = 0;
    }

}

#include "..\..\Menu\submenu_switch.h"
#include "..\..\Menu\submenu_enum.h"
REGISTER_SUBMENU(BODYGUARD_ENTITYOPS,   sub::BodyguardMenu::BodyguardEntityOps)
REGISTER_SUBMENU(BODYGUARD_WEAPONOPS,   sub::BodyguardMenu::BodyguardWeaponOps)
