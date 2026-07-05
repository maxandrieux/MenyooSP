# Plan 2 — Optimisations en jeu (Bodyguard v2)

> Rédigé le 2026-07-04, basé sur la section « optimisations » de l'audit (P1..P10) et vérifié
> contre le code ACTUEL (post-refactor Mission 0). Toutes les références fichier:ligne
> ci-dessous ont été re-contrôlées sur le code présent dans le dépôt.
>
> Plan frère : `docs/plans/03-bugfix.md` (correction de bugs, rédigé en parallèle).
> Les dépendances croisées sont signalées à chaque lot concerné.

---

## 1. Objectif et périmètre

Rendre l'escorte et le combat des gardes du corps fluides et crédibles en jeu :

1. **Priorité absolue (demande explicite de l'utilisateur)** : quand le véhicule d'escorte
   est plein, en faire apparaître d'autres automatiquement (« cascade ») jusqu'à ce que tous
   les gardes soient assis, sans que les véhicules s'empilent les uns sur les autres.
2. Réagir aux événements du monde : changement de véhicule du joueur, véhicule détruit,
   convoi distancé.
3. Réduire le coût CPU du tick (allocations, natives répétées) et le « bégaiement » de l'IA
   en combat.

**Hors périmètre** : nouvelles fonctionnalités (plan 4), corrections de bugs de la cascade
« gardes autonomes / véhicules qui disparaissent » (plan 3), traduction des toasts (P1-1
audit menu).

Fichiers principaux touchés (tous sous `Solution/source/Submenus/Bodyguards/` sauf mention) :
`BodyguardEscort.cpp/h`, `BodyguardCombat.cpp`, `BodyguardSpawn.cpp/h`, `BodyguardSquads.cpp`,
`BodyguardManagement.h`, `BodyguardConfig.cpp`, et
`Solution/source/_Build/bin/Release/menyooStuff/Language/French.json`.

---

## 2. Découpage en lots livrables indépendamment

| Lot | Contenu | Audit | Taille | Dépend de |
|-----|---------|-------|--------|-----------|
| 1 | Cascade de véhicules d'escorte + placement anti-empilement | P1 + P2 | Moyen | Rien (livrable seul) |
| 2 | Véhicule détruit + changement de véhicule du joueur | P4 + P3 | Moyen | Bugfixes n°1 et n°2 du plan 3 (fortement recommandé avant) |
| 3 | Réserver les gardes « Driver » pour les volants | P5 | Petit | Lot 1 (utilise la cascade) |
| 4 | Pack performance du tick + bornes de sièges | P8 + P10 | Petit/Moyen | Rien |
| 5 | Téléportation de rattrapage (convoi + gardes à pied) | P6 | Moyen | Lot 2 (réutilise ses helpers) |
| 6 | Embarquement réaliste optionnel (sans warp) | P7 | Moyen | Lot 1 |
| 7 | Anti-bégaiement des re-tasks combat | P9 | Moyen | Bugfix n°4 du plan 3 (OBLIGATOIRE avant) |

Ordre de livraison conseillé : **Lot 1 → (plan 3 bugfixes 1+2) → Lot 2 → Lot 3 → Lot 4 →
Lot 5 → Lot 6 → (plan 3 bugfix 4) → Lot 7**. Chaque lot se termine par un build MSBuild
Release x64 (0 erreur / 0 nouvel avertissement) avant de passer au suivant.

---

## 3. LOT 1 — Cascade de véhicules d'escorte + anti-empilement (P1 + P2)

**Fichier unique : `BodyguardEscort.cpp`** (aucun header à toucher, tout est dans le
namespace anonyme du TU).

### Étape 1.1 — Extraire un helper `SpawnOneEscortVehicle` (petit)

- Extraire le corps actuel des lignes ~545-554 (`CREATE_VEHICLE` → `push_back` → `WAIT(0)`)
  de `AssignBodyguardsToEscort()` dans un helper du namespace anonyme, placé près de
  `FillDriverSeat` :

```cpp
// Spawns the Nth escort vehicle of the current assignment. convoyIndex 0 = first
// vehicle (6 m ahead of the player), each next one is placed further back so
// vehicles never stack on the same spot (P2).
Vehicle SpawnOneEscortVehicle(GTAped& player, const GTAmodel::Model& m, int convoyIndex)
{
    // File indienne : 6 m devant, puis 6 - 8*i (derrière le joueur pour i >= 1).
    Vector3 pos = player.GetOffsetInWorldCoords(Vector3(0.f, 6.f - 8.f * convoyIndex, 0.f));
    float heading = player.GetHeading();
    Vehicle escortVeh = VEHICLE::CREATE_VEHICLE(m.hash, pos.x, pos.y, pos.z, heading, true, true, false);
    if (escortVeh == 0 || !ENTITY::DOES_ENTITY_EXIST(escortVeh))
        return 0;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(escortVeh, true, true);
    UnlockVehicleForBodyguards(escortVeh);
    ApplyEscortVehicleGodmode(escortVeh);
    VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(escortVeh, 5.0f);
    VEHICLE::SET_VEHICLE_ENGINE_ON(escortVeh, true, true, false);
    s_spawnedEscortVehicles.push_back(escortVeh);
    WAIT(0);
    return escortVeh;
}
```

- **Placement (P2)** : l'offset `Y = 6 - 8*i` met le 1er véhicule devant le joueur (comme
  aujourd'hui) et les suivants en file derrière lui, espacés de 8 m — aucun empilement
  possible. `SET_VEHICLE_ON_GROUND_PROPERLY` reste le filet de sécurité vertical.
- **Option v2 (à ne faire que si le placement simple déçoit en test)** : remplacer l'offset
  par `PATHFIND::GET_NTH_CLOSEST_VEHICLE_NODE_WITH_HEADING(pos.x, pos.y, pos.z,
  convoyIndex + 1, &nodePos, &nodeHeading, &numLanes, 1, 3.f, 0.f)` pour poser chaque
  véhicule sur la route avec le bon cap, avec repli sur l'offset simple si la native
  renvoie faux. À garder pour une itération ultérieure : le plan de base n'en a pas besoin.

### Étape 1.2 — Boucler la cascade dans `AssignBodyguardsToEscort` (moyen)

- Remplacer le bloc `if (!pending.empty() && g_escortSpawnIfFull)` (`BodyguardEscort.cpp:540-564`)
  par :

```cpp
if (!pending.empty() && g_escortSpawnIfFull)
{
    GTAmodel::Model m(g_escortVehicleModel);
    if (m.IsInCdImage() && m.IsVehicle() && m.Load(4000))
    {
        int spawnedVehicles = 0;
        const int kMaxEscortVehiclesPerAssign = 4; // MAX_BODYGUARDS = 7 -> 4 suffit même en 2 places
        while (!pending.empty() && spawnedVehicles < kMaxEscortVehiclesPerAssign)
        {
            const size_t before = pending.size();
            Vehicle escortVeh = SpawnOneEscortVehicle(player, m, spawnedVehicles);
            if (!EntityVehicleExists(escortVeh))
                break; // échec CREATE_VEHICLE (pool plein, etc.)
            ++spawnedVehicles;

            FillDriverSeat(escortVeh, pending);
            FillPassengerSeats(escortVeh, pending);

            if (pending.size() == before)
            {
                // Personne n'a pu s'asseoir : supprimer CE véhicule précis et arrêter.
                ENTITY::SET_ENTITY_AS_MISSION_ENTITY(escortVeh, true, true);
                GTAvehicle(escortVeh).Delete();
                s_spawnedEscortVehicles.erase(
                    std::remove(s_spawnedEscortVehicles.begin(), s_spawnedEscortVehicles.end(), escortVeh),
                    s_spawnedEscortVehicles.end());
                break;
            }
        }
    }
    else
    {
        Game::Print::PrintBottomCentre("~r~Modèle de véhicule invalide");
    }
    m.Unload(); // un seul Load/Unload pour toute la cascade
}
```

- **Garde-fous anti-boucle infinie** (tous les trois obligatoires) :
  1. plafond dur `kMaxEscortVehiclesPerAssign = 4` ;
  2. test de progression `pending.size() == before` (modèle exotique sans siège
     utilisable → on ne repart pas pour un tour) ;
  3. sortie sur échec de `CREATE_VEHICLE`.
- **IMPORTANT — interaction plan 3 (bugfix n°2)** : ne PAS utiliser
  `DeleteEmptySpawnedEscortVehicles()` pour nettoyer le véhicule resté vide, contrairement
  à l'esquisse de l'audit. Le bugfix n°2 va ajouter un **délai de grâce** à cette fonction
  (un véhicule vide n'y sera plus supprimé immédiatement) ; la suppression explicite du
  handle précis (ci-dessus) rend le lot 1 indépendant de l'ordre de livraison des deux plans.
- Le toast final existant (lignes 566-572, « Gardes affectés : X/Y ») reste inchangé : il
  compte correctement grâce à `initialPending`.

### Étape 1.3 — Rien d'autre à toucher

- `HasActiveEscortConvoy()` (`BodyguardEscort.cpp:467-480`), l'auto-assign au spawn
  (`BodyguardSpawn.cpp:134-135`) et le bouton du menu (`BodyguardEscort.cpp:716-718`)
  appellent tous `AssignBodyguardsToEscort()` : ils profitent de la cascade sans modification.
- `EnsureEscortVehicleDriver` est déjà appelé pour **chaque** véhicule de
  `s_spawnedEscortVehicles` dans `TickEscort` (lignes 592-593) : les véhicules 2..N auront
  leur conducteur maintenu comme le premier.

**Natives utilisées** : uniquement celles déjà présentes (`CREATE_VEHICLE`,
`SET_ENTITY_AS_MISSION_ENTITY`, `SET_VEHICLE_ON_GROUND_PROPERLY`, `SET_VEHICLE_ENGINE_ON`,
`GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS` indirectement via `FillPassengerSeats`).
**Config / French.json** : rien de nouveau dans ce lot.
**Taille du lot : MOYEN** (~60 lignes déplacées/modifiées dans un seul fichier).

---

## 4. LOT 2 — Véhicule détruit (P4) + changement de véhicule du joueur (P3)

**Fichier : `BodyguardEscort.cpp` (fonction `TickEscort`, lignes 575-652).**

> **Dépendance plan 3 — à livrer APRÈS les bugfixes n°1 et n°2** :
> - Bugfix n°1 : `ClearEscortState(bg)` quand `bg.EscortVehicle` n'existe plus
>   (ligne 601 actuelle : `continue` nu). Sans lui, P3/P4 libèrent des gardes que le tick
>   re-perd aussitôt.
> - Bugfix n°2 : réécriture de `DeleteEmptySpawnedEscortVehicles` (délai de grâce + critère
>   « aucun garde affecté »). P4 ajoute un critère « épave » à cette même fonction : le faire
>   après la réécriture évite un conflit de merge et un double travail.
> Si le plan 3 n'est pas encore livré, le lot 2 reste implémentable mais il faut inclure
> soi-même le bugfix n°1 (1 ligne) — le signaler dans le commit.

### Étape 2.1 — P4 : libérer les gardes d'une épave (petit)

- Dans la boucle par garde de `TickEscort`, juste après le bloc
  `if (!EntityVehicleExists(veh)) { ClearEscortState(bg); continue; }` (lignes 606-610),
  ajouter :

```cpp
if (ENTITY::IS_ENTITY_DEAD(veh) || !VEHICLE::IS_VEHICLE_DRIVEABLE(veh, FALSE))
{
    ClearEscortState(bg); // rend le garde au groupe vanilla -> il resuit le joueur
    continue;
}
```

- Dans `DeleteEmptySpawnedEscortVehicles` (version post-bugfix n°2), ajouter le même
  critère : une épave (`IS_ENTITY_DEAD || !IS_VEHICLE_DRIVEABLE`) spawnée par le mod est
  supprimable même si des cadavres l'occupent encore, sans attendre le délai de grâce.
- Effet combiné avec le lot 1 : au prochain « Assign » (manuel ou auto-assign d'un nouveau
  spawn), la cascade respawne un véhicule de remplacement.
- **Natives nouvelles** : `VEHICLE::IS_VEHICLE_DRIVEABLE`, `ENTITY::IS_ENTITY_DEAD`.

### Étape 2.2 — P3 : détecter le changement de véhicule du joueur (moyen)

- En tête de `TickEscort` (après le calcul de `playerVeh`, lignes 580-585), ajouter une
  détection à mémoire statique :

```cpp
static Vehicle s_lastPlayerVeh = 0;
static int s_playerVehChangedAt = 0;

if (playerVeh != s_lastPlayerVeh)
{
    // Le joueur vient de changer de véhicule (ou d'en sortir/monter).
    if (s_lastPlayerVeh != 0 && !IsSpawnedEscortVehicle(s_lastPlayerVeh))
    {
        // Libérer les gardes restés assis dans l'ANCIEN véhicule personnel du joueur.
        for (auto& bg : BodyguardDb)
        {
            if (bg.EscortVehicle.Exists() && bg.EscortVehicle.GetHandle() == s_lastPlayerVeh)
                ClearEscortState(bg);
        }
    }
    s_lastPlayerVeh = playerVeh;
    s_playerVehChangedAt = now; // 'now' : déplacer sa déclaration (ligne 595) AVANT ce bloc
}

// Ré-affectation automatique une fois le nouveau véhicule stable (anti-rebond 2 s).
if (playerVeh != 0 && s_playerVehChangedAt != 0 &&
    now - s_playerVehChangedAt > 2000)
{
    s_playerVehChangedAt = 0;
    if (g_escortAutoAssignNewSpawns) // réutilise le toggle existant, pas de nouvelle option
        AssignBodyguardsToEscort();
}
```

- Points de vigilance :
  - **Ne libérer que si l'ancien véhicule n'est PAS un véhicule d'escorte spawné**
    (`IsSpawnedEscortVehicle`) : un garde passager d'un véhicule du convoi ne doit pas être
    éjecté quand le joueur change de voiture.
  - Le cas « joueur descend à pied et s'éloigne » (passagers qui restent assis dans SA
    voiture pour toujours) est le **bugfix n°6 du plan 3** — même zone de code. Si le
    bugfix n°6 est déjà livré, fusionner les deux détections dans le même bloc statique
    (une seule paire `s_lastPlayerVeh`/timestamp) plutôt que d'en empiler deux.
  - La ré-affectation auto est volontairement conditionnée au toggle existant
    `g_escortAutoAssignNewSpawns` (déjà persisté `escort_auto_assign`) : pas de nouvelle
    clé de config, comportement opt-in cohérent (« auto-assign » = « le mod gère seul »).
  - `AssignBodyguardsToEscort` contient des `WAIT(0)` (via `ForcePedIntoSeat`) : c'est
    déjà le cas pour l'auto-assign au spawn, acceptable dans le tick à 750 ms.
- **Natives nouvelles** : aucune.

**Config / French.json** : rien de nouveau.
**Taille du lot : MOYEN** (~40 lignes, un seul fichier).

---

## 5. LOT 3 — Réserver les gardes « Driver » pour les volants (P5, petit)

**Fichier : `BodyguardEscort.cpp` (fonction `AssignBodyguardsToEscort`).**

- Juste avant `if (g_escortUseMySeatsFirst && EntityVehicleExists(playerVeh))` (ligne 524),
  si un débordement vers des véhicules d'escorte est prévisible, repousser les Drivers en
  fin de liste `pending` (les remplissages passagers consomment la liste dans l'ordre,
  `FillDriverSeat` préfère déjà les Drivers parmi ce qui reste — lignes 326-339) :

```cpp
if (g_escortSpawnIfFull || !s_spawnedEscortVehicles.empty())
{
    std::stable_partition(pending.begin(), pending.end(),
        [](const BodyguardEntity* bg) { return bg->Role != BodyguardRole::Driver; });
}
```

- `std::stable_partition` conserve l'ordre relatif du reste de l'escouade (l'ordre de la
  base reste la règle pour les non-Drivers).
- Pas besoin de compter précisément les sièges libres du véhicule du joueur : si
  finalement tout le monde tient dans la voiture du joueur, un Driver assis passager n'est
  pas un problème (il n'y a pas de véhicule d'escorte à conduire).
- `<algorithm>` est déjà inclus (ligne 19).

**Natives / config / French.json** : rien de nouveau. **Taille : PETIT** (~6 lignes).

---

## 6. LOT 4 — Pack performance du tick (P8) + bornes de sièges (P10)

Cinq correctifs indépendants les uns des autres, livrables en un commit.

### Étape 4.1 — Scans de peds sans allocation massive (petit, gain fort)

- `World::GetNearbyPeds(result, ped, radius)` (3 args) délègue à la surcharge 4 args avec
  `maxAmount = 10000`, qui alloue `std::vector<int> handles(20002)` (~80 Ko) **à chaque
  appel** (`Solution/source/Scripting/World.cpp:204-215`).
- Remplacer les deux appels chauds :
  - `BodyguardCombat.cpp:273` (TickRetaliate, toutes les 500 ms) :
    `World::GetNearbyPeds(peds, GTAped(playerPed), 60.0f, 128);`
  - `BodyguardCombat.cpp:297` (FindWantedThreat, toutes les 700 ms) :
    `World::GetNearbyPeds(peds, GTAped(playerPed), 90.0f, 128);`
- 128 est très au-dessus du nombre de peds que `GET_PED_NEARBY_PEDS` renvoie réellement
  autour du joueur (~30-60 max en pratique). Ne PAS modifier `World.cpp` (utilisé partout
  ailleurs) : on change seulement nos deux appels.

### Étape 4.2 — Godmode véhicule appliqué une fois, pas en boucle (petit)

- `ApplyEscortVehicleGodmode` (8+ natives) est appelé aujourd'hui :
  - pour chaque véhicule dans `DeleteEmptySpawnedEscortVehicles` (`BodyguardEscort.cpp:141`) ;
  - pour chaque garde affecté dans `TickEscort` (`BodyguardEscort.cpp:612`) — toutes les 750 ms.
- Correctif : mémoriser la dernière valeur appliquée et ne ré-appliquer que sur changement
  du toggle. Dans le namespace anonyme de `BodyguardEscort.cpp` :

```cpp
static bool s_lastAppliedVehGodmode = false;
static bool s_vehGodmodeDirty = true; // force la 1re application

void ReapplyEscortGodmodeIfChanged()
{
    if (!s_vehGodmodeDirty && s_lastAppliedVehGodmode == g_escortVehicleGodmode)
        return;
    for (Vehicle veh : s_spawnedEscortVehicles)
        ApplyEscortVehicleGodmode(veh);
    s_lastAppliedVehGodmode = g_escortVehicleGodmode;
    s_vehGodmodeDirty = false;
}
```

  - Appeler `ReapplyEscortGodmodeIfChanged()` une fois en tête de `TickEscort` ;
  - supprimer l'appel ligne 141 (dans la purge) et l'appel ligne 612 (boucle par garde) ;
  - conserver l'appel au spawn (`SpawnOneEscortVehicle`, lot 1) et celui de la boucle de
    réutilisation d'`AssignBodyguardsToEscort` (ligne 535) peut aussi être supprimé
    (couvert par le tick).
  - Piège : le toggle est aussi modifiable pendant que le convoi roule → le tick à 750 ms
    suffit comme point d'application unique (latence imperceptible).

### Étape 4.3 — Déverrouillage hors du chemin chaud (petit)

- `UnlockVehicleForBodyguards` (11 natives) est ré-exécuté pour chaque conducteur à chaque
  `TickEscort` (`BodyguardEscort.cpp:633`), alors qu'il a déjà été fait au spawn, au seat
  (`SeatBodyguard:261`, `ForcePedIntoSeat:215`) et au reboard (`QueueReboardOrClear:425`).
- Correctif : supprimer la ligne 633. Les portes ne se re-verrouillent pas spontanément ;
  tous les chemins d'entrée dans un siège déverrouillent déjà.

### Étape 4.4 — Throttle du mode 1 « Player Target » (petit)

- `TickShootOnTarget` tourne **chaque frame** (via `TickCombatResponse`, appelé chaque
  frame par `BodyguardTick.cpp:51`) et interroge `GET_ENTITY_PLAYER_IS_FREE_AIMING_AT` /
  `GET_PLAYER_TARGET_ENTITY` + éventuellement les occupants du véhicule visé, à 60 fps.
  En mode 4 (défaut), c'est le seul des trois scanners non throttlé.
- Correctif dans `TickShootOnTarget` (`BodyguardCombat.cpp:213`), même pattern que
  `TickRetaliate` (lignes 266-270) :

```cpp
static int s_nextScan = 0;
int now = MISC::GET_GAME_TIMER();
if (now < s_nextScan) return;
s_nextScan = now + 200; // 5 Hz : imperceptible pour la réactivité de visée
```

  (déplacer la déclaration `now` existante de la ligne 251 en conséquence).

### Étape 4.5 — Auto-assign et blips une seule fois par escouade (petit)

- `SpawnBodyguardPed` (`BodyguardSpawn.cpp:64-138`) fait, **pour chaque ped spawné** :
  - la boucle de rafraîchissement des blips de TOUS les gardes (lignes 126-130) ;
  - `AssignBodyguardsToEscort()` complet si auto-assign actif (lignes 134-135) → spawner
    une escouade de 4 = 4 purges + 4 remplissages + 4 toasts.
- Correctif :
  1. Ajouter un paramètre par défaut à la signature dans `BodyguardSpawn.h` :
     `Ped SpawnBodyguardPed(const GTAmodel::Model& model, const std::string& text, BodyguardRole role, bool deferConvoyAssign = false);`
  2. Dans `SpawnBodyguardPed` :
     - remplacer la boucle blips par le seul nouveau ped :
       `sub::BodyguardMenu::ApplyBodyguardBlipForRole(ped, role);`
     - encadrer l'auto-assign : `if (!deferConvoyAssign && g_escortAutoAssignNewSpawns && HasActiveEscortConvoy()) AssignBodyguardsToEscort();`
  3. Dans `SpawnSquad` (`BodyguardSquads.cpp:221-241`) : appeler
     `SpawnBodyguardPed(model, def.name, m.role, /*deferConvoyAssign=*/true)` puis, après la
     boucle, UNE fois :
     `if (spawned > 0 && g_escortAutoAssignNewSpawns && HasActiveEscortConvoy()) AssignBodyguardsToEscort();`
     (nécessite d'inclure `BodyguardEscort.h` dans `BodyguardSquads.cpp` s'il n'y est pas).
  - Piège : ne pas casser l'appelant mono-spawn `AddOptionBodyGuardPed`
    (`BodyguardSpawn.cpp:153`) — le paramètre par défaut `false` préserve son comportement.

### Étape 4.6 — P10 : bornes de sièges côté combat (trivial)

- `BodyguardCombat.cpp:66` : `for (int seat = -1; seat <= maxPass; ++seat)` →
  `seat < maxPass` (dans `VehicleHasFriendlyOccupant`).
- `BodyguardCombat.cpp:88` : `for (int seat = 0; seat <= maxPass; ++seat)` →
  `seat < maxPass` (dans `FirstOccupant`).
- Aligne sur la convention correcte utilisée partout dans `BodyguardEscort.cpp`
  (lignes 88, 103, 118, 309) : `GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS` renvoie le nombre de
  sièges passagers, donc les index valides sont `-1` (conducteur) et `0..max-1`.

**Natives / config / French.json** : rien de nouveau dans tout le lot 4.
**Taille du lot : PETIT/MOYEN** (6 micro-changements, 4 fichiers).

---

## 7. LOT 5 — Téléportation de rattrapage (P6)

**Fichiers : `BodyguardEscort.cpp/h` (tick + toggle), `BodyguardConfig.cpp` (persistance),
`French.json` (libellé).**

### Étape 5.1 — Nouveau toggle persisté (petit)

- `BodyguardEscort.h` : `extern bool g_escortCatchupTeleport;`
- `BodyguardEscort.cpp` (bloc des globals, lignes 25-31) :
  `bool g_escortCatchupTeleport = true;` (activé par défaut : c'est un filet de sécurité,
  pas un gadget).
- `BodyguardEscortMenu()` (bloc `--- Settings ---`, après « Escort Vehicle Godmode »,
  ligne 733) : `AddToggle("Catch-Up Teleport", g_escortCatchupTeleport);`
- `BodyguardConfig.cpp` :
  - Read : `g_escortCatchupTeleport = ini.GetBoolValue(kSection, "escort_catchup_teleport", g_escortCatchupTeleport);`
  - Save : `ini.SetBoolValue(kSection, "escort_catchup_teleport", g_escortCatchupTeleport);`

### Étape 5.2 — Rattrapage des véhicules du convoi (moyen)

- Dans `TickEscort`, après la boucle `EnsureEscortVehicleDriver` (lignes 592-593), ajouter
  (throttle intégré : le tick est déjà à 750 ms, ajouter un anti-spam par véhicule via une
  petite map statique `Vehicle -> int lastWarpAt`, warp au plus toutes les 5 s) :

```cpp
if (g_escortCatchupTeleport)
{
    constexpr float kCatchupDistSq = 150.f * 150.f;
    for (Vehicle veh : s_spawnedEscortVehicles)
    {
        if (!EntityVehicleExists(veh)) continue;
        Ped driver = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, -1, false);
        if (driver == 0 || !IsTrackedAliveBodyguard(driver)) continue;
        if (DistSq(veh, playerPed) <= kCatchupDistSq) continue;   // helper local, voir note
        if (ENTITY::IS_ENTITY_ON_SCREEN(veh)) continue;           // jamais à l'écran
        // ... anti-spam 5 s par véhicule ...
        Vector3 warpPos; float warpHeading;
        Vector3 behind = player.GetOffsetInWorldCoords(Vector3(0.f, -20.f, 0.f));
        if (PATHFIND::GET_NTH_CLOSEST_VEHICLE_NODE_WITH_HEADING(
                behind.x, behind.y, behind.z, 1, &warpPos, &warpHeading, nullptr, 1, 3.f, 0.f))
        {
            ENTITY::SET_ENTITY_COORDS(veh, warpPos.x, warpPos.y, warpPos.z, false, false, false, false);
            ENTITY::SET_ENTITY_HEADING(veh, warpHeading);
        }
        else
        {
            ENTITY::SET_ENTITY_COORDS(veh, behind.x, behind.y, behind.z, false, false, false, false);
            ENTITY::SET_ENTITY_HEADING(veh, player.GetHeading());
        }
        VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(veh, 5.0f);
        // Invalider le cache de tâche du conducteur pour re-tasker aussitôt :
        for (auto& bg : BodyguardDb)
            if (bg.EscortVehicle.Exists() && bg.EscortVehicle.GetHandle() == veh)
                bg.DriverTasked = false;
    }
}
```

- Note : `DistanceSq` existe déjà côté combat (`BodyguardCombat.cpp:139-147`) mais dans le
  namespace anonyme d'un autre TU → dupliquer un petit helper `DistSq(Entity, Entity)` dans
  le namespace anonyme de `BodyguardEscort.cpp` (6 lignes) plutôt que d'exposer un header.
- Téléporter le **véhicule** déplace ses occupants avec lui : aucun invariant DB touché.

### Étape 5.3 — Rattrapage des gardes à pied (petit)

- Toujours sous le même toggle, dans la boucle par garde de `TickEscort`… non : les gardes
  à pied **hors escorte** ne passent pas dans `TickEscort` (early-continue ligne 601-602).
  Ajouter la vérification dans `TickBodyguards` (`BodyguardTick.cpp`), dans le bloc 750 ms
  existant (après `TickEscort()`), via une petite fonction exportée
  `TickEscortCatchupOnFoot()` déclarée dans `BodyguardEscort.h` :
  - pour chaque `bg` vivant avec `!bg.EscortVehicle.Exists()` et hors véhicule
    (`PED::GET_VEHICLE_PED_IS_IN(ped, FALSE) == 0`), si `DistSq > 150²` et
    `!IS_ENTITY_ON_SCREEN(ped)` : `SET_ENTITY_COORDS` vers
    `player.GetOffsetInWorldCoords(Vector3((i % 2) ? 2.f : -2.f, -3.f, 0.f))` (léger
    étalement latéral pour ne pas les empiler), puis rien d'autre — l'IA de groupe vanilla
    reprend la formation.
  - Ne PAS téléporter un garde en combat (`IsPedBusyFighting` est dans le namespace anonyme
    d'Escort : le helper exporté y a accès).
- **Natives nouvelles du lot** : `PATHFIND::GET_NTH_CLOSEST_VEHICLE_NODE_WITH_HEADING`,
  `ENTITY::IS_ENTITY_ON_SCREEN`, `ENTITY::SET_ENTITY_COORDS`, `ENTITY::SET_ENTITY_HEADING`.

**Nouvelles clés de config** : `escort_catchup_teleport` (bool, défaut `true`).
**Nouvelles entrées French.json** :
`"Catch-Up Teleport": "Téléportation de rattrapage"`.
**Taille du lot : MOYEN** (~80 lignes, 4 fichiers).

---

## 8. LOT 6 — Embarquement réaliste optionnel (P7)

**Fichiers : `BodyguardEscort.cpp/h`, `BodyguardConfig.cpp`, `French.json`.**

### Étape 6.1 — Nouveau toggle persisté (petit)

- `BodyguardEscort.h` : `extern bool g_escortRealisticBoarding;`
- `BodyguardEscort.cpp` : `bool g_escortRealisticBoarding = false;` (défaut OFF : le warp
  actuel est fiable, le mode réaliste est cosmétique).
- Menu (bloc `--- Settings ---`) : `AddToggle("Realistic Boarding", g_escortRealisticBoarding);`
- `BodyguardConfig.cpp` : clé `escort_realistic_boarding` (Read + Save, pattern bool standard).

### Étape 6.2 — Entrée naturelle dans `SeatBodyguard` (moyen)

- Dans `SeatBodyguard` (`BodyguardEscort.cpp:254-284`), avant le `ForcePedIntoSeat` :

```cpp
Vector3 vehPos = ENTITY::GET_ENTITY_COORDS(veh, TRUE);
Vector3 pedPos = ENTITY::GET_ENTITY_COORDS(ped, TRUE);
const bool nearEnough = /* DistSq(pedPos, vehPos) < 25*25 */;
if (g_escortRealisticBoarding && nearEnough)
{
    TASK::TASK_ENTER_VEHICLE(ped, veh, 8000, seat, 2.0f, 1, 0); // flag 1 = entrée normale
    bg.EscortVehicle = GTAentity(veh);
    bg.EscortSeat = seat;
    bg.DriverTasked = false;
    bg.LastEscortPlayerInVehicle = false;
    bg.LastEscortTargetVehicle = 0;
    bg.NextReboardAt = MISC::GET_GAME_TIMER() + 8000; // laisser le temps de marcher
    bg.ReboardAttempts = 0;
    return true; // "affecté" = en route vers le siège
}
// sinon : chemin warp actuel inchangé (ForcePedIntoSeat)
```

- **Le suivi asynchrone existe déjà** : si le garde n'est pas assis au tick suivant,
  `TickEscort` ligne 614 (`!IsPedInVehicle`) enchaîne sur `QueueReboardOrClear`
  (lignes 411-444) qui re-tente `TASK_ENTER_VEHICLE` puis force le warp après 3 tentatives.
  Régler `NextReboardAt = now + 8000` au moment de l'affectation évite que le reboard
  interrompe la marche initiale. C'est exactement le pattern demandé « warp de secours
  après délai » — quasiment aucun code nouveau côté tick.
- **Piège 1 — comptage du toast** : `AssignBodyguardsToEscort` compte `assigned` via la
  décroissance de `pending`. Avec le retour `true` anticipé ci-dessus, un garde « en route »
  compte comme affecté : c'est le comportement voulu (l'audit le demande explicitement).
- **Piège 2 — sièges convoités** : deux gardes en marche vers le même véhicule ne peuvent
  pas viser le même siège : `FillPassengerSeats` teste `IsSeatAvailableForPed(0, veh, seat)`
  qui verra le siège encore vide pendant la marche. Protection : `TrySeatAnyPending` affecte
  des sièges distincts par construction (un seul garde par index de siège dans la boucle) —
  vérifier seulement que `AdoptExistingSeat`/`GetPedSeatInVehicle` réconcilient bien le
  siège réel à l'arrivée : c'est déjà fait par `TickEscort` lignes 623-628
  (resynchronisation `physicalSeat`).
- **Piège 3 — véhicule en mouvement** : `TASK_ENTER_VEHICLE` échoue si le véhicule roule
  déjà ; le filet `QueueReboardOrClear` finit par warper. Acceptable pour une option
  cosmétique OFF par défaut.
- **Natives nouvelles** : aucune (`TASK_ENTER_VEHICLE` déjà utilisé ligne 439).

**Nouvelles clés de config** : `escort_realistic_boarding` (bool, défaut `false`).
**Nouvelles entrées French.json** :
`"Realistic Boarding": "Embarquement réaliste (sans téléportation)"`.
**Taille du lot : MOYEN** (~40 lignes, 4 fichiers).

---

## 9. LOT 7 — Anti-bégaiement des re-tasks combat (P9)

**Fichiers : `BodyguardCombat.cpp`, `BodyguardManagement.h`.**

> **Dépendance plan 3 — à livrer APRÈS le bugfix n°4** (relâchement de
> `SET_PED_KEEP_TASK(FALSE)` en fin de combat). Le bugfix n°4 introduit très probablement
> un suivi « ce garde a reçu une tâche de combat » dans `BodyguardEntity` ; le lot 7
> réutilise ce même champ au lieu d'en créer un doublon. Implémenter P9 avant créerait un
> conflit direct sur `TaskBodyguardOnTarget`.

### Étape 7.1 — Champ de suivi de cible (petit)

- `BodyguardManagement.h`, classe `BodyguardEntity` (ajouter EN FIN de bloc v2, jamais
  au milieu — même règle que le commentaire ligne 20) :

```cpp
Ped LastCombatTarget{ 0 };   // runtime handle — NEVER persisted
int LastCombatTaskAt{ 0 };
```

  (si le bugfix n°4 a déjà ajouté un champ équivalent, le réutiliser tel quel).

### Étape 7.2 — Ne re-tasker que si nécessaire (moyen)

- Dans `TaskBodyguardOnTarget` (`BodyguardCombat.cpp:158-197`), après les garde-fous
  d'existence (ligne 167), ajouter :

```cpp
// Déjà en train de combattre CETTE cible -> ne pas réémettre la tâche
// (la forme à 2 arguments est valide ici : garde vs cible ennemie, contrairement
// au cas garde vs joueur documenté dans BodyguardEscort.cpp:401-403).
if (bg.LastCombatTarget == targetPed && PED::IS_PED_IN_COMBAT(bgPed, targetPed))
    return;
```

  et en fin des deux branches (véhicule / à pied), avant `return` :
  `bg.LastCombatTarget = targetPed; bg.LastCombatTaskAt = MISC::GET_GAME_TIMER();`
- Réinitialiser `LastCombatTarget = 0` :
  - dans le code de fin de combat ajouté par le bugfix n°4 (point de relâchement naturel) ;
  - dans `ClearEscortState` n'est PAS nécessaire (champ combat, pas escorte) ;
  - au spawn (`BodyguardEntity ent{}` initialise déjà à 0).

### Étape 7.3 — Arbitrage des cibles en mode 4 « Full Protection » (moyen)

- Problème : en mode 4 (`BodyguardCombat.cpp:376-381`), `TickShootOnTarget`,
  `TickRetaliate` et `TickWantedThreats` peuvent tasker la squad sur des cibles différentes
  toutes les ~500-700 ms → ping-pong.
- Correctif minimal : centraliser l'émission dans une petite structure de priorité au
  niveau du namespace anonyme :

```cpp
// Priorité : 0 = cible visée par le joueur, 1 = riposte, 2 = menace wanted.
static Ped s_pendingTarget = 0;
static int s_pendingPriority = 99;

void ProposeSquadTarget(Ped target, int priority)
{
    if (target == 0) return;
    if (priority < s_pendingPriority) { s_pendingTarget = target; s_pendingPriority = priority; }
}
```

  - Les trois `Tick*` appellent `ProposeSquadTarget(target, prio)` au lieu de
    `TaskAllBodyguardsOnTarget(target)` directement (en gardant leurs throttles et caches
    `s_lastTarget` internes intacts) ;
  - `TickCombatResponse` (point d'entrée unique, `BodyguardCombat.cpp:357`) vide la
    proposition en fin de frame : si `s_pendingTarget != 0`,
    `TaskAllBodyguardsOnTarget(s_pendingTarget)` une seule fois, puis reset
    (`s_pendingTarget = 0; s_pendingPriority = 99;`).
  - En modes 1/2/3 (un seul scanner actif), le comportement est identique à aujourd'hui.
- **Natives nouvelles** : `PED::IS_PED_IN_COMBAT` (déjà utilisée ailleurs dans le TU,
  ligne 310).

**Config / French.json** : rien de nouveau.
**Taille du lot : MOYEN** (~70 lignes, 2 fichiers).

---

## 10. Récapitulatif des nouvelles clés de config et libellés

### `menyooConfig.ini` `[bodyguards]` (via `BodyguardConfig.cpp`, Read + Save)

| Clé | Type | Défaut | Lot |
|-----|------|--------|-----|
| `escort_catchup_teleport` | bool | `true` | 5 |
| `escort_realistic_boarding` | bool | `false` | 6 |

Rappel du pattern : lecture dans `ReadBodyguardConfig` (AUCUNE native autorisée dedans),
écriture dans `SaveBodyguardConfig`, clamp/validation à la lecture si numérique.

### `French.json` (clé anglaise dans le code → entrée FR)

| Clé anglaise (code) | Valeur française |
|---|---|
| `"Catch-Up Teleport"` | `"Téléportation de rattrapage"` |
| `"Realistic Boarding"` | `"Embarquement réaliste (sans téléportation)"` |

Les lots 1-4 et 7 n'ajoutent AUCUN libellé de menu. Valider le JSON après édition
(367 clés actuellement ; +2 attendues).

---

## 11. Risques et pièges transverses

1. **Invariant DB** : aucun lot ne crée de ped → `BodyguardDb`/`s_bodyguards` ne sont
   jamais touchés hors `SpawnBodyguardPed`. Les véhicules d'escorte ne sont PAS dans la DB :
   seule `s_spawnedEscortVehicles` les suit — toute suppression de véhicule doit retirer le
   handle de ce vecteur (lot 1, étape 1.2, le fait explicitement).
2. **Handles runtime jamais persistés** : `LastCombatTarget` (lot 7) est un handle → ne
   JAMAIS l'écrire dans l'ini ni dans les squads JSON (même règle que `EscortVehicle`,
   commentaire `BodyguardManagement.h:22`).
3. **`ReadBodyguardConfig` sans natives** : les deux nouveaux booléens sont de simples
   lectures ini, pas de pattern « pending » nécessaire (contrairement à `formation_index`).
4. **Ordre avec le plan 3** :
   - la purge des véhicules vides (`DeleteEmptySpawnedEscortVehicles`) sera réécrite par le
     bugfix n°2 (délai de grâce + « aucun garde affecté ») → le lot 1 supprime son véhicule
     raté par handle précis, PAS via la purge (déjà intégré à l'étape 1.2) ; le critère
     « épave » du lot 2 (étape 2.1) s'ajoute à la version réécrite ;
   - le lot 2 suppose le bugfix n°1 (`ClearEscortState` sur handle mort, ligne 601) ;
   - le lot 7 suppose le bugfix n°4 (relâchement `KEEP_TASK`) ;
   - le lot 3 et le lot 4 sont sans dépendance et peuvent être livrés à tout moment.
5. **`WAIT(0)` dans le tick** : `AssignBodyguardsToEscort` (appelée par le lot 2 depuis
   `TickEscort`) peut céder la main plusieurs frames via `ForcePedIntoSeat`. C'est déjà le
   cas aujourd'hui via l'auto-assign au spawn ; ne pas ajouter d'appel dans un chemin
   par-frame (uniquement dans le bloc 750 ms).
6. **Cascade et pool de véhicules** : `CREATE_VEHICLE` peut échouer en zone chargée ; la
   boucle du lot 1 sort proprement (test `EntityVehicleExists`). Ne pas réessayer en boucle
   dans la même frame.
7. **`SET_ENTITY_COORDS` sur un véhicule occupé** (lot 5) déplace les occupants — c'est le
   comportement voulu. Ne jamais téléporter le véhicule si le JOUEUR est dedans (impossible
   par construction : on ne téléporte que les véhicules distants de 150 m et hors écran).
8. **Compat sauvegardes/squads JSON** : aucun champ persisté nouveau côté squads ; rien à
   migrer.

---

## 12. Critères de test en jeu (à faire par l'utilisateur)

### Lot 1 — Cascade + placement
1. Régler « Spawn Escort Vehicle If Full » = ON, modèle `police`. Spawner 7 gardes.
   « Assign Bodyguards To Escort » à pied : **2 véhicules** apparaissent (4 + 3), en file
   (le 2e derrière vous, ~8 m d'écart), sans collision ni empilement. Toast « Gardes
   affectés : 7/7 ».
2. Même test en étant DANS une voiture 2 places : vos sièges se remplissent d'abord
   (1 passager), puis 2 véhicules d'escorte pour les 6 restants.
3. Modèle `insurgent` (beaucoup de places) : 1 seul véhicule spawné pour 7 gardes.
4. « Spawn Escort Vehicle If Full » = OFF : comportement inchangé (toast rouge X/Y, pas de
   cascade).
5. Rouler 2-3 minutes : les 2 véhicules suivent en `TASK_VEHICLE_ESCORT`, chacun avec
   conducteur maintenu (tuer un conducteur → un passager reprend le volant).

### Lot 2 — Véhicule détruit / changement de véhicule
6. Faire exploser un véhicule d'escorte : les survivants reviennent en formation à pied en
   ~1 s (plus de gardes qui tournent autour de l'épave) ; l'épave spawnée disparaît.
7. Avec « Auto-Assign New Spawns » = ON : descendre de votre voiture avec des gardes
   passagers, monter dans une AUTRE voiture, attendre ~2 s → les gardes quittent l'ancienne
   voiture et se réaffectent (vos sièges + convoi/cascade).
8. Avec « Auto-Assign New Spawns » = OFF : changer de voiture libère quand même les gardes
   de l'ancienne (ils resuivent à pied), mais aucune ré-affectation automatique.

### Lot 3 — Drivers réservés
9. Escouade avec 2 Drivers + 5 autres, joueur dans une berline 4 places, cascade ON :
   après « Assign », vérifier (menu Bodyguard List / rôles) que les conducteurs des
   véhicules d'escorte sont bien les Drivers, pas des Riflemen.

### Lot 4 — Performance
10. Mode combat 4 + 7 gardes + zone dense (Legion Square) : pas de chute de FPS perceptible
    par rapport à avant (comparaison subjective) ; viser des peds en rafale déclenche
    toujours l'attaque en <0,3 s (throttle 200 ms imperceptible).
11. Spawner une escouade de 4 avec auto-assign ON : UN seul toast « Gardes affectés »,
    pas quatre ; blips corrects sur tous.
12. Toggle « Escort Vehicle Godmode » ON/OFF pendant que le convoi roule : appliqué en
    <1 s aux véhicules spawnés (tirer dessus pour vérifier).

### Lot 5 — Rattrapage
13. « Catch-Up Teleport » = ON : semer le convoi (course en ville, virages serrés), puis
    regarder devant soi ~10 s : le convoi réapparaît derrière vous (jamais À L'ÉCRAN au
    moment du warp). OFF : le convoi reste largué (comportement actuel).
14. Laisser 2 gardes à pied à >150 m (sans escorte) : ils se téléportent près de vous hors
    champ. Un garde en combat ne doit PAS être téléporté.

### Lot 6 — Embarquement réaliste
15. « Realistic Boarding » = ON, gardes à <25 m : « Assign » → ils MARCHENT jusqu'aux
    portes et montent (pas de pop). Si un garde est bloqué (mur), il est warpé après
    ~10-15 s (filet reboard). OFF : warp instantané comme aujourd'hui.

### Lot 7 — Anti-bégaiement
16. Mode 1 : viser un ennemi en continu : les gardes tirent SANS micro-pauses toutes les
    3 s (avant : redémarrage visible de l'animation de visée).
17. Mode 4 + 3 étoiles + un ennemi au sol qui vous tire dessus : la squad ne « clignote »
    plus entre deux cibles ; elle traite la cible prioritaire (celle qui vous blesse) puis
    passe aux policiers.

### Après CHAQUE lot
- Build Release x64 : 0 erreur, pas de nouvel avertissement.
- Round-trip config : modifier les toggles → quitter/relancer → valeurs conservées
  (`menyooConfig.ini [bodyguards]`).
- Non-régression : spawn 1 garde, Dismiss All, Revive/Heal — aucun crash, DB cohérente
  (log debug `DbgLogSquadState` sans desync).

---

## 13. Estimation globale

| Lot | Taille | Risque |
|-----|--------|--------|
| 1 (cascade + placement) | Moyen (~60 lignes, 1 fichier) | Faible (garde-fous triples) |
| 2 (détruit + changement véhicule) | Moyen (~40 lignes) | Moyen (interactions bugfixes 1/2/6) |
| 3 (Drivers réservés) | Petit (~6 lignes) | Très faible |
| 4 (perf + bornes) | Petit/Moyen (6 micro-fixes) | Faible |
| 5 (rattrapage) | Moyen (~80 lignes) | Moyen (natives pathfind, esthétique du warp) |
| 6 (embarquement réaliste) | Moyen (~40 lignes) | Moyen (asynchrone, dépend du filet reboard) |
| 7 (anti-bégaiement) | Moyen (~70 lignes) | Moyen (couplage bugfix n°4) |

Total estimé : ~300 lignes réparties sur 7 commits indépendants.
