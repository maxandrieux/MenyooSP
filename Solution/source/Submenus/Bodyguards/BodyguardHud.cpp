#include "BodyguardHud.h"

#include "../../Natives/natives.h"
#include "../../Natives/natives2.h"
#include "../../Scripting/Game.h"
#include "../../Scripting/WeaponIndivs.h"
#include "BodyguardManagement.h"
#include "BodyguardMenu.h"
#include "BodyguardSpawn.h"

#include <algorithm>
#include <string>
#include <vector>

namespace sub::BodyguardMenu
{
    bool g_hudEnabled = true;
}

namespace
{
    using namespace sub::BodyguardMenu;

    struct HeadshotSlot
    {
        Ped ped{ 0 };
        int id{ -1 };
        bool ready{ false };
        std::string txd;
        int registeredAt{ 0 };
    };

    static std::vector<HeadshotSlot> s_headshots;

    float Clamp01(float v)
    {
        if (v < 0.f) return 0.f;
        if (v > 1.f) return 1.f;
        return v;
    }

    bool IsTrackedPed(Ped ped)
    {
        for (auto& bg : BodyguardDb)
        {
            if (bg.Handle.Exists() && bg.Handle.GetHandle() == ped)
                return true;
        }
        return false;
    }

    void ReleaseSlot(size_t index)
    {
        if (index >= s_headshots.size())
            return;

        if (s_headshots[index].id != -1)
            PED::UNREGISTER_PEDHEADSHOT(s_headshots[index].id);

        s_headshots.erase(s_headshots.begin() + index);
    }

    HeadshotSlot* FindSlot(Ped ped)
    {
        for (auto& slot : s_headshots)
        {
            if (slot.ped == ped)
                return &slot;
        }
        return nullptr;
    }

    void RegisterHeadshot(Ped ped, int now)
    {
        if (s_headshots.size() >= BodyguardManagement::MAX_BODYGUARDS)
            return;

        int id = PED::REGISTER_PEDHEADSHOT(ped);
        if (id == -1)
            return;

        s_headshots.push_back({ ped, id, false, "", now });
    }

    void MaintainHeadshots()
    {
        const int now = MISC::GET_GAME_TIMER();

        for (size_t i = 0; i < s_headshots.size(); )
        {
            HeadshotSlot& slot = s_headshots[i];
            if (!ENTITY::DOES_ENTITY_EXIST(slot.ped) || !IsTrackedPed(slot.ped))
            {
                ReleaseSlot(i);
                continue;
            }

            if (slot.id != -1 && !slot.ready &&
                PED::IS_PEDHEADSHOT_VALID(slot.id) &&
                PED::IS_PEDHEADSHOT_READY(slot.id))
            {
                const char* txd = PED::GET_PEDHEADSHOT_TXD_STRING(slot.id);
                if (txd && txd[0] != '\0')
                {
                    slot.txd = txd;
                    slot.ready = true;
                }
            }

            if (slot.id != -1 && now - slot.registeredAt > 10000)
            {
                // Lazy appearance refresh: free the old slot BEFORE re-registering,
                // otherwise at MAX_BODYGUARDS the new registration is refused by the
                // size cap and the avatar blinks for one management tick.
                const Ped ped = slot.ped;
                PED::UNREGISTER_PEDHEADSHOT(slot.id);
                slot.id = -1;
                ReleaseSlot(i);
                RegisterHeadshot(ped, now);
                continue;
            }

            ++i;
        }

        for (auto& bg : BodyguardDb)
        {
            if (!bg.Handle.Exists())
                continue;

            Ped ped = bg.Handle.GetHandle();
            if (ped != 0 && ENTITY::DOES_ENTITY_EXIST(ped) && !FindSlot(ped))
                RegisterHeadshot(ped, now);
        }
    }

    std::string WeaponLabel(Ped ped)
    {
        Hash weapon = 0;
        if (WEAPON::GET_CURRENT_PED_WEAPON(ped, &weapon, TRUE) && weapon != 0)
            return GetWeaponLabel(weapon, true);
        return "Unarmed";
    }

    void DrawBar(float left, float y, float width, float height, float ratio, RGBA fill, RGBA back)
    {
        const float centerX = left + width * 0.5f;
        GRAPHICS::DRAW_RECT(centerX, y, width, height, back.R, back.G, back.B, back.A, false);
        const float fillWidth = width * Clamp01(ratio);
        if (fillWidth > 0.0001f)
            GRAPHICS::DRAW_RECT(left + fillWidth * 0.5f, y, fillWidth, height, fill.R, fill.G, fill.B, fill.A, false);
    }
}

namespace sub::BodyguardMenu
{
    void ReleaseAllBodyguardHeadshots()
    {
        for (auto& slot : s_headshots)
        {
            if (slot.id != -1)
                PED::UNREGISTER_PEDHEADSHOT(slot.id);
        }
        s_headshots.clear();
    }

    void TickBodyguardHud()
    {
        static int s_nextManageAt = 0;
        const int now = MISC::GET_GAME_TIMER();

        if (!g_hudEnabled)
        {
            if (!s_headshots.empty())
                ReleaseAllBodyguardHeadshots();
            return;
        }

        if (now >= s_nextManageAt)
        {
            s_nextManageAt = now + 500;
            MaintainHeadshots();
        }

        if (BodyguardDb.empty())
            return;
        if (CUTSCENE::IS_CUTSCENE_PLAYING() || HUD::IS_PAUSE_MENU_ACTIVE())
            return;

        const float rowW = 0.150f;
        const float rowH = 0.036f;
        const float gap = 0.004f;
        const float x = 1.0f - rowW / 2.0f - 0.005f;
        const float left = x - rowW / 2.0f;
        float y = 0.975f - rowH / 2.0f;
        int rows = 0;

        for (auto& bg : BodyguardDb)
        {
            if (rows >= (int)BodyguardManagement::MAX_BODYGUARDS)
                break;
            if (!bg.Handle.Exists())
                continue;

            Ped ped = bg.Handle.GetHandle();
            if (ped == 0 || !ENTITY::DOES_ENTITY_EXIST(ped))
                continue;

            const int hp = ENTITY::GET_ENTITY_HEALTH(ped);
            const int maxHp = (std::max)(1, ENTITY::GET_ENTITY_MAX_HEALTH(ped));
            const int pedArmor = PED::GET_PED_ARMOUR(ped);
            const bool dead = hp <= 0 || PED::IS_PED_DEAD_OR_DYING(ped, true);
            const float alpha = dead ? 120.f : 255.f;

            GRAPHICS::DRAW_RECT(x, y, rowW, rowH, dead ? 60 : 0, 0, 0, 170, false);

            const float avatarW = 0.020f;
            const float avatarH = 0.032f;
            const float avatarX = left + 0.013f;
            HeadshotSlot* slot = FindSlot(ped);
            if (slot && slot->ready && !slot->txd.empty())
                GRAPHICS::DRAW_SPRITE(slot->txd.c_str(), slot->txd.c_str(), avatarX, y, avatarW, avatarH, 0.f, 255, dead ? 120 : 255, dead ? 120 : 255, (int)alpha, false, 0);
            else
                GRAPHICS::DRAW_RECT(avatarX, y, avatarW, avatarH, 45, 45, 45, 180, false);

            std::string label = (!bg.Name.empty() ? bg.Name : bg.HashName) + " - " + WeaponLabel(ped);
            if (label.size() > 28)
                label = label.substr(0, 25) + "...";

            Game::Print::SetupDraw(0, Vector2(0.f, 0.20f), false, false, false,
                dead ? RGBA(170, 170, 170, 230) : RGBA(255, 255, 255, 245));
            Game::Print::drawstring(label, left + 0.026f, y - 0.014f);

            const float barLeft = left + 0.026f;
            const float barW = rowW - 0.032f;
            DrawBar(barLeft, y + 0.004f, barW, 0.0055f, dead ? 0.f : (float)hp / (float)maxHp,
                RGBA(70, 210, 90, 225), RGBA(45, 45, 45, 190));
            DrawBar(barLeft, y + 0.012f, barW, 0.0055f, dead ? 0.f : (float)pedArmor / (float)(std::max)(1, armor),
                RGBA(60, 140, 240, 225), RGBA(45, 45, 45, 190));

            y -= (rowH + gap);
            ++rows;
        }
    }
}
