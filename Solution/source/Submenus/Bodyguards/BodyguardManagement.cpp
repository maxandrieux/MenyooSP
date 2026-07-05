#include "BodyguardManagement.h"

#include "../../Menu/Menu.h"
#include "../../Scripting/Game.h"
#include "../../Scripting/GTAped.h"
#include "../../Scripting/GTAentity.h"
#include "../../Scripting/GTAblip.h"
#include "../../Natives/natives.h"

#include <algorithm>
#include "../../Util/StringManip.h"
#include "../../Scripting/Model.h"
#include "../../Scripting/World.h"

#include "BodyguardSpawn.h"     // s_bodyguards
#include "BodyguardSettings.h"  // g_selectedBodyguardHandle
#include "BodyguardMenu.h"      // RemoveBodyguardBlip
#include "BodyguardEscort.h"    // ClearEscortState
#include "BodyguardChauffeur.h" // StopChauffeur
#include "BodyguardHud.h"       // ReleaseAllBodyguardHeadshots
#include "BodyguardDebug.h"     // dbg::Log

namespace sub::BodyguardMenu
{
	bool operator==(const BodyguardEntity& left, const BodyguardEntity& right)
	{
		return left.Handle == right.Handle;
	}
	bool operator!=(const BodyguardEntity& left, const BodyguardEntity& right)
	{
		return !(left == right);
	}

	std::vector<BodyguardEntity> BodyguardDb;
}

namespace sub::BodyguardMenu
{
	namespace BodyguardManagement
	{
		// <- changed UINT -> unsigned int
		unsigned int GetNumberOfBodyguardsSpawned(const EntityType& type)
		{
			switch (type)
			{
			case EntityType::ALL:
				return static_cast<unsigned int>(BodyguardDb.size());
			default:
				return static_cast<unsigned int>(std::count_if(
					BodyguardDb.begin(),
					BodyguardDb.end(),
					[type](const BodyguardEntity& item)
					{
						return item.Type == type;
					}));
			}
		}

		int GetBodyguardIndexInDb(const GTAentity& entity)
		{
			for (int i = 0; i < static_cast<int>(BodyguardDb.size()); ++i)
			{
				if (BodyguardDb[i].Handle == entity)
					return i;
			}
			return -1;
		}

		int GetBodyguardIndexInDb(const BodyguardEntity& ent)
		{
			return GetBodyguardIndexInDb(ent.Handle);
		}

		void AddBodyguardToDb(BodyguardEntity ent)
		{
			if (!ent.Handle.Exists())
				return;

			if (ent.HashName.empty())
				ent.HashName = IntToHexString(ent.Handle.Model().hash, true);

			BodyguardDb.push_back(std::move(ent));
		}

		void DbgLogSquadState(const std::string& tag)
		{
			const size_t db = BodyguardDb.size();
			const size_t sb = s_bodyguards.size();
			std::string m = tag
				+ " | db=" + std::to_string(db)
				+ " s_bg=" + std::to_string(sb)
				+ " sel=" + std::to_string(sub::BodyguardMenu::g_selectedBodyguardHandle);
			if (db != sb)
				m += "  [WARN DESYNC db!=s_bg]";
			dbg::Log(m);
		}

		void RemoveBodyguardByHandle(Ped handle)
		{
			if (handle == 0)
				return;

			BodyguardDb.erase(
				std::remove_if(BodyguardDb.begin(), BodyguardDb.end(),
					[handle](const BodyguardEntity& e)
					{
						return e.Handle.GetHandle() == handle;
					}),
				BodyguardDb.end());

			s_bodyguards.erase(
				std::remove(s_bodyguards.begin(), s_bodyguards.end(), handle),
				s_bodyguards.end());
		}

		void RemoveBodyguardFromDb(const BodyguardEntity& ent)
		{
			// Capture the handle by value first: erasing reorders/destroys vector
			// elements, and 'ent' may itself be a reference into BodyguardDb.
			Ped handle = ent.Handle.GetHandle();
			RemoveBodyguardByHandle(handle);
		}

		void DeleteBodyguard(BodyguardEntity& ent)
		{
			// Capture the real handle BEFORE any mutation. PED::DELETE_PED zeroes the
			// pointer it receives, so keep a separate copy for the purge step.
			Ped ped = ent.Handle.GetHandle();

			// Restore escort/group state (and drop any spawned escort vehicle) while the
			// record is still valid, before the ped is deleted and the entry is purged.
			if (sub::BodyguardMenu::IsChauffeurPed(ped))
				sub::BodyguardMenu::StopChauffeur();
			ClearEscortState(ent);

			if (ent.Handle.Exists())
			{
				ent.Handle.RequestControl();

				GTAblip blip = ent.Handle.CurrentBlip();
				if (blip.Exists())
					blip.Remove();

				ent.Handle.Detach();
				ent.Handle.SetMissionEntity(false);

				Ped delHandle = ped;
				if (ped && ENTITY::DOES_ENTITY_EXIST(ped))
					PED::DELETE_PED(&delHandle);
			}

			// Purge tracking AFTER the ped work, using the captured (non-zeroed) handle.
			// Do not touch 'ent' past this point: it may have been erased/moved.
			RemoveBodyguardByHandle(ped);

			if (sub::BodyguardMenu::g_selectedBodyguardHandle == ped)
				sub::BodyguardMenu::g_selectedBodyguardHandle = 0;

			DbgLogSquadState("DELETE ped=" + std::to_string(ped));
		}

		int CleanupDeadBodyguards()
		{
			int removed = 0;

			for (size_t i = 0; i < BodyguardDb.size(); )
			{
				BodyguardEntity& bg = BodyguardDb[i];

				if (IsBodyguardAlive(bg))
				{
					++i;
					continue;
				}

				Ped handle = bg.Handle.GetHandle();

				dbg::Log(std::string("CLEANUP_REMOVE handle=") + std::to_string(handle)
					+ " exists=" + (bg.Handle.Exists() ? "1" : "0")
					+ " dead=" + ((bg.Handle.Exists() && bg.Handle.IsDead()) ? "1" : "0"));

				// Restore escort/group state and drop any spawned escort vehicle while the
				// record is still valid, before erasing it from the DB.
				if (sub::BodyguardMenu::IsChauffeurPed(handle))
					sub::BodyguardMenu::StopChauffeur();
				ClearEscortState(bg);

				// Remove the blip if the ped still exists (dead-but-present case); safe if missing.
				sub::BodyguardMenu::RemoveBodyguardBlip(handle);

				BodyguardDb.erase(BodyguardDb.begin() + i);

				s_bodyguards.erase(
					std::remove(s_bodyguards.begin(), s_bodyguards.end(), handle),
					s_bodyguards.end());

				if (sub::BodyguardMenu::g_selectedBodyguardHandle == handle)
					sub::BodyguardMenu::g_selectedBodyguardHandle = 0;

				++removed;
			}

			DbgLogSquadState("CLEANUP removed=" + std::to_string(removed));
			return removed;
		}

		void DismissAllBodyguards()
		{
			if (sub::BodyguardMenu::IsChauffeurActive())
				sub::BodyguardMenu::StopChauffeur();

			for (BodyguardEntity& bg : BodyguardDb)
			{
				Ped ped = bg.Handle.GetHandle();

				// Restore escort/group state and drop any spawned escort vehicle while the
				// record is still valid, before the ped is deleted.
				ClearEscortState(bg);

				if (bg.Handle.Exists())
				{
					bg.Handle.RequestControl();

					GTAblip blip = bg.Handle.CurrentBlip();
					if (blip.Exists())
						blip.Remove();

					bg.Handle.Detach();
					bg.Handle.SetMissionEntity(false);

					Ped delHandle = ped;
					if (ped && ENTITY::DOES_ENTITY_EXIST(ped))
						PED::DELETE_PED(&delHandle);
				}
			}

			BodyguardDb.clear();
			s_bodyguards.clear();
			sub::BodyguardMenu::g_selectedBodyguardHandle = 0;

			// All peds are gone: drop any now-empty mod-spawned escort vehicle
			// immediately instead of waiting for the (DB-empty) tick.
			CleanupEscortVehicles();
			sub::BodyguardMenu::ReleaseAllBodyguardHeadshots();

			DbgLogSquadState("DISMISS_ALL");
		}
				
		void ShowArrowAboveEntity(const GTAentity& ent, RGBA colour)
		{
			if (ent.Exists())
			{
				const auto& soe_pos = ent.GetPosition();
				const auto& soe_md = ent.ModelDimensions();
				const auto& markerPos = soe_pos + Vector3(0, 0, (std::max)(soe_md.Dim1.z, soe_md.Dim2.z) + 0.20f); // May not be at the right position if the entity is tilted
				World::DrawMarker(MarkerType::UpsideDownCone, markerPos, Vector3(), Vector3(), Vector3(0.45f, 0.45f, 0.50f), RGBA(190, 0, 0, 190));
			}

		
		}
	}
}
