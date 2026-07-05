#include "BodyguardSquads.h"

#include "BodyguardManagement.h"
#include "BodyguardSpawn.h"
#include "BodyguardRole.h"
#include "BodyguardEscort.h"

#include "../../Menu/Menu.h"
#include "../../Menu/Language.h"
#include "../../Scripting/Game.h"
#include "../../Scripting/Model.h"
#include "../../Util/ExePath.h"
#include "../../Util/StringManip.h"

#include <pugixml\src\pugixml.hpp>

namespace sub::BodyguardMenu
{
    std::vector<SquadDef> g_squads;

    // Index of the squad currently open in the edit submenu.
    static int s_selectedSquadIndex = 0;
    static int s_selectedMemberIndex = 0;

    // ---- Canonical default squad table -----------------------------------
    // Unique characters appear once. Police uses s_m_y_cop_01.
    struct DefaultSquadMember { const char* model; BodyguardRole role; int count; };
    struct DefaultSquad { const char* name; const DefaultSquadMember* members; int memberCount; };

    static const DefaultSquadMember kMilitaryMembers[] = {
        { "s_m_y_marine_01", BodyguardRole::Rifleman, 2 },
        { "s_m_m_marine_01", BodyguardRole::Heavy,    1 },
    };
    static const DefaultSquadMember kPoliceMembers[] = {
        { "s_m_y_cop_01",  BodyguardRole::Rifleman, 2 },
        { "s_m_y_swat_01", BodyguardRole::Heavy,    1 },
    };
    static const DefaultSquadMember kFibMembers[] = {
        { "s_m_m_fibsec_01",    BodyguardRole::Rifleman, 2 },
        { "s_m_m_fiboffice_01", BodyguardRole::Medic,    1 },
    };
    static const DefaultSquadMember kBlackOpsMembers[] = {
        { "s_m_y_blackops_01", BodyguardRole::Driver,   1 },
        { "s_m_y_blackops_02", BodyguardRole::Rifleman, 2 },
        { "s_m_y_blackops_03", BodyguardRole::Sniper,   1 },
    };
    static const DefaultSquadMember kMichaelMembers[] = {
        { "player_zero", BodyguardRole::Rifleman, 1 },
    };
    static const DefaultSquadMember kTrevorMembers[] = {
        { "player_two", BodyguardRole::Heavy, 1 },
    };
    static const DefaultSquadMember kFranklinMembers[] = {
        { "player_one", BodyguardRole::Sniper, 1 },
    };

    static const DefaultSquad kDefaultSquads[] = {
        { "Military", kMilitaryMembers, (int)(sizeof(kMilitaryMembers) / sizeof(kMilitaryMembers[0])) },
        { "Police",   kPoliceMembers,   (int)(sizeof(kPoliceMembers)   / sizeof(kPoliceMembers[0]))   },
        { "FIB",      kFibMembers,      (int)(sizeof(kFibMembers)      / sizeof(kFibMembers[0]))      },
        { "Black Ops", kBlackOpsMembers, (int)(sizeof(kBlackOpsMembers) / sizeof(kBlackOpsMembers[0])) },
        { "Michael",  kMichaelMembers,  (int)(sizeof(kMichaelMembers)  / sizeof(kMichaelMembers[0]))  },
        { "Trevor",   kTrevorMembers,   (int)(sizeof(kTrevorMembers)   / sizeof(kTrevorMembers[0]))   },
        { "Franklin", kFranklinMembers, (int)(sizeof(kFranklinMembers) / sizeof(kFranklinMembers[0])) },
    };
    static const int kDefaultSquadCount = (int)(sizeof(kDefaultSquads) / sizeof(kDefaultSquads[0]));

    static SquadDef BuildDefaultSquad(const DefaultSquad& src)
    {
        SquadDef def;
        def.name = src.name;
        def.isDefault = true;
        for (int i = 0; i < src.memberCount; ++i)
        {
            SquadMember m;
            m.model = src.members[i].model;
            m.role = src.members[i].role;
            m.count = src.members[i].count;
            def.members.push_back(m);
        }
        return def;
    }

    static int FindDefaultSquadIndexByName(const std::string& name)
    {
        for (int i = 0; i < kDefaultSquadCount; ++i)
        {
            if (name == kDefaultSquads[i].name)
                return i;
        }
        return -1;
    }

    static bool HasSquadNamed(const std::vector<SquadDef>& squads, const char* name)
    {
        for (const auto& squad : squads)
        {
            if (squad.name == name)
                return true;
        }
        return false;
    }

    static int NextSquadSpawnIndex(const std::string& squadName)
    {
        const std::string prefix = squadName + " #";
        int maxIndex = 0;
        for (const auto& bg : BodyguardDb)
        {
            if (bg.Name.rfind(prefix, 0) != 0)
                continue;

            const std::string suffix = bg.Name.substr(prefix.size());
            try
            {
                int idx = std::stoi(suffix);
                if (idx > maxIndex)
                    maxIndex = idx;
            }
            catch (...)
            {
            }
        }
        return maxIndex + 1;
    }

    static bool EnsureDefaultSquadsPresent(std::vector<SquadDef>& squads)
    {
        bool changed = false;
        for (int i = 0; i < kDefaultSquadCount; ++i)
        {
            if (HasSquadNamed(squads, kDefaultSquads[i].name))
                continue;

            squads.push_back(BuildDefaultSquad(kDefaultSquads[i]));
            changed = true;
        }
        return changed;
    }

    std::vector<SquadDef> DefaultSquadsVector()
    {
        std::vector<SquadDef> out;
        for (int i = 0; i < kDefaultSquadCount; ++i)
            out.push_back(BuildDefaultSquad(kDefaultSquads[i]));
        return out;
    }

    void LoadDefaultSquads()
    {
        g_squads = DefaultSquadsVector();
    }

    static std::string SquadsXmlPath()
    {
        return GetPathffA(Pathff::Main, true) + "Squads.xml";
    }

    void SaveSquads()
    {
        pugi::xml_document doc;
        auto nodeDecl = doc.append_child(pugi::node_declaration);
        nodeDecl.append_attribute("version") = "1.0";
        nodeDecl.append_attribute("encoding") = "ISO-8859-1";

        auto nodeRoot = doc.append_child("Squads");
        for (const auto& def : g_squads)
        {
            auto nodeSquad = nodeRoot.append_child("Squad");
            nodeSquad.append_attribute("name") = def.name.c_str();
            nodeSquad.append_attribute("isDefault") = def.isDefault;
            for (const auto& m : def.members)
            {
                auto nodeMember = nodeSquad.append_child("Member");
                nodeMember.append_attribute("model") = m.model.c_str();
                nodeMember.append_attribute("role") = RoleKey(m.role);
                nodeMember.append_attribute("count") = m.count;
            }
        }

        doc.save_file(SquadsXmlPath().c_str());
    }

    void LoadSquads()
    {
        pugi::xml_document doc;
        if (doc.load_file(SquadsXmlPath().c_str()).status != pugi::status_ok)
        {
            LoadDefaultSquads();
            SaveSquads();
            return;
        }

        std::vector<SquadDef> loaded;
        pugi::xml_node nodeRoot = doc.child("Squads");
        for (auto nodeSquad = nodeRoot.child("Squad"); nodeSquad; nodeSquad = nodeSquad.next_sibling("Squad"))
        {
            SquadDef def;
            def.name = nodeSquad.attribute("name").as_string();
            def.isDefault = nodeSquad.attribute("isDefault").as_bool(false);
            for (auto nodeMember = nodeSquad.child("Member"); nodeMember; nodeMember = nodeMember.next_sibling("Member"))
            {
                SquadMember m;
                m.model = nodeMember.attribute("model").as_string();
                m.role = RoleFromKey(nodeMember.attribute("role").as_string());
                m.count = nodeMember.attribute("count").as_int(1);
                if (m.count < 1) m.count = 1;
                def.members.push_back(m);
            }
            loaded.push_back(def);
        }

        if (loaded.empty())
        {
            LoadDefaultSquads();
            SaveSquads();
            return;
        }

        bool changed = EnsureDefaultSquadsPresent(loaded);
        g_squads = loaded;
        if (changed)
            SaveSquads();
    }

    void ResetSquadToDefault(int index)
    {
        if (index < 0 || index >= (int)g_squads.size())
            return;

        int defaultIndex = FindDefaultSquadIndexByName(g_squads[index].name);
        if (defaultIndex < 0)
            return;

        g_squads[index].isDefault = true;
        g_squads[index].members = BuildDefaultSquad(kDefaultSquads[defaultIndex]).members;
        SaveSquads();
    }

    void ResetAllSquads()
    {
        LoadDefaultSquads();
        SaveSquads();
    }

    void SpawnSquad(const SquadDef& def)
    {
        int spawned = 0;
        int memberIdx = NextSquadSpawnIndex(def.name);
        for (const auto& m : def.members)
        {
            for (int i = 0; i < m.count; ++i)
            {
                if (BodyguardDb.size() >= BodyguardManagement::MAX_BODYGUARDS)
                    break;

                GTAmodel::Model model(m.model);
                std::string bgName = def.name + " #" + std::to_string(memberIdx++);
                Ped ped = BodyguardManagement::SpawnBodyguardPed(model, bgName, m.role, true);
                if (ped != 0)
                    ++spawned;
            }
            if (BodyguardDb.size() >= BodyguardManagement::MAX_BODYGUARDS)
                break;
        }

        if (spawned > 0 && g_escortAutoAssignNewSpawns && HasActiveEscortConvoy())
            AssignBodyguardsToEscort();

        Game::Print::PrintBottomLeft(Language::TranslateToSelected("Squad members spawned: ") + std::to_string(spawned));
    }

    void BodyguardSquadsMenu()
    {
        AddTitle("Squads");

        if (g_squads.empty())
            LoadSquads();

        for (int i = 0; i < (int)g_squads.size(); ++i)
        {
            bool bSelect = false;
            AddOption(g_squads[i].name, bSelect, nullFunc, SUB::BODYGUARD_SQUAD_EDIT);
            if (bSelect)
                s_selectedSquadIndex = i;
        }

        bool bNew = false;
        AddOption("New Squad...", bNew);
        if (bNew)
        {
            std::string name = Game::InputBox("", 32U, "Squad Name", "");
            if (name.empty())
            {
                Game::Print::PrintBottomLeft("Squad name cannot be empty.");
            }
            else if (!IsSafePath(name))
            {
                Game::Print::PrintBottomLeft("Invalid squad name.");
            }
            else if (HasSquadNamed(g_squads, name.c_str()))
            {
                Game::Print::PrintBottomLeft("A squad with that name already exists.");
            }
            else
            {
                SquadDef def;
                def.name = name;
                def.isDefault = false;
                g_squads.push_back(def);
                SaveSquads();
                Game::Print::PrintBottomLeft(Language::TranslateToSelected("Squad created: ") + name);
            }
        }

        bool bResetAll = false;
        AddOption("Reset All Squads To Default", bResetAll);
        if (bResetAll)
        {
            ResetAllSquads();
            Game::Print::PrintBottomLeft("All squads have been reset.");
        }
    }

    void BodyguardSquadEdit()
    {
        if (s_selectedSquadIndex < 0 || s_selectedSquadIndex >= (int)g_squads.size())
        {
            AddTitle("Squad");
            AddOption("No squad selected");
            return;
        }

        SquadDef& def = g_squads[s_selectedSquadIndex];

        AddTitle(def.name);

        bool bSpawn = false;
        AddOption("Spawn Squad", bSpawn);
        if (bSpawn)
            SpawnSquad(def);

        for (int i = 0; i < (int)def.members.size(); ++i)
        {
            SquadMember& m = def.members[i];
            int roleIdx = (int)m.role;
            if (roleIdx < 0 || roleIdx >= (int)kRoleDisplayLabels.size())
                roleIdx = 0;

            bool bSelect = false;
            AddOption(m.model + " (" + kRoleDisplayLabels[roleIdx] + " x" + std::to_string(m.count) + ")", bSelect, nullFunc, SUB::BODYGUARD_SQUAD_MEMBER_EDIT);
            if (bSelect)
                s_selectedMemberIndex = i;
        }

        bool bAddMember = false;
        AddOption("Add Member...", bAddMember);
        if (bAddMember)
        {
            std::string model = Game::InputBox("", 32U, "Ped Model Name", "");
            if (model.empty())
            {
                Game::Print::PrintBottomLeft("Model name cannot be empty.");
            }
            else
            {
                // Model name is a technical ped model (never translated). Validate it exists.
                GTAmodel::Model mdl(model);
                if (!mdl.IsInCdImage())
                {
                    Game::Print::PrintBottomLeft("Unknown ped model.");
                }
                else
                {
                    SquadMember nm;
                    nm.model = model;
                    nm.role = BodyguardRole::Rifleman;
                    nm.count = 1;
                    def.members.push_back(nm);
                    SaveSquads();
                    Game::Print::PrintBottomLeft(Language::TranslateToSelected("Member added: ") + model);
                }
            }
        }

        bool bReset = false;
        AddOption("Reset This Squad", bReset);
        if (bReset)
        {
            ResetSquadToDefault(s_selectedSquadIndex);
            Game::Print::PrintBottomLeft("Squad reset.");
        }

    }

    void BodyguardSquadMemberEdit()
    {
        if (s_selectedSquadIndex < 0 || s_selectedSquadIndex >= (int)g_squads.size())
        {
            AddTitle("Squad Member");
            AddOption("No squad selected");
            return;
        }

        SquadDef& def = g_squads[s_selectedSquadIndex];
        if (s_selectedMemberIndex < 0 || s_selectedMemberIndex >= (int)def.members.size())
        {
            AddTitle(def.name);
            AddOption("No member selected");
            return;
        }

        SquadMember& m = def.members[s_selectedMemberIndex];
        AddTitle(m.model);

        int roleIdx = (int)m.role;
        if (roleIdx < 0 || roleIdx >= (int)kRoleDisplayLabels.size())
            roleIdx = 0;

        bool rInput = false, rPlus = false, rMinus = false;
        // Single-element display array -> the selected index MUST be 0, otherwise
        // AddTexter falls back to printing the raw index instead of the label.
        AddTexter("Role", 0, { kRoleDisplayLabels[roleIdx] }, rInput, rPlus, rMinus);
        if (rPlus)  roleIdx = (roleIdx + 1) % (int)kRoleDisplayLabels.size();
        if (rMinus) roleIdx = (roleIdx == 0 ? (int)kRoleDisplayLabels.size() - 1 : roleIdx - 1);
        if (rPlus || rMinus)
        {
            m.role = (BodyguardRole)roleIdx;
            SaveSquads();
        }

        bool cInput = false, cPlus = false, cMinus = false;
        AddNumber("Count", m.count, 0, cInput, cPlus, cMinus);
        if (cPlus && m.count < (int)BodyguardManagement::MAX_BODYGUARDS)
        {
            ++m.count;
            SaveSquads();
        }
        if (cMinus && m.count > 1)
        {
            --m.count;
            SaveSquads();
        }

        bool bRemove = false;
        AddOption("Remove Member", bRemove);
        if (bRemove)
        {
            def.members.erase(def.members.begin() + s_selectedMemberIndex);
            if (s_selectedMemberIndex >= (int)def.members.size())
                s_selectedMemberIndex = (int)def.members.size() - 1;
            SaveSquads();
            Game::Print::PrintBottomLeft("Squad member removed.");
        }
    }
}

#include "..\..\Menu\submenu_switch.h"
#include "..\..\Menu\submenu_enum.h"
REGISTER_SUBMENU(BODYGUARD_SQUADS,            sub::BodyguardMenu::BodyguardSquadsMenu)
REGISTER_SUBMENU(BODYGUARD_SQUAD_EDIT,        sub::BodyguardMenu::BodyguardSquadEdit)
REGISTER_SUBMENU(BODYGUARD_SQUAD_MEMBER_EDIT, sub::BodyguardMenu::BodyguardSquadMemberEdit)
