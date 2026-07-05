# Bodyguards

MenyooSP bodyguard system — reference for the v2 feature set.

All code lives in namespace `sub::BodyguardMenu` (helpers under `sub::BodyguardMenu::BodyguardManagement`). Source: `Solution/source/Submenus/Bodyguards/`.

---

## 1. Overview

A bodyguard is a friendly ped spawned into the player's group, tracked in two parallel lists that are always kept in sync:

- `std::vector<BodyguardEntity> BodyguardDb` — full record (handle, name, model hash, role, escort state).
- `std::vector<Ped> s_bodyguards` — raw handles. `static constexpr size_t MAX_BODYGUARDS = 7`.

Both lists are pushed and erased together; never mutate one without the other. The single spawn path that guarantees this is `BodyguardManagement::SpawnBodyguardPed(model, name, role, deferConvoyAssign=false)`. The classic per-model menu button `AddOptionBodyGuardPed(text, model)` simply forwards to it with `g_defaultSpawnRole`.

`BodyguardEntity` (`BodyguardManagement.h`) keeps its original layout (`Type`, `Name`, `HashName`, `Handle`) and appends v2 runtime fields: `Role`, `EscortVehicle`, `EscortSeat`, `WasNeverLeavesGroup`, `RemovedFromGroup`, driver task cache, reboard retry state and combat task cache. `operator==` compares the handle only. Runtime handles (`EscortVehicle`, `LastCombatTarget`, the ped handle, blip handles) are never persisted.

**What is new in v2**
- A 6-role system (weapon / accuracy / shoot rate / combat tuning / firing pattern / role blip per role).
- Editable, resettable named squads persisted to `Squads.xml`.
- Escort-vehicle assignment (player seats first, optional spawned escort, escort/follow driving).
- Combat response that sics the squad on the player's target ("shoot on citizen") or on whoever attacked the player ("retaliate"), with friendly-fire exclusions.
- A bottom-right Bodyguard HUD with headshot, current weapon, health and armor for each active guard.
- Chauffeur mode: a free guard, Driver preferred, drives the player to a waypoint or cruises freely.
- Functional Medic healing/revive support plus tactical Attack / Cease Fire / Hold Position orders.
- Optional contextual voice lines, persisted spawn defaults, formation spacing, escort vehicle blips and helicopter escort.
- A bodyguard-specific wanted-level page (opened from Player Options) that reuses the existing freeze enforcement.
- An always-on tick (`TickBodyguards`) hosted in `ThreadMenuLoops2`.
- Simple settings persisted to `menyooConfig.ini [bodyguards]`; squad definitions to `menyooStuff/Squads.xml`.

---

## 2. Role system

Defined header-only in `BodyguardRole.h` (the `kRoleDefs` and `kRoleDisplayLabels` tables are `inline`, so there is a single shared definition across translation units). The **technical key** is English, used for the enum, XML round-trip and all logic. The **display label** (`kRoleDisplayLabels`) is shown in menu texters and is translated via `French.json` — it is never used in comparisons.

`enum class BodyguardRole : int { Rifleman=0, Shotgun=1, Sniper=2, Heavy=3, Medic=4, Driver=5, Count }`.

| Role | Key | Weapon | Accuracy | Shoot rate | Combat ability | Combat range | Firing pattern | Blip sprite | Blip colour |
|------|-----|--------|---------:|-----------:|---------------:|-------------:|----------------|------------:|------------:|
| Rifleman | `Rifleman` | `WEAPON_CARBINERIFLE` | 60 | 700 | 2 | 1 (medium) | FullAuto | 1 (Standard) | 3 (blue) |
| Shotgun | `Shotgun` | `WEAPON_PUMPSHOTGUN` | 45 | 350 | 2 | 0 (near) | SingleShot | 1 (Standard) | 3 (blue) |
| Sniper | `Sniper` | `WEAPON_SNIPERRIFLE` | 95 | 220 | 1 | 2 (far) | SingleShot | 1 (Standard) | 29 |
| Heavy | `Heavy` | `WEAPON_RPG` | 50 | 280 | 2 | 2 (far) | SingleShot | 1 (Standard) | 49 |
| Medic | `Medic` | `WEAPON_PISTOL` | 50 | 300 | 1 | 1 (medium) | SingleShot | 153 (Health) | 2 (green) |
| Driver | `Driver` | `WEAPON_MICROSMG` | 40 | 550 | 1 | 1 (medium) | FullAuto | 225 (PersonalVehicleCar) | 5 (yellow) |

Combat ability: 0 poor, 1 average, 2 professional. Combat range: 0 near, 1 medium, 2 far.

- `RoleKey(role)` / `RoleFromKey(string)` convert key <-> enum (unknown key falls back to `Rifleman`). A `static_assert` keeps `kRoleDefs` in lockstep with the enum.
- `ApplyRoleToBodyguard(ped, role)` applies weapon, accuracy, shoot rate, perception, combat ability/range/movement, drive-by-capable combat attributes and firing pattern. Driver also gets stronger driver ability/aggression.
- `ApplyBodyguardBlipForRole(ped, role)` uses the role sprite/colour when `g_roleBlipsEnabled`; otherwise it falls back to the legacy `blipIcon` blip (Standard / Friend / VIP). The legacy "Bodyguard Blip" texter is only rendered in Settings while "Role Blips" is OFF (it has no effect otherwise).
- The default role for new spawns is `g_defaultSpawnRole` (Bodyguards menu → Settings → "Default Spawn Role"). Per-bodyguard role can be changed from the Bodyguard entity submenu, which re-applies tuning and the blip.
- On spawn, `ApplyRoleToBodyguard` runs first, then `ApplyAutoArmOnSpawn`. Auto-arm (when enabled) intentionally overrides the role weapon — hence the menu label "Auto-Arm New Bodyguards (overrides role weapon)".
- The Bodyguards menu entry remains "Settings", but the submenu title is "Bodyguard Settings" so it is distinct from the global Menyoo settings page.
- Medic-role guards can heal wounded guards, revive dead-but-still-existing guard bodies, and optionally heal the player. While a medic is in a heal attempt, it is treated as busy and excluded from escort/combat retasking.

### Weapon configuration — single source per audience

All weapon options live in exactly two places, with no overlap:

- **Future spawns** — Settings → "Spawn Defaults": "Auto-Arm New Bodyguards (overrides role weapon)" (`g_autoArmNewBodyguards`) and "Spawn Weapon" (`g_spawnWeaponIndex`). Rendered by `AddSpawnArmingOptions()` (`BodyguardMenu.cpp`, which owns the weapon tables) but displayed inside the Settings submenu.
- **Already-spawned guards** — "Active Bodyguards" → "--- Weapons ---": pick a weapon and "Arm All Bodyguards" (immediate action).

"Apply Preset" (see below) never enables auto-arm and never changes the spawn weapon.

### Bodyguard list and entity submenu

`BODYGUARD_LIST` shows the current count in the title (`Bodyguard List (x/7)`) using existing tracked handles and `MAX_BODYGUARDS`. Each row includes name/model, role, health/max health and a `[DEAD]` suffix for dead or dying peds. Selecting a guard opens `BODYGUARD_ENTITYOPS`, whose title uses the guard's friendly name when available.

The entity submenu includes per-guard Role, Wardrobe, Voice Changer, Weapons and Loadouts, plus individual actions:

- **Heal Bodyguard** restores max health, current health and armor from the bodyguard defaults.
- **Bring Bodyguard To Self** teleports only that guard in front of the player. If the guard has an escort assignment and Reboard After Combat is enabled, the escort tick may ask them to re-enter their assigned vehicle afterwards; the action does not clear escort runtime state.
- **Hold Position / Follow Me** toggles a non-escort guard between a guard-stand scenario outside the vanilla group and normal group following. This is runtime-only and never persisted.
- **Delete Bodyguard** calls `BodyguardManagement::DeleteBodyguard`, preserving the `BodyguardDb` + `s_bodyguards` invariant and returning to the list.

### Tactical commands

The main Bodyguards menu Actions section adds:

- **Attack My Target** resolves the player's aimed/locked ped or first vehicle occupant and tasks the squad even when automatic Combat Response is Off. Friendly targets are rejected by the same handle/relationship checks as automatic combat.
- **Cease Fire** clears current combat tasks and suppresses automatic retasking for about 5 seconds.
- **Hold Positions (All) / Follow Me (All)** applies the per-guard hold/follow toggle to every alive guard. Guards assigned to escort vehicles cannot be placed on hold.

### Active Bodyguards submenu (formerly "Squad Tools")

Menu: `BODYGUARD_SQUAD_TOOLS` (historical enum id kept — never reorder). Listed in the main Bodyguards menu directly under "Bodyguard List" because it operates on the *currently spawned* guards, unlike "Squads" which edits squad *templates*. Sections:

- **Weapons** — Weapon texter + "Arm All Bodyguards".
- **Health** — Heal All / Refill Armor / Revive Dead. Heal All and Revive Dead also recover guards in the GTA "dead or dying" state (downed but not fully dead). Revive Dead reports bodies that no longer exist in the world as "bodies missing" because the engine has recycled/despawned them and they cannot be resurrected.
- **Stat Presets** (formerly "Equipment Presets"; renamed to stop colliding with the Police/Military/FIB *squad* names) — Police / Military / Gang / FIB / Heavy presets of health/armor/godmode/accuracy (+ a weapon handed to the current squad). "Apply To Current Squad" applies stats+weapon to alive guards; "Use For New Bodyguards" copies **stats only** (health/armor/godmode) into the spawn defaults — it deliberately does not touch auto-arm or the spawn weapon.
- **Cleanup** — Cleanup Dead / Dismiss All.

---

## 3. Squads

Source: `BodyguardSquads.cpp` / `BodyguardSquads.h`. Menus: `BODYGUARD_SQUADS` (list), `BODYGUARD_SQUAD_EDIT` (per-squad editor) and `BODYGUARD_SQUAD_MEMBER_EDIT` (per-member role/count/remove).

`SquadDef { std::string name; bool isDefault; std::vector<SquadMember> members; }`
`SquadMember { std::string model; BodyguardRole role; int count; }`

### Default squads

The single canonical table `kDefaultSquads` drives default load, per-squad reset and reset-all. Each unique character model appears once. Police uses `s_m_y_cop_01`.

| Squad | Members |
|-------|---------|
| Military | `s_m_y_marine_01` x2 Rifleman; `s_m_m_marine_01` x1 Heavy |
| Police | `s_m_y_cop_01` x2 Rifleman; `s_m_y_swat_01` x1 Heavy |
| FIB | `s_m_m_fibsec_01` x2 Rifleman; `s_m_m_fiboffice_01` x1 Medic |
| Black Ops | `s_m_y_blackops_01` x1 Driver; `s_m_y_blackops_02` x2 Rifleman; `s_m_y_blackops_03` x1 Sniper |
| Michael | `player_zero` x1 Rifleman |
| Trevor | `player_two` x1 Heavy |
| Franklin | `player_one` x1 Sniper |

Default squads are **editable and resettable**, not read-only. Squad names (proper nouns) and model names are never translated.

### Editing / creating / resetting

- **Spawn:** open a squad → "Spawn Squad". `SpawnSquad` iterates members, repeating each `count` times, calling `SpawnBodyguardPed` (so both tracking lists stay synced). Spawned guards are named from the squad plus a unique suffix (`Police #1`, `Police #2`, ...), continuing after the largest existing suffix for that squad. It stops at `MAX_BODYGUARDS`.
- **Edit a member:** change Role (texter) or Count (number, 1..`MAX_BODYGUARDS`). Each change saves to `Squads.xml` immediately. "Save Squads" also flushes explicitly.
- **Add a member:** "Add Member..." → free-text ped model. The model is accepted only if `GTAmodel::Model(model).IsInCdImage()`; it is added with role `Rifleman` and count 1, then saved. (Model names are technical and never translated.)
- **Edit/remove a member:** the squad editor shows one line per member (`model (Role xCount)`). Opening a member gives Role / Count / Remove Member. Every change saves immediately.
- **New squad:** "New Squad..." → free-text name. Name must be non-empty and pass `IsSafePath`; it is created empty with `isDefault=false`, then saved. Use "Add Member..." to populate it.
- **Reset one:** "Reset This Squad" restores members from the matching default by squad name and saves. This avoids wrong resets if custom squads or new defaults change the list order.
- **Reset all:** "Reset All Squads To Default" reloads the canonical defaults and saves.

`g_squads` is loaded at startup by `ReadBodyguardConfig` (so a squad can be spawned before the Squads menu is ever opened) and, as a fallback, lazily by `BodyguardSquadsMenu` if the list is still empty. On missing/parse-failed/empty XML, defaults are loaded and written out.

---

## 4. Escort vehicle

Source: `BodyguardEscort.cpp` / `BodyguardEscort.h`. Menu: `BODYGUARD_ESCORT`.

The first line of the Escort Vehicle menu is a non-clickable status break: `Assigned: x/y - model`, translated by prefix while leaving the vehicle model technical/raw.

### Settings (persisted)

| Setting (menu label) | Global | Meaning |
|----------------------|--------|---------|
| Use My Seats First | `g_escortUseMySeatsFirst` (default true) | Fill the player's current vehicle's free passenger seats before anything else. |
| Spawn Escort Vehicle If Full | `g_escortSpawnIfFull` (default false) | If bodyguards remain unseated, spawn a dedicated escort vehicle for them. |
| Auto-Assign New Spawns | `g_escortAutoAssignNewSpawns` (default false) | When a convoy already exists, newly spawned bodyguards are assigned to the next free escort seat. |
| Reboard After Combat | `g_escortReboardAfterCombat` (default true) | Assigned guards pulled out of a vehicle are asked to re-enter, then warped back after repeated failed attempts. |
| Escort Vehicle Godmode | `g_escortVehicleGodmode` (default false) | Applies invincibility/proofs to escort vehicles spawned by this module. |
| Catch-Up Teleport | `g_escortCatchupTeleport` (default true) | Teleports distant convoy vehicles and off-screen foot guards back near the player as a safety net. |
| Realistic Boarding | `g_escortRealisticBoarding` (default false) | Nearby guards walk to their assigned seats first; the existing reboard/warp fallback still recovers failures. |
| Escort Vehicle Model | `g_escortVehicleModel` (default `"police"`) | Model for the spawned escort vehicle. |
| Escort Helicopter Model | `g_escortHeliModel` (default `"buzzard2"`) | Model for the spawned escort helicopter command. |
| Escort Driving Style | `g_escortDrivingStyleIndex` (default 0) | Index into `DrivingStyle::nameArray`. |

Quick-pick models in the texter: `police`, `sultan`, `riot`, `insurgent`. A custom value not in that list is shown as an extra selected entry.

The menu also exposes **Spawn Escort Helicopter**. It spawns the configured helicopter behind/above the player, seats a Driver-role guard first when possible, seats free passengers, tracks the helicopter in the same spawned escort list as ground vehicles, and adds a friendly vehicle blip. Clear Escort Assignments and empty-vehicle cleanup apply to the helicopter too.

### Seat-fill order ("Assign Bodyguards To Escort")

1. Gather alive bodyguards not already assigned and seated. If an unassigned guard is already physically sitting in the player's vehicle, adopt that seat instead of teleporting them elsewhere.
2. During escort, remove assigned guards from the vanilla player group (`REMOVE_PED_FROM_GROUP`) and restore them on clear/delete. This prevents group AI from ejecting passengers from non-player vehicles.
3. If "Use My Seats First" is on and the player is in a vehicle, fill that vehicle's free passenger seats (`seat < maxPassengers`).
4. If a spawned escort vehicle is expected or already exists, reserve **Driver-role** guards until after passenger-first seating, so Driver guards remain available for escort vehicle driver seats.
5. Reuse free seats in escort vehicles previously spawned by this module, so multiple spawned squads join the same convoy until it is full.
6. Only if bodyguards remain and "Spawn Escort Vehicle If Full" is on, spawn escort vehicles in a bounded cascade (up to four per assignment). The first appears ahead of the player and later vehicles are offset behind the player so they do not stack. Each new vehicle is mission entity, unlocked doors, engine on. A **Driver-role** bodyguard is preferred for the driver seat (-1); failures continue to the next candidate/bodyguard rather than aborting the whole fill.
7. If a spawned vehicle cannot seat anyone (for example an exotic model without usable seats), that exact vehicle is deleted and the cascade stops instead of looping.
8. "Clear Escort Assignments" asks guards to leave vehicles first, restores group behaviour, and deletes module-spawned escort vehicles as soon as they are physically empty (this explicit button bypasses the 12-second grace, which only applies to the automatic tick purge). Player/personal vehicles are never deleted by this path.

### Custom model validation

`Custom Model...` opens a free-text input. The input is accepted only if `GTAmodel::Model(input).IsInCdImage() && IsVehicle()`; otherwise a red "Invalid vehicle model" toast is shown. (`IsSafePath` is not used on model names.)

### Driving

`TickEscort` (every ~750ms) maintains convoy state:
- It purges/deletes empty module-spawned escort vehicles only after they are physically empty, not assigned to any alive guard, and have stayed empty for a 12-second grace period. This prevents combat/reboard gaps from deleting the convoy vehicle. The explicit "Clear Escort Assignments" button skips that grace for the vehicles it empties.
- Wrecked or no-longer-driveable module-spawned escort vehicles release their assigned guards back to vanilla group behaviour; once no alive guard remains assigned, the wreck can be deleted even if dead occupants still exist.
- If an assigned escort vehicle disappears or de-streams while a guard still has escort state, the guard's group membership is restored instead of leaving them orphaned outside the vanilla player group.
- If the player changes personal vehicle, guards assigned to the old non-escort vehicle are released and asked to leave. When Auto-Assign New Spawns is enabled, the new vehicle is re-assigned after a short 2-second stability delay.
- It promotes a passenger (Driver role preferred) if a spawned escort vehicle loses its driver.
- If Catch-Up Teleport is enabled, a spawned convoy vehicle more than about 150 m away and off-screen is moved near a road node behind the player, no more than once every 5 seconds per vehicle; driver task caches are invalidated so the convoy resumes immediately.
- Driver tasks are cached per guard and re-issued only when the followed target state changes, avoiding periodic stop-and-go.
- Player in a vehicle -> `TASK_VEHICLE_ESCORT` toward the player's vehicle.
- Player on foot -> `TASK_VEHICLE_FOLLOW` the player ped.
- Spawned escort helicopters use `TASK_HELI_MISSION`: follow while the player is moving or in a vehicle, and a courtesy landing mission when the player has been on foot long enough.
- Assigned guards who end up on foot after combat are re-tasked to re-enter, with a warp fallback after repeated failures.
- If Realistic Boarding is enabled and the guard is within about 25 m of the target vehicle, initial assignment uses `TASK_ENTER_VEHICLE` instead of immediate warp; the normal reboard fallback keeps the assignment reliable if the walk-in fails.
- Passenger guards sitting in the player's own abandoned vehicle are asked to leave and rejoin the group after the player has been on foot for about 8 seconds and is more than 15 m away. This does not apply to module-spawned escort vehicles, which keep following as a convoy.
- Non-escort guards on foot that fall more than about 150 m behind and are off-screen are teleported near the player when Catch-Up Teleport is enabled, unless they are busy fighting.

The TASK receives the actual driving style value `DrivingStyle::nameArray[index].style`, not the index. `ClearEscortState` restores group membership and `WasNeverLeavesGroup`; deletion is restricted to vehicles tracked in the module's spawned escort vehicle list.

### Chauffeur mode

Menu: `BODYGUARD_CHAUFFEUR`, reachable from the main Bodyguards menu as "Chauffeur".

- **Drive Me To Waypoint** chooses an alive free guard, preferring Driver role, and drives the player to the current waypoint. If no waypoint exists, it falls back to free driving.
- **Drive Me Around** starts free driving without a destination.
- **Stop Chauffeur** brakes/stops the mode, restores the guard to the vanilla group, clears blocking events and only schedules deletion for a vehicle spawned by Chauffeur mode when it is physically empty. The player's/current vehicle is never deleted.
- The mode can reuse the player's current vehicle or spawn `g_chauffeurVehicleModel`; an empty model means "Same As Escort". Helicopter models use `TASK_HELI_MISSION` and attempt a landing near the waypoint (issued once, not re-sent every tick).
- **Warp Into Vehicle** (`g_chauffeurWarpPlayer`, default true) teleports the player into a free passenger seat when the ride starts; when OFF the player is asked to walk in via `TASK_ENTER_VEHICLE`.
- A safety net re-issues the driving task (checked via `GET_SCRIPT_TASK_STATUS`) if something external cleared it while the ride is still active.
- Chauffeur guards are treated as busy: escort assignment, escort tick and combat tasking skip them.

---

## 5. Combat response (shoot on citizen)

Source: `BodyguardCombat.cpp`. Setting "Combat Response" in Bodyguards -> Settings -> Behaviour -> `g_combatResponseMode` (default 4 / Full Protection).

| Mode | Label | Behaviour |
|------|-------|-----------|
| 0 | Off | No combat tasking. |
| 1 | Player Target | Resolve the player's aimed/locked entity and task the whole squad to attack it (unless friendly). Re-tasks when the target changes or after a short refresh. |
| 2 | Retaliate | Every ~500 ms, scans nearby peds (`World::GetNearbyPeds`, 60 m radius) for one that has just damaged the player (`HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY`), tasks the whole squad on it, then clears the player's last-damage entity. Friendly peds are excluded. |
| 3 | Wanted Threats | While the player's wanted level is at least `g_wantedThreatMinStars`, scans nearby peds for law-response or actively fighting threats and tasks bodyguards on the best nearby target. |
| 4 | Full Protection | Runs Player Target, Retaliate and Wanted Threats together. This is the default for new configs. |

Mode 1 reads the target via `GET_ENTITY_PLAYER_IS_FREE_AIMING_AT`, falling back to `GET_PLAYER_TARGET_ENTITY`, and is throttled to ~200 ms to avoid doing aim/occupant native work every frame. A vehicle target resolves to its first occupant (driver, then passengers). Mode 2 is self-contained (it uses the world ped pool directly, so it does not depend on the `TickSubsystems` globals) and is throttled to ~500 ms to keep the scan cheap. Modes 2 and 3 cap nearby-ped scans at 128 handles instead of using the large default allocation path. Mode 3 is gated by "Wanted Response Min Stars" (`g_wantedThreatMinStars`, default 2), so one-star wanted incidents do not make the squad hunt law-response peds unless the user lowers the setting to 1.

In Full Protection, the three scanners propose targets into one per-frame arbitration point instead of all tasking the squad independently. Priority is Player Target first, then Retaliate, then Wanted Threats. This prevents ping-pong re-tasks when multiple protection sources fire close together.

**Friendly-fire exclusions** (`IsFriendlyToPlayer`, handle/enum comparisons only — never labels): the player ped, any player ped, any member of the player's group, any tracked bodyguard (`GetBodyguardIndexInDb`), and any ped whose relationship to the player is Companion (0), Respect (1) or Like (2). For a vehicle, if **any** occupant is friendly the whole vehicle is rejected.

Tasking uses `TASK_COMBAT_PED(bgPed, target, 0, 16)` + `SET_PED_KEEP_TASK` on foot. Bodyguards already seated in a vehicle also receive vehicle-fire tasks (`TASK_VEHICLE_SHOOT_AT_PED`, `TASK_DRIVE_BY`, and `SET_VEHICLE_SHOOT_AT_TARGET`) so gunners/passengers have a chance to use mounted or drive-by weapons. A guard with an escort assignment is not given a walking combat task, so combat should not pull them out of the convoy. Each guard remembers its last runtime combat target and skips re-tasking when it is already fighting that same target, reducing visible combat stutter. When the last squad target is gone, `TickCombatResponse` releases `SET_PED_KEEP_TASK` for non-escort guards and clears their combat task softly so vanilla group following resumes; escort guards keep convoy ownership and only reset their driver task cache. Some vehicle weapons, especially tank cannons, remain limited by GTA V AI/native behaviour.

Manual Attack My Target uses the same target resolution and friendly-fire exclusions. Cease Fire clears current combat task state and suppresses automatic retasking briefly. Busy guards (chauffeur or medic-in-care) are excluded from combat tasking.

---

## 6. Wanted level

Source: `BodyguardWanted.cpp`. Menu: `BODYGUARD_WANTED`. The submenu is exposed from Player Options as "Wanted Options"; the historical enum and implementation stay under Bodyguards for stability.

UI items:
- **Wanted Level** — display only (`GET_PLAYER_WANTED_LEVEL`).
- **Set Wanted Level** (0..5) — applies on input via `SET_PLAYER_WANTED_LEVEL` + `_NOW`.
- **Clear Wanted Level** — `CLEAR_PLAYER_WANTED_LEVEL`.
- **Force Max Wanted** — sets level 5 immediately.
- **Fake Wanted Level** (0..5) — `SET_FAKE_WANTED_LEVEL`; visual stars only, no police response.
- **Lock Level** (0..5) — bound to `g_wantedLockLevel`.
- **Lock Wanted Level** (toggle) — `g_wantedLockEnabled`.

### Interaction with existing freeze logic

The lock reuses the **existing** `selfFreezeWantedLevel` (declared in `Routine.h`, enforced per frame at `Routine.cpp:3432`) rather than adding a second writer:
- Lock ON → `selfFreezeWantedLevel = g_wantedLockLevel`. While locked, changing Lock Level keeps the freeze in sync.
- Lock OFF → `selfFreezeWantedLevel = 0`.
- On startup, `ReadBodyguardConfig` re-arms the lock: if `wanted_lock_enabled` was saved ON, it sets `selfFreezeWantedLevel = wanted_lock_level` immediately, so a persisted lock takes effect without the user re-toggling it.

`neverWanted` (Player Options, enforced at `Routine.cpp:3380`) overrides everything to 0. While Lock is on, Set/Clear are effectively ignored because the freeze re-applies every frame.

---

## 7. Persistence

Runtime handles and runtime task flags (Ped / Vehicle / Blip ints, including `EscortVehicle`, `EscortSeat`, `DriverTasked`, `CombatTasked`) are **never persisted**.

### menyooConfig.ini `[bodyguards]`

Read/written by `ReadBodyguardConfig` / `SaveBodyguardConfig` (`BodyguardConfig.cpp`), invoked from `MenuConfig.cpp` `ConfigRead()` / `SaveConfig()`.

| Key | Type | Global |
|-----|------|--------|
| `default_spawn_role` | string | `g_defaultSpawnRole` (round-trips via `RoleKey` / `RoleFromKey`) |
| `role_blips_enabled` | bool | `g_roleBlipsEnabled` |
| `hud_enabled` | bool | `g_hudEnabled` |
| `escort_use_my_seats_first` | bool | `g_escortUseMySeatsFirst` |
| `escort_spawn_if_full` | bool | `g_escortSpawnIfFull` |
| `escort_auto_assign` | bool | `g_escortAutoAssignNewSpawns` |
| `escort_reboard_after_combat` | bool | `g_escortReboardAfterCombat` |
| `escort_vehicle_godmode` | bool | `g_escortVehicleGodmode` |
| `escort_catchup_teleport` | bool | `g_escortCatchupTeleport` |
| `escort_realistic_boarding` | bool | `g_escortRealisticBoarding` |
| `escort_vehicle_model` | string | `g_escortVehicleModel` |
| `escort_heli_model` | string | `g_escortHeliModel` |
| `escort_driving_style_index` | long | `g_escortDrivingStyleIndex` |
| `chauffeur_vehicle_model` | string | `g_chauffeurVehicleModel` |
| `chauffeur_warp_player` | bool | `g_chauffeurWarpPlayer` |
| `combat_response_mode` | long | `g_combatResponseMode` |
| `combat_wanted_min_stars` | long | `g_wantedThreatMinStars` (1..5, default 2) |
| `medic_enabled` | bool | `g_medicEnabled` |
| `medic_heal_player` | bool | `g_medicHealPlayer` |
| `voice_lines_enabled` | bool | `g_voiceLinesEnabled` |
| `default_health` | long | `health` |
| `default_armor` | long | `armor` |
| `default_godmode` | bool | `godmode` |
| `auto_arm_new` | bool | `g_autoArmNewBodyguards` |
| `spawn_weapon_index` | long | `g_spawnWeaponIndex` (0..9) |
| `arm_weapon_index` | long | `g_armWeaponIndex` (0..8) |
| `wanted_lock_enabled` | bool | `g_wantedLockEnabled` |
| `wanted_lock_level` | long | `g_wantedLockLevel` |
| `formation_index` | long | `g_formationIndex` (0 Default, 1 Circle Inward, 2 Circle North, 3 Line; clamped to 0 if out of range) |
| `formation_spacing` | long | `g_formationSpacing` (0 vanilla, 1..10 m) |

`ReadBodyguardConfig` must not call natives. A persisted non-default `formation_index` or non-zero `formation_spacing` therefore only sets `g_formationApplyPending`; the actual group native calls are deferred to the first `TickBodyguards()` (same deferral pattern as the wanted-lock re-arm, which can act immediately because `selfFreezeWantedLevel` is enforced per frame elsewhere).

### menyooStuff/Squads.xml

Squad **definitions** only (models, roles via technical key, counts) — no handles. Path: `GetPathffA(Pathff::Main, true) + "Squads.xml"`. Encoding `ISO-8859-1` via pugixml.

```xml
<?xml version="1.0" encoding="ISO-8859-1"?>
<Squads>
  <Squad name="Military" isDefault="true">
    <Member model="s_m_y_marine_01" role="Rifleman" count="2" />
    <Member model="s_m_m_marine_01" role="Heavy"    count="1" />
  </Squad>
  <!-- ... -->
</Squads>
```

The `role` attribute is the technical key. Missing/empty XML triggers a defaults write.

---

## 8. The always-on tick

`TickBodyguards()` (`BodyguardTick.cpp`) is called every frame from `ThreadMenuLoops2()` in `Routine.cpp` (the `for(;;) { WAIT(0); ... }` loop, line ~3972; include at `Routine.cpp:78`). It:

- First, applies a pending persisted group formation (`g_formationApplyPending` → `SET_GROUP_FORMATION`) — done *before* the empty-DB early-out so it is armed even if the first bodyguard is spawned later.
- Every frame: `TickBodyguardHud()` draws the HUD and maintains/releases headshot slots as needed.
- ~Every 500ms: `TickChauffeur()` maintains chauffeur driving and spawned-vehicle cleanup.
- Early-outs when `BodyguardDb` is empty.
- ~Every 400ms: `UpdateBodyguardBlipsOnDeath()` (death sprite 274; alive restores the role/legacy blip).
- Every call: `TickCombatResponse()` (also releases finished combat tasks, including after Combat Response is switched Off).
- ~Every 1000ms: Medic heal/revive state machine.
- ~Every 2000ms: hold-position scenario re-application for guards on hold and out of combat.
- ~Every 750ms: `TickEscort()` (driver tasking, disappeared/wrecked-vehicle cleanup, 12-second empty escort-vehicle grace, vehicle-change reassignment, convoy catch-up and abandoned personal-vehicle passenger release), then `TickEscortCatchupOnFoot()` for distant off-screen guards not assigned to escort.

Throttling uses `MISC::GET_GAME_TIMER()`.

---

## 9. File map

**New** (`Solution/source/Submenus/Bodyguards/`): `BodyguardRole.h`, `BodyguardTick.{h,cpp}`, `BodyguardEscort.{h,cpp}`, `BodyguardCombat.{h,cpp}`, `BodyguardWanted.{h,cpp}`, `BodyguardSquads.{h,cpp}`, `BodyguardConfig.{h,cpp}`, `BodyguardHud.{h,cpp}`, `BodyguardChauffeur.{h,cpp}`, `BodyguardDebug.h`.

**Modified:**
- Bodyguards: `BodyguardManagement.{h,cpp}`, `BodyguardSpawn.{h,cpp}`, `BodyguardMenu.{h,cpp}`, `BodyguardSettings.cpp`, `BodyguardSubmenu.cpp`.
- Menu: `submenu_enum.h` (ids `BODYGUARD_ESCORT`, `BODYGUARD_WANTED`, `BODYGUARD_SQUADS`, `BODYGUARD_SQUAD_EDIT`, `BODYGUARD_SQUAD_MEMBER_EDIT` after `BODYGUARD_SQUAD_PRESETS`), `Routine.cpp` (tick wiring), `MenuConfig.cpp` (config read/save hooks), `PlayerOptions.cpp` (entry point to Wanted Options).
- `Solution/source/_Build/bin/Release/menyooStuff/Language/French.json` (UI labels only — no proper names, models, vehicles or weapons).

---

## 10. Build

If the generated Visual Studio project already contains the bodyguard `.cpp` files, MSBuild Release x64 is enough. If a fresh checkout is missing any of those files from the project, run `generate.bat` once before MSBuild.

```sh
# 1) regenerate the solution
Solution/external/premake/premake5.exe vs2022

# 2) build Release | x64
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  Solution/Menyoo.sln -t:Build -p:Configuration=Release -p:Platform=x64 -m -nologo
```

Output: `Solution/source/_Build/bin/Release/Menyoo.asi`. Known-harmless warnings: 12x C4805 in `GTAmemory.cpp`.

> Editing only existing `.cpp`/`.h` (no new files) does not require the Premake step — an incremental MSBuild of the solution is enough.

---

## 11. Deployment (install into GTA)

The build only writes to `Solution/source/_Build/bin/Release/`. The game loads the plugin from the **GTA install root**, so the freshly built `Menyoo.asi` must be copied there for changes to appear in-game. On this machine the GTA root is:

```
D:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced
```

Steps:

1. **Close GTA V completely.** An `.asi` is file-locked while the game runs, and ASI plugins are only loaded at game startup — copying over a running game does nothing until a restart.
2. Copy `Solution/source/_Build/bin/Release/Menyoo.asi` → `<GTA root>\Menyoo.asi` (overwrite). Keep a backup of the previous `.asi` (e.g. `Menyoo.asi.bak`) if you may need to revert.
3. For the French UI, copy `Solution/source/_Build/bin/Release/menyooStuff/Language/French.json` → `<GTA root>\menyooStuff\Language\French.json`. (Without it the menu still works, just with English labels.)
4. **Launch GTA V** and open Menyoo → Bodyguards.

`menyooConfig.ini`'s `[bodyguards]` section and `menyooStuff\Squads.xml` are created automatically on first run / first save — they do not need to be copied. The user's standing preference is to perform this copy themselves (the assistant does not touch the GTA install folder unless explicitly authorized).

---

## 12. In-game smoke test

1. **Spawn roles** — set Default Spawn Role, spawn a bodyguard, open it, confirm Role matches and the weapon is the role's weapon.
2. **Role blips distinct** — spawn a Medic and a Driver; with Role Blips ON, confirm the Medic blip is the green health sprite (153) and the Driver blip is the yellow vehicle sprite (225). Toggle Role Blips OFF and confirm blips revert to the legacy Standard/Friend/VIP blip.
3. **Bodyguard list and individual actions** — confirm `Bodyguard List (0/7)`, then spawn guards and confirm the counter changes. Open one guard, use Heal Bodyguard and Bring Bodyguard To Self, then Delete Bodyguard; confirm it returns to the list and the counter decreases.
4. **Squad names unique** — spawn Police twice; the list should show `Police #1`, `Police #2`, `Police #3`, then continue with `Police #4` etc. rather than duplicate names.
5. **Escort fills seats then spawns vehicle** — with "Use My Seats First" ON, enter a car, "Assign Bodyguards To Escort" -> bodyguards take your passenger seats. Add 7 guards, enable "Spawn Escort Vehicle If Full", reassign -> the overflow enters one escort vehicle; nobody loops on doors or exits spontaneously.
6. **Escort status line** — the `Assigned: x/y - model` line is translated, updates after Assign/Clear, and is not selectable/clickable.
7. **Two squads, one convoy** — spawn one squad, assign escort, spawn another squad, assign again. The second squad fills free seats in the existing escort vehicle before a new vehicle is spawned.
8. **Clear safety** — "Clear Escort Assignments" makes guards leave, deletes only empty module-spawned escort vehicles, and never touches the player's personal/current vehicle.
9. **Combat response + no friendly fire** — Combat Response = Player Target, aim at a random pedestrian -> squad attacks. Aim at a bodyguard or a group member -> squad does NOT attack. During escort, passengers use vehicle/drive-by tasks and should not receive foot combat tasks that make them leave the convoy.
10. **Wanted set / clear / lock** — open Player Options -> Wanted Options. Set Wanted Level 3 -> 3 stars. Clear -> 0. Lock Level 2 + Lock ON -> level holds at 2 even after a Set/Clear. Lock OFF -> wanted behaves normally. Confirm Player Options "Never Wanted" forces 0 over the lock.
11. **Squad spawn / edit / create / reset** — Squads -> Police -> Spawn Squad -> 2 cops + 1 swat. Open one member, edit role/count, reopen, confirm it stuck. Create "New Squad...", "Add Member..." (e.g. `s_m_y_swat_01`), Spawn Squad -> confirm it spawns; "Remove Member" -> confirm it's gone. Reset This Squad -> back to defaults. Reset All Squads -> all defaults restored.
12. **Toasts and labels in French** — with French selected, verify spawn, max count, arm/heal/refill/revive, preset, cleanup, dismiss, squad create/reset/member errors, escort assign/clear/model errors and individual Heal/Bring all show French labels/toasts.
13. **Config + Squads.xml round-trip** — change Default Spawn Role, Role Blips, Formation, escort settings including auto-assign/reboard/godmode, combat mode, wanted lock; edit a squad. Restart the game. Confirm the saved Formation is re-applied to the group (spawn a guard and check the walking formation). Confirm `menyooConfig.ini [bodyguards]` values and `menyooStuff/Squads.xml` definitions are restored exactly (and no handles were written).
