# MenyooSP Bodyguard Dynamic Workflow Context

This document is the local context file for a new Claude Code conversation.
Read it before planning or implementing anything.

## Repository

- Root: `C:\Users\maxan\Documents\Codex\2026-06-24\j-ai-un-objectif-pour-toi\work\MenyooSP`
- Project: C++ GTA V MenyooSP trainer.
- Build generation: `generate.bat` runs `Solution\external\premake\premake5.exe vs2022`.
- Source glob: `premake5.lua` includes `Solution/source/**.h` and `Solution/source/**.cpp`; after adding new source files, rerun Premake before MSBuild.
- Build target: `Release | x64`, output `Solution/source/_Build/bin/Release/Menyoo.asi`.
- Existing local changes are present in bodyguard files. Do not revert or overwrite user/local changes.

## Current Bodyguard Files

Inspect these first:

- `Solution/source/Submenus/Bodyguards/BodyguardManagement.h`
- `Solution/source/Submenus/Bodyguards/BodyguardManagement.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardMenu.h`
- `Solution/source/Submenus/Bodyguards/BodyguardMenu.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardSettings.h`
- `Solution/source/Submenus/Bodyguards/BodyguardSettings.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardSpawn.h`
- `Solution/source/Submenus/Bodyguards/BodyguardSpawn.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardSubmenu.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardDebug.h`
- `Solution/source/Menu/submenu_enum.h`
- `Solution/source/Menu/Routine.cpp`
- `Solution/source/Menu/MenuConfig.cpp`
- `Solution/source/Menu/Language.cpp`
- `Solution/source/Menu/Language.h`
- `Solution/source/_Build/bin/Release/menyooStuff/Language/French.json`
- `Solution/source/_Build/bin/Release/menyooStuff/PedList.xml`

## Existing Bodyguard Baseline

The current local bodyguard work already includes:

- Stable bodyguard selection by ped handle, not raw pointer into `BodyguardDb`.
- `BodyguardDb` plus `s_bodyguards`; keep them in sync.
- `BodyguardDebug.h` logging.
- Basic squad tools:
  - arm all
  - auto-arm new bodyguards
  - spawn weapon
  - heal all
  - refill armor
  - cleanup dead bodyguards
  - dismiss all bodyguards
  - simple hardcoded squad presets
- Blips with death sprite support, but death update must be called from an always-on tick.

Treat that as the baseline. Do not remove it unless replacing it with a strictly better equivalent.

## User Requirements

Implement all of these in one coherent delivery:

1. Bodyguard Vehicle Escort
   - Use My Seats First
   - Spawn Escort Vehicle If Full
   - Escort Vehicle Model
   - Escort Driving Style

2. Squad Blip Colors
   - bodyguard blip color/icon by role
   - at minimum Medic and Driver must be visibly distinct

3. Role System
   - each bodyguard has a role:
     - Driver
     - Rifleman
     - Shotgun
     - Sniper
     - Heavy
     - Medic
   - role controls default weapon, accuracy/combat tuning, firing pattern, blip style.

4. Squads
   - default squads:
     - Military
     - Police
     - FIB
     - Michael
     - Trevor
     - Franklin
   - no unique character appears twice.
   - user can create more squads and modify existing default squads.
   - default squads should be resettable, not permanently read-only.

5. Shoot On Citizen
   - if player shoots/targets a civilian ped or civilian vehicle, bodyguards attack the same target.
   - must avoid friendly fire against player/bodyguards/friendly peds.

6. Wanted Level
   - menu to read, set, clear, force max, and optionally lock wanted level.

7. Full French Menu
   - menu UI should be translated through existing language system.
   - do not translate proper names, vehicle names, ped model names, character names, or weapon names unless they are already localized by the game.

## Verified Local Facts

- `AddOptionBodyGuardPed` is called from generic ped model changer flow. Do not add a role parameter to it. Use a global default spawn role instead.
- Police ped model must be `s_m_y_cop_01`, not `s_m_y_cop`.
- Useful default ped models exist in `PedList.xml`:
  - `player_zero` = Michael
  - `player_one` = Franklin
  - `player_two` = Trevor
  - `s_m_y_cop_01` = Cop Male
  - `s_m_y_swat_01` = SWAT
  - `s_m_m_fibsec_01` = FIB Security
  - `s_m_m_fiboffice_01` = FIB Office Worker
  - `s_m_y_marine_01`, `s_m_m_marine_01` = Marines
- `ThreadMenuLoops2` in `Routine.cpp` is the right place for an always-on bodyguard tick.
- `neverWanted` and `selfFreezeWantedLevel` already exist in `Routine.cpp`/`Routine.h`; wanted UI should document interaction with existing Player Options.
- `SET_FAKE_WANTED_LEVEL` is exposed under namespace `MISC`.
- `GET_PLAYER_TARGET_ENTITY` exists locally and should be used as a lock-on fallback alongside `GET_ENTITY_PLAYER_IS_FREE_AIMING_AT`.
- `DrivingStyle::nameArray` already exists in `Scripting/enums.cpp` and is used by vehicle/task menus.
- `BlipIcon::Health` exists and can represent Medic.
- `GTAmodel::Model::IsVehicle()` exists and should validate user-entered escort vehicle models.
- Existing XML files under `menyooStuff` commonly use pugixml. Use XML for squads.
- Existing `French.json` is used by `Language::TranslateToSelected`; menu primitives already call translation.
- Toasts/messages built with `Game::Print` are not automatically translated unless code explicitly routes/static-keys them.

## Important Corrections To Previous Claude Plan

Do not repeat these mistakes:

- Do not use `s_m_y_cop`; use `s_m_y_cop_01`.
- Do not make default squads read-only if the user wants to edit them. Use a reset option instead.
- Do not store all settings only in `Squads.xml`. Persist simple bodyguard options in `menyooConfig.ini`.
- Do not persist runtime handles.
- Do not limit `Escort Vehicle Model` to a short hardcoded list. Provide quick choices plus custom model input.
- Do not assume "full French" covers dynamic game/model/weapon names.
- Do not compare translated labels for logic. Use technical role keys for XML and enum mapping; display labels can be translated.

## Recommended Data Model

Add a stable role enum:

```cpp
enum class BodyguardRole {
    Rifleman = 0,
    Shotgun = 1,
    Sniper = 2,
    Heavy = 3,
    Medic = 4,
    Driver = 5,
    Count
};
```

Add a role definition table with `static_assert`:

```cpp
static_assert(sizeof(kRoleDefs) / sizeof(kRoleDefs[0]) == (size_t)BodyguardRole::Count,
              "kRoleDefs desynced from BodyguardRole enum");
```

Add bodyguard fields:

- `BodyguardRole Role`
- `Vehicle EscortVehicle`
- `int EscortSeat`
- enough runtime state to restore driver group behavior after escort tasks.

Persist custom squads in `menyooStuff\Squads.xml`.

Persist simple bodyguard settings in `menyooConfig.ini`, likely under a new `[bodyguards]` section:

- default spawn role
- escort toggles
- escort model
- escort driving style
- combat response enabled/mode
- role blip settings if made configurable

## Native/API Pointers

Useful local natives verified in `Solution/source/Natives/natives.h`:

- `TASK_VEHICLE_ESCORT`
- `TASK_VEHICLE_FOLLOW`
- `IS_VEHICLE_SEAT_FREE`
- `GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS`
- `SET_PED_INTO_VEHICLE`
- `CREATE_VEHICLE`
- `SET_PED_KEEP_TASK`
- `SET_PED_NEVER_LEAVES_GROUP`
- `GET_ENTITY_PLAYER_IS_FREE_AIMING_AT`
- `GET_PLAYER_TARGET_ENTITY`
- `TASK_COMBAT_PED`
- `TASK_SHOOT_AT_ENTITY`
- `HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY`
- `CLEAR_ENTITY_LAST_DAMAGE_ENTITY`
- `SET_PLAYER_WANTED_LEVEL`
- `SET_PLAYER_WANTED_LEVEL_NOW`
- `CLEAR_PLAYER_WANTED_LEVEL`
- `SET_MAX_WANTED_LEVEL`
- `MISC::SET_FAKE_WANTED_LEVEL`
- `SET_BLIP_SPRITE`
- `SET_BLIP_COLOUR`

## Required Dynamic Workflow Constraints

When using Claude Code Dynamic Workflow:

- Use `ultracode`.

- Require a read-only inspection phase before edits.
- Require a synthesis phase.
- Require an adversarial review/critique phase before implementation.
- Require implementation after the plan is approved by the workflow controller.
- Require final build/verification phase.

## Minimum Agent Split

Use up to these agents:

1. Architecture/bodyguard data model inspector.
2. Vehicle escort/seating/task inspector.
3. Squads XML/persistence/UI inspector.
4. Combat response/wanted inspector.
5. French translation/build inspector.
6. Synthesis architect.
7. Adversarial reviewer.
8. Implementation agent for role/blip/tick.
9. Implementation agent for escort/combat/wanted.
10. Implementation agent for squads/config/i18n.
11. Build/fix agent.
12. Final reviewer.


## Expected Verification

- Run Premake after adding new files.
- Run Release x64 build if toolchain is available.
- Validate `French.json` with a JSON parser.
- Report any build blockers explicitly.
- Provide smoke-test checklist for in-game validation.
