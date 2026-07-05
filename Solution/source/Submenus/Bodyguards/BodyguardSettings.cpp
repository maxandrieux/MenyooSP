#include "BodyguardSettings.h"
#include "BodyguardManagement.h"
#include "../../Menu/Menu.h"
#include "../../Menu/Language.h"
#include "../../Natives/natives.h"
#include "../../Natives/natives2.h"

#include "../../Util/keyboard.h"
#include "../../Scripting/Game.h"
#include "BodyguardMenu.h"
#include "BodyguardDebug.h"
#include "BodyguardSpawn.h"
#include "BodyguardCombat.h"
#include "BodyguardRole.h"
#include "BodyguardHud.h"
#include "BodyguardTick.h"

#include "../../Menu/Routine.h"

#include <algorithm>
#include <climits>
#include <vector>

namespace sub::BodyguardMenu
{
    // Currently selected bodyguard in the menu
    Ped g_selectedBodyguardHandle = 0;

    BodyguardEntity* GetSelectedBodyguard()
    {
        if (g_selectedBodyguardHandle == 0)
            return nullptr;

        for (auto& bg : BodyguardDb)
        {
            if (bg.Handle.GetHandle() == g_selectedBodyguardHandle)
                return &bg;
        }

        // Selected bodyguard is no longer in the DB (deleted/cleaned/dismissed while its
        // submenu was open). Log once per distinct lost handle to avoid per-frame spam.
        static Ped s_lastLostLogged = 0;
        if (g_selectedBodyguardHandle != s_lastLostLogged)
        {
            s_lastLostLogged = g_selectedBodyguardHandle;
            dbg::Log("SELECTION_LOST handle=" + std::to_string(g_selectedBodyguardHandle));
        }
        return nullptr;
    }

    void BodyguardList()
    {
        size_t alive = 0;
        for (const auto& bg : BodyguardDb)
        {
            if (bg.Handle.Exists())
                ++alive;
        }

        AddTitle(Language::TranslateToSelected("Bodyguard List") + " (" +
            std::to_string(alive) + "/" + std::to_string(BodyguardManagement::MAX_BODYGUARDS) + ")");

        if (BodyguardDb.empty())
        {
            AddOption("No bodyguards spawned");
            return;
        }

        BodyguardEntity* pBodyguardToDelete = nullptr;

        for (UINT i = 0; i < BodyguardDb.size(); i++)
        {
            auto& bg = BodyguardDb[i];

            if (!bg.Handle.Exists())
                continue;

            bool bPressed = false;

            std::string label = !bg.Name.empty() ? bg.Name : bg.HashName;
            int roleIdx = (int)bg.Role;
            if (roleIdx < 0 || roleIdx >= (int)BodyguardRole::Count)
                roleIdx = 0;
            int hp = ENTITY::GET_ENTITY_HEALTH(bg.Handle.GetHandle());
            int maxHp = (std::max)(1, ENTITY::GET_ENTITY_MAX_HEALTH(bg.Handle.GetHandle()));
            label += " [" + kRoleDisplayLabels[roleIdx] + "] " + std::to_string(hp) + "/" + std::to_string(maxHp);
            if (hp <= 0 || PED::IS_PED_DEAD_OR_DYING(bg.Handle.GetHandle(), true))
                label += " ~r~[DEAD]";

            AddOption(label, bPressed, nullFunc, SUB::BODYGUARD_ENTITYOPS);

            if (bPressed)
            {
                g_selectedBodyguardHandle = bg.Handle.GetHandle();
            }

            if (*Menu::currentopATM == Menu::printingop)
            {
                if (bg.Handle.Exists())
                    ENTITY::SET_ENTITY_HAS_GRAVITY(bg.Handle.GetHandle(), true);
                sub::BodyguardMenu::BodyguardManagement::ShowArrowAboveEntity(bg.Handle);

                bool bDeletePressed = false;
                if (Menu::bitController)
                {
                    Menu::add_IB(INPUT_SCRIPT_RLEFT, Language::TranslateToSelected("Delete Bodyguard"));
                    bDeletePressed = IS_DISABLED_CONTROL_JUST_PRESSED(2, INPUT_SCRIPT_RLEFT) != 0;
                }
                else
                {
                    Menu::add_IB(VirtualKey::B, Language::TranslateToSelected("Delete Bodyguard"));
                    bDeletePressed = IsKeyJustUp(VirtualKey::B);
                }

                if (bDeletePressed)
                {
                    pBodyguardToDelete = &bg;
                }
            }
        }

        if (pBodyguardToDelete)
        {
            sub::BodyguardMenu::BodyguardManagement::DeleteBodyguard(*pBodyguardToDelete);
        }
    }

    // Group formation setting. Persisted in menyooConfig.ini [bodyguards] as
    // "formation_index" (BodyguardConfig.cpp). Indices match the SET_GROUP_FORMATION
    // ids (0 Default, 1 Circle Inward, 2 Circle North, 3 Line).
    int  g_formationIndex = 0;
    // Set by ReadBodyguardConfig (which must not call natives); consumed by
    // TickBodyguards on the first tick, like the wanted-lock re-arm pattern.
    bool g_formationApplyPending = false;

    void BodyguardOps_()
    {
        static int blipIndex = 0;
        static const std::vector<std::pair<int, std::string>> blipOptions =
        {
            { 1,   "Standard" },
            { 280, "Friend"   },
            { 480, "VIP"      }
        };

        static const std::vector<std::pair<int, std::string>> formationOptions =
        {
            { 0, "Default Formation" },
            { 1, "Circle (Inward)" },
            { 2, "Circle (North)" },
            { 3, "Line" }
        };

        AddTitle("Bodyguard Settings");

        AddBreak("--- Spawn Defaults ---");

        bool bHealth_plus = false, bHealth_minus = false, bHealth_input = false;
        AddNumber("Default Health", sub::BodyguardMenu::health, 0, bHealth_input, bHealth_plus, bHealth_minus);
        if (bHealth_plus && sub::BodyguardMenu::health < INT_MAX) ++sub::BodyguardMenu::health;
        if (bHealth_minus && sub::BodyguardMenu::health > 0) --sub::BodyguardMenu::health;
        if (bHealth_input)
        {
            std::string inputStr = Game::InputBox("", 5U, "Default Health", std::to_string(sub::BodyguardMenu::health));
            if (!inputStr.empty())
            {
                try { sub::BodyguardMenu::health = std::stoi(inputStr); }
                catch (...) { Game::Print::PrintBottomLeft("~r~" + Language::TranslateToSelected("Invalid input: ") + inputStr); }
            }
        }

        bool bArmor_plus = false, bArmor_minus = false, bArmor_input = false;
        AddNumber("Default Armor", sub::BodyguardMenu::armor, 0, bArmor_input, bArmor_plus, bArmor_minus);
        if (bArmor_plus && sub::BodyguardMenu::armor < INT_MAX) ++sub::BodyguardMenu::armor;
        if (bArmor_minus && sub::BodyguardMenu::armor > 0) --sub::BodyguardMenu::armor;
        if (bArmor_input)
        {
            std::string inputStr = Game::InputBox("", 5U, "Default Armor", std::to_string(sub::BodyguardMenu::armor));
            if (!inputStr.empty())
            {
                try { sub::BodyguardMenu::armor = std::stoi(inputStr); }
                catch (...) { Game::Print::PrintBottomLeft("~r~" + Language::TranslateToSelected("Invalid input: ") + inputStr); }
            }
        }

        bool oldGodmode = sub::BodyguardMenu::godmode;
        AddToggle("Godmode", sub::BodyguardMenu::godmode);
        if (oldGodmode != sub::BodyguardMenu::godmode)
        {
            for (auto& bg : sub::BodyguardMenu::BodyguardDb)
            {
                if (!bg.Handle.Exists()) continue;
                Ped ped = bg.Handle.GetHandle();
                if (sub::BodyguardMenu::godmode) SetPedInvincibleOn(ped);
                else SetPedInvincibleOff(ped);
            }
        }

        int roleIdx = (int)sub::BodyguardMenu::g_defaultSpawnRole;
        if (roleIdx < 0 || roleIdx >= (int)BodyguardRole::Count) roleIdx = 0;
        bool roleInput = false, role_plus = false, role_minus = false;
        AddTexter("Default Spawn Role", 0, { kRoleDisplayLabels[roleIdx] }, roleInput, role_plus, role_minus);
        if (role_plus)  roleIdx = (roleIdx + 1) % (int)BodyguardRole::Count;
        if (role_minus) roleIdx = (roleIdx == 0 ? (int)BodyguardRole::Count - 1 : roleIdx - 1);
        if (role_plus || role_minus)
            sub::BodyguardMenu::g_defaultSpawnRole = (BodyguardRole)roleIdx;

        // Spawn arming (auto-arm + spawn weapon) lives here with the other
        // future-spawn defaults; rendered by BodyguardMenu.cpp (owns the weapon tables).
        sub::BodyguardMenu::AddSpawnArmingOptions();

        AddBreak("--- Blips ---");

        bool oldRoleBlips = sub::BodyguardMenu::g_roleBlipsEnabled;
        AddToggle("Role Blips", sub::BodyguardMenu::g_roleBlipsEnabled);
        if (oldRoleBlips != sub::BodyguardMenu::g_roleBlipsEnabled)
            sub::BodyguardMenu::RefreshAllBodyguardBlips();

        // Legacy blip icon only has an effect while Role Blips is OFF, so hide it
        // otherwise instead of showing a dead option.
        if (!sub::BodyguardMenu::g_roleBlipsEnabled)
        {
            bool bBlipInput = false;
            bool icon_plus = false;
            bool icon_minus = false;
            AddTexter("Bodyguard Blip", 0, { blipOptions[blipIndex].second }, bBlipInput, icon_plus, icon_minus);
            if (icon_plus)
            {
                blipIndex = (blipIndex + 1) % blipOptions.size();
                sub::BodyguardMenu::blipIcon = blipOptions[blipIndex].first;
                sub::BodyguardMenu::RefreshAllBodyguardBlips();
            }
            if (icon_minus)
            {
                blipIndex = (blipIndex == 0 ? (int)blipOptions.size() - 1 : blipIndex - 1);
                sub::BodyguardMenu::blipIcon = blipOptions[blipIndex].first;
                sub::BodyguardMenu::RefreshAllBodyguardBlips();
            }
        }

        AddBreak("--- HUD ---");
        bool oldHud = sub::BodyguardMenu::g_hudEnabled;
        AddToggle("Bodyguard HUD", sub::BodyguardMenu::g_hudEnabled);
        if (oldHud && !sub::BodyguardMenu::g_hudEnabled)
            sub::BodyguardMenu::ReleaseAllBodyguardHeadshots();

        AddBreak("--- Behaviour ---");

        static const std::vector<std::string> combatLabels = { "Off", "Player Target", "Retaliate", "Wanted Threats", "Full Protection" };
        int combatIdx = sub::BodyguardMenu::g_combatResponseMode;
        if (combatIdx < 0 || combatIdx >= (int)combatLabels.size()) combatIdx = 0;
        bool combatInput = false, combat_plus = false, combat_minus = false;
        AddTexter("Combat Response", 0, { combatLabels[combatIdx] }, combatInput, combat_plus, combat_minus);
        if (combat_plus)  combatIdx = (combatIdx + 1) % (int)combatLabels.size();
        if (combat_minus) combatIdx = (combatIdx == 0 ? (int)combatLabels.size() - 1 : combatIdx - 1);
        if (combat_plus || combat_minus)
            sub::BodyguardMenu::g_combatResponseMode = combatIdx;

        AddToggle("Medic Revives/Heals", sub::BodyguardMenu::g_medicEnabled);
        AddToggle("Medic Heals Player", sub::BodyguardMenu::g_medicHealPlayer);
        AddToggle("Voice Lines", sub::BodyguardMenu::g_voiceLinesEnabled);

        static const std::vector<std::string> starLabels = { "1", "2", "3", "4", "5" };
        int starIdx = sub::BodyguardMenu::g_wantedThreatMinStars - 1;
        if (starIdx < 0 || starIdx >= (int)starLabels.size()) starIdx = 1;
        bool stInput = false, st_plus = false, st_minus = false;
        AddTexter("Wanted Response Min Stars", 0, { starLabels[starIdx] }, stInput, st_plus, st_minus);
        if (st_plus)  starIdx = (starIdx + 1) % (int)starLabels.size();
        if (st_minus) starIdx = (starIdx == 0 ? (int)starLabels.size() - 1 : starIdx - 1);
        if (st_plus || st_minus)
            sub::BodyguardMenu::g_wantedThreatMinStars = starIdx + 1;

        if (g_formationIndex < 0 || g_formationIndex >= (int)formationOptions.size())
            g_formationIndex = 0;
        bool bFormationInput = false;
        bool form_plus = false;
        bool form_minus = false;
        AddTexter("Formation", 0, { formationOptions[g_formationIndex].second }, bFormationInput, form_plus, form_minus);
        if (form_plus || form_minus)
        {
            if (form_plus)
                g_formationIndex = (g_formationIndex + 1) % (int)formationOptions.size();
            else
                g_formationIndex = (g_formationIndex == 0 ? (int)formationOptions.size() - 1 : g_formationIndex - 1);

            int playerGroup = PLAYER::GET_PLAYER_GROUP(PLAYER::PLAYER_ID());
            PED::SET_GROUP_FORMATION(playerGroup, formationOptions[g_formationIndex].first);
            if (g_formationSpacing > 0)
                PED::SET_GROUP_FORMATION_SPACING(playerGroup, (float)g_formationSpacing, -1.f, -1.f);
            g_formationApplyPending = false; // just applied directly
        }

        bool bSpacingInput = false, bSpacingPlus = false, bSpacingMinus = false;
        AddNumber("Formation Spacing", g_formationSpacing, 0, bSpacingInput, bSpacingPlus, bSpacingMinus);
        if (bSpacingPlus && g_formationSpacing < 10) ++g_formationSpacing;
        if (bSpacingMinus && g_formationSpacing > 0) --g_formationSpacing;
        if (bSpacingInput)
        {
            std::string inputStr = Game::InputBox("", 2U, "Formation Spacing", std::to_string(g_formationSpacing));
            if (!inputStr.empty())
            {
                try { g_formationSpacing = (std::min)(10, (std::max)(0, std::stoi(inputStr))); }
                catch (...) { Game::Print::PrintBottomLeft("~r~" + Language::TranslateToSelected("Invalid input: ") + inputStr); }
            }
        }
        if (bSpacingPlus || bSpacingMinus || bSpacingInput)
        {
            int playerGroup = PLAYER::GET_PLAYER_GROUP(PLAYER::PLAYER_ID());
            if (g_formationSpacing > 0)
                PED::SET_GROUP_FORMATION_SPACING(playerGroup, (float)g_formationSpacing, -1.f, -1.f);
            else
                PED::SET_GROUP_FORMATION_SPACING(playerGroup, 0.f, -1.f, -1.f);
            g_formationApplyPending = false;
        }
    }
}


#include "..\..\Menu\submenu_switch.h"
#include "..\..\Menu\submenu_enum.h"
REGISTER_SUBMENU(BODYGUARD_LIST,        sub::BodyguardMenu::BodyguardList)
REGISTER_SUBMENU(BODYGUARD_SETTINGS,    sub::BodyguardMenu::BodyguardOps_)
