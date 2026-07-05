# Plan 4 — Nouvelles fonctionnalités Bodyguard v2

> Rédigé le 2026-07-04, après le refactor de clarté (Mission 0). Toutes les références fichier:ligne ont été vérifiées contre le code actuel de la branche `Development`.

## Objectif et périmètre

Ajouter au système Bodyguard v2 des fonctionnalités de gameplay, organisées en **jalons livrables** (chaque jalon compile, se teste en jeu et se livre seul) :

1. **Jalon 1 — HUD Bodyguards** (demande explicite n°1) : panneau discret en bas à droite avec avatar (headshot), vie, armure et arme de chaque garde actif.
2. **Jalon 2 — Mode Chauffeur "Drive Me"** (demande explicite n°2) : un garde conduit le joueur (voiture, moto, hélico) vers le waypoint ou en conduite libre.
3. **Jalon 3 — Medic fonctionnel + commandes tactiques** (top 1 et 2 de l'audit).
4. **Jalon 4 — Répliques vocales + pack qualité de vie** (top 3 et 4 de l'audit).
5. **Jalon 5 — Escorte héliportée** (top 5 de l'audit).

Hors périmètre : traduction des toasts FR codés en dur (P1-1), déplacement de "Wanted Level" (P2-5), rappel héliporté (D2 de l'audit, réserve).

**Invariants à respecter partout** (rappel) :
- `BodyguardDb` et `s_bodyguards` toujours synchronisés → tout spawn passe par `SpawnBodyguardPed` (`BodyguardSpawn.h:39`).
- Jamais persister de handles runtime (ped/vehicle/blip/headshot id).
- `submenu_enum.h` : nouveaux IDs ajoutés **uniquement à la fin** de la plage bodyguard (juste avant `MAX_SUBS`, après `BODYGUARD_SQUAD_MEMBER_EDIT`, ligne 302).
- `ReadBodyguardConfig` ne doit appeler **aucune native** → toute ré-application au chargement passe par un flag "pending" consommé par `TickBodyguards` (pattern `g_formationApplyPending`, `BodyguardConfig.cpp:43-50`).
- Tout nouveau libellé de menu = clé anglaise dans le code + entrée dans `French.json`.

**Point build important** : `premake5.lua` (racine du dépôt, section `files` lignes 17-20) inclut `Solution/source/**.cpp` par glob, mais le `.vcxproj` est généré. **Chaque étape qui crée un fichier `.cpp`/`.h` nécessite de relancer `generate.bat` (premake5 vs2022) avant MSBuild** (cf. `docs/Bodyguards.md` §10).

---

## Jalon 1 — HUD Bodyguards en bas à droite

**Idée** : un empilement vertical de lignes (une par garde, max 7 = `MAX_BODYGUARDS`), en bas à droite. Chaque ligne : headshot (tête du ped), nom + arme tenue, barre de vie, barre d'armure. Garde mort = ligne grisée/rouge. Toggle "Bodyguard HUD" dans Settings, persisté.

### Étape 1.1 — Créer `BodyguardHud.h` / `BodyguardHud.cpp` + étape premake

**Taille : petit** (squelette) — **Fichiers** : nouveaux `Solution/source/Submenus/Bodyguards/BodyguardHud.h` et `.cpp` ; relancer `generate.bat` puis MSBuild.

`BodyguardHud.h` :

```cpp
#pragma once
namespace sub::BodyguardMenu
{
    // Toggle persisted as "hud_enabled" in menyooConfig.ini [bodyguards].
    extern bool g_hudEnabled;

    // Per-frame HUD renderer. Called from TickBodyguards (BodyguardTick.cpp).
    void TickBodyguardHud();

    // Releases every registered headshot slot (call on dismiss-all / toggle OFF).
    void ReleaseAllBodyguardHeadshots();
}
```

### Étape 1.2 — Gestion des headshots (PEDHEADSHOTMANAGER)

**Taille : moyen** — **Fichier** : `BodyguardHud.cpp`.

Le jeu limite le manager à ~34 slots partagés avec le scaleform vanilla ; nous en utilisons au plus 7. Cache local :

```cpp
namespace { // TU-local
struct HeadshotSlot { Ped ped = 0; int id = -1; bool ready = false; std::string txd; };
static std::vector<HeadshotSlot> s_headshots; // <= MAX_BODYGUARDS entrées
}
```

Logique par tick (throttle ~500 ms pour la partie gestion, natives :
`PEDHEADSHOTMANAGER::REGISTER_PEDHEADSHOT`, `IS_PEDHEADSHOT_VALID`, `IS_PEDHEADSHOT_READY`, `GET_PEDHEADSHOT_TXD_STRING`, `UNREGISTER_PEDHEADSHOT` — toutes présentes dans `Natives/natives.h:4540-4546`) :

1. Pour chaque `bg` de `BodyguardDb` avec `bg.Handle.Exists()` : si aucun slot pour ce ped → `id = REGISTER_PEDHEADSHOT(ped)` ; si `id != -1` (échec silencieux sinon, on retentera au tick suivant).
2. Slot non prêt : quand `IS_PEDHEADSHOT_VALID(id) && IS_PEDHEADSHOT_READY(id)` → `txd = GET_PEDHEADSHOT_TXD_STRING(id)`, marquer `ready`.
3. **Libération** : slot dont le ped n'est plus dans `BodyguardDb` (renvoyé/cleanup/delete) → `UNREGISTER_PEDHEADSHOT(id)` + retrait du vecteur. Idem dans `ReleaseAllBodyguardHeadshots()` (appelée quand `g_hudEnabled` passe à OFF, et par sécurité au dismiss-all — voir 1.5).
4. **Rafraîchissement d'apparence** (Wardrobe/ModelChanger) : re-registration paresseuse toutes les ~10 s (unregister + register), simple et suffisant. Optionnel v1 ; à défaut, noter que l'avatar peut être périmé après un changement de tenue.

Piège : ne **jamais** persister `id` ni `txd` ; le txd retourné sert à la fois de dict et de nom de texture dans `DRAW_SPRITE(txd, txd, ...)`.

### Étape 1.3 — Rendu (DRAW_RECT / DRAW_SPRITE / drawstring)

**Taille : moyen** — **Fichier** : `BodyguardHud.cpp`.

Réutiliser exactement les utilitaires existants du mod (coordonnées normalisées 0..1, rect centré) :
- fond/barres : `GRAPHICS::DRAW_RECT` — même pattern que `DrawStatBar` de `Submenus/VehicleSpawner.cpp:1055-1075` (fond + remplissage proportionnel + libellés).
- avatar : `GRAPHICS::DRAW_SPRITE(txd, txd, x, y, w, h, 0.f, r, g, b, a, false, 0)` — pattern `VehicleSpawner.cpp:986`.
- texte : `Game::Print::SetupDraw(0, Vector2(0.f, 0.20f), false, false, false, RGBA(...))` + `Game::Print::drawstring(...)` — pattern `VehicleSpawner.cpp:1187-1190`.

Esquisse de layout (valeurs de départ, à ajuster en jeu) :

```cpp
void sub::BodyguardMenu::TickBodyguardHud()
{
    if (!g_hudEnabled) { /* libération lazy si des slots restent */ return; }
    if (BodyguardDb.empty()) return;
    if (CAM::IS_CUTSCENE_PLAYING() || HUD::IS_PAUSE_MENU_ACTIVE()) return;

    const float rowW = 0.150f, rowH = 0.036f, gap = 0.004f;
    const float x = 1.0f - rowW / 2.0f - 0.005f;          // ancré à droite
    float y = 0.975f - rowH / 2.0f;                        // 1re ligne tout en bas
    int rows = 0;
    for (auto& bg : BodyguardDb)
    {
        if (!bg.Handle.Exists() || rows >= (int)BodyguardManagement::MAX_BODYGUARDS) break/continue;
        Ped ped = bg.Handle.GetHandle();
        const bool dead = ENTITY::GET_ENTITY_HEALTH(ped) <= 0;

        DRAW_RECT(x, y, rowW, rowH, 0, 0, 0, 170, false);                    // fond
        // avatar 0.020 x 0.036 à gauche de la ligne (teinte rouge sombre si mort)
        // nom (bg.Name / bg.HashName) + arme, police 0, échelle ~0.20
        // barre vie   : ratio = GET_ENTITY_HEALTH / GET_ENTITY_MAX_HEALTH   (vert / gris)
        // barre armure: ratio = GET_PED_ARMOUR(ped) / max(1, sub::BodyguardMenu::armor) (bleu)
        y -= (rowH + gap); ++rows;
    }
}
```

- Arme tenue : `WEAPON::GET_CURRENT_PED_WEAPON(ped, &hash, true)` (`natives.h:6649`) puis **`GetWeaponLabel(hash, true)`** (`Scripting/WeaponIndivs.h:80`) qui passe déjà par le label GXT — pas de table à dupliquer. Fallback "Unarmed" si hash inconnu/0.
- Mort : barres à 0, fond `RGBA(60,0,0,170)`, avatar alpha 120, texte gris — pas de nouveau blip (le tick blips gère déjà la mort).
- Ne pas dessiner par-dessus le menu : le menu Menyoo est à gauche par défaut mais `menuPos` est déplaçable ; v1 assume bas-droite libre (le HUD vanilla arme/argent est en haut à droite). Pas de gestion de collision.

### Étape 1.4 — Branchement dans le tick

**Taille : petit** — **Fichier** : `BodyguardTick.cpp` (+ include `BodyguardHud.h`).

`TickBodyguardHud()` doit être appelé **chaque frame** (le rendu GTA ne persiste pas d'une frame à l'autre), donc dans `TickBodyguards()` (`BodyguardTick.cpp:14`, déjà appelé chaque frame par `ThreadMenuLoops2`, `Routine.cpp:3972`) — **avant** l'early-out DB vide n'est pas nécessaire (rien à afficher sans garde), donc l'appeler juste après le bloc formation, avec son propre early-out interne. Le throttle 500 ms ne concerne que la gestion des headshots, pas le dessin.

### Étape 1.5 — Toggle Settings + persistance + libérations

**Taille : petit** — **Fichiers** : `BodyguardSettings.cpp` (fonction `BodyguardOps_`, section `--- Blips ---` renommée en pratique inchangée : ajouter le toggle dans une nouvelle section `--- HUD ---` après les blips, ~ligne 219) ; `BodyguardConfig.cpp` ; `BodyguardManagement.cpp`.

- `AddToggle("Bodyguard HUD", g_hudEnabled);` — si passage ON→OFF, appeler `ReleaseAllBodyguardHeadshots()`.
- Config : lecture `g_hudEnabled = ini.GetBoolValue(kSection, "hud_enabled", g_hudEnabled);` dans `ReadBodyguardConfig` (pas de native → OK direct), écriture symétrique dans `SaveBodyguardConfig` (`BodyguardConfig.cpp:63-82`). Défaut : **true** (feature vitrine).
- `DismissAllBodyguards()` (`BodyguardManagement.cpp`) : appeler `ReleaseAllBodyguardHeadshots()` (les slots seraient libérés de toute façon au tick suivant, mais autant être propre).

**Clés ini nouvelles (jalon 1)** : `hud_enabled` (bool).
**French.json nouvelles (jalon 1)** : `"Bodyguard HUD": "ATH gardes du corps"`, `"--- HUD ---": "--- ATH ---"`.

**Risques jalon 1** : fuite de slots headshot si on oublie une voie de suppression (couvert par la libération "ped absent de la DB" à chaque tick de gestion, qui attrape tous les cas) ; `GET_PEDHEADSHOT_TXD_STRING` sur un id non-ready → crash possible, toujours garder le test valid+ready ; performance négligeable (7 DRAW_RECT×4 + 7 sprites/frame).

**Estimation jalon 1 : moyen** (~250 lignes, 1 nouveau TU, natives bien documentées).

---

## Jalon 2 — Mode Chauffeur ("Drive Me")

**Idée** : un garde (rôle Driver préféré) prend le volant, le joueur est passager ; destination = waypoint carte si posé, sinon conduite libre ; support voiture/moto/hélico ; arrêt propre qui rend le garde au groupe.

### Étape 2.1 — Créer `BodyguardChauffeur.h` / `.cpp` + enum + premake

**Taille : petit** — **Fichiers** : nouveaux `BodyguardChauffeur.h/.cpp` ; `Menu/submenu_enum.h` : ajouter `BODYGUARD_CHAUFFEUR,` **après** `BODYGUARD_SQUAD_MEMBER_EDIT` (ligne 302), juste avant `MAX_SUBS` — fin de plage respectée ; relancer `generate.bat`.

`BodyguardChauffeur.h` :

```cpp
#pragma once
#include "BodyguardManagement.h"
namespace sub::BodyguardMenu
{
    extern std::string g_chauffeurVehicleModel; // "" = reuse g_escortVehicleModel
    extern bool g_chauffeurWarpPlayer;          // true = warp passenger, false = TASK_ENTER_VEHICLE

    bool IsChauffeurPed(Ped ped);      // exclusion pour TickEscort / Combat
    bool IsChauffeurActive();
    void StartChauffeur(bool toWaypoint); // toWaypoint=false => wander
    void StopChauffeur();              // rend le garde au groupe, nettoie le véhicule spawné
    void TickChauffeur();              // appelé par TickBodyguards (~500 ms)
    void BodyguardChauffeurMenu();     // submenu BODYGUARD_CHAUFFEUR
}
```

État TU-local : `s_active`, `s_toWaypoint`, `s_guardHandle (Ped)`, `s_vehicle (Vehicle)`, `s_vehicleWasSpawned (bool)`, `s_lastWaypoint (Vector3)`, `s_arrived (bool)`, `s_playerBoardRequestedAt (int)`.

### Étape 2.2 — Démarrage : choix du garde et du véhicule

**Taille : moyen** — **Fichier** : `BodyguardChauffeur.cpp` (`StartChauffeur`).

1. **Garde** : premier `bg` de `BodyguardDb` vivant (`IsBodyguardAlive`) avec `bg.Role == BodyguardRole::Driver` et sans affectation escorte (`!bg.EscortVehicle.Exists()`) ; fallback : n'importe quel garde vivant non affecté ; sinon toast rouge "No available bodyguard" et abandon. Si le garde est en escorte uniquement (aucun autre candidat), refuser plutôt que voler un pilote de convoi.
2. **Véhicule** :
   - Si le joueur est déjà dans un véhicule → utiliser ce véhicule (`s_vehicleWasSpawned = false`). Si le joueur occupe le siège conducteur, le déplacer d'abord : `PED::SET_PED_INTO_VEHICLE(playerPed, veh, 0)` (siège passager avant) — faire **avant** d'installer le garde.
   - Sinon spawn : modèle = `g_chauffeurVehicleModel` si non vide, sinon `g_escortVehicleModel` (`BodyguardEscort.h:16`). Même validation que l'escorte : `GTAmodel::Model(m).IsInCdImage() && IsVehicle()` (pattern `BodyguardEscort.cpp:556-563`), spawn mission entity, portes déverrouillées, moteur allumé — copier le bloc de spawn escorte (`BodyguardEscort.cpp:527-555`) ou, mieux, extraire un helper commun `SpawnTrackedBodyguardVehicle(model)` dans `BodyguardEscort` et l'exposer via le header. Position : sur la route la plus proche devant le joueur (`PATHFIND::GET_CLOSEST_VEHICLE_NODE_WITH_HEADING`).
3. **Installer le garde conducteur** : sortir du groupe vanilla (même pattern que `RemoveFromPlayerGroupForEscort`, `BodyguardEscort.cpp:182-190` — champ `bg.RemovedFromGroup`/`bg.WasNeverLeavesGroup` réutilisés), puis `PED::SET_PED_INTO_VEHICLE(guard, veh, -1)` (warp direct : fiable), `SET_PED_KEEP_TASK(guard, true)`, et surtout **`PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(guard, true)`** pour qu'il n'abandonne pas le volant sous les tirs (à ré-annuler au stop).
4. **Installer le joueur** : si `g_chauffeurWarpPlayer` → `SET_PED_INTO_VEHICLE(playerPed, veh, 0)` (puis -2 "any free" si occupé) ; sinon `TASK::TASK_ENTER_VEHICLE(playerPed, veh, 20000, 0, 1.f, 1, nullptr)` (`natives.h:5552`).
5. Mémoriser waypoint : `GET_FIRST_BLIP_INFO_ID(8)` (8 = sprite Waypoint, cf. usage `Menu/Routine.cpp:2439`) ; si `toWaypoint` et pas de waypoint → basculer en wander avec toast d'info.

### Étape 2.3 — Tâche de conduite (sol + hélico)

**Taille : moyen** — **Fichier** : `BodyguardChauffeur.cpp`.

Style/vitesse : réutiliser `g_escortDrivingStyleIndex` → `DrivingStyle::nameArray[idx].style` (même conversion index→valeur que `BodyguardEscort.cpp:587-590`). Vitesse : 20.f m/s sol (≈ 72 km/h), 40.f hélico.

- **Sol, waypoint** : `TASK::TASK_VEHICLE_DRIVE_TO_COORD_LONGRANGE(guard, veh, wp.x, wp.y, wp.z, 20.f, style, 12.f)` (`natives.h:5562`, usage de référence `VehicleOptions.cpp:844`).
- **Sol, libre** : `TASK::TASK_VEHICLE_WANDER_STANDARD(guard, veh, 17.f, style)`.
- **Hélico détecté** (`VEHICLE::IS_THIS_MODEL_A_HELI(model)`) :
  - waypoint : `TASK::TASK_HELI_MISSION(guard, veh, 0, 0, wp.x, wp.y, wp.z, 4 /*GoTo*/, 40.f, 40.f, -1.f, 80, 20, -1.f, 0)` (signature `natives.h:5661`, usage de référence `VehicleOptions.cpp:862` et `STSTasks.cpp:1542`) ;
  - à l'approche (< 60 m 2D), ré-émettre avec `missionFlag 20` (**LandAtCoords**) pour atterrir à proximité ;
  - libre : mission 8 (Follow/Protect) autour du joueur n'a pas de sens ici → wander hélico = GoTo vers un point aléatoire à ~1 km, renouvelé à l'arrivée (simple et robuste).

### Étape 2.4 — `TickChauffeur` : maintenance et arrivée

**Taille : moyen** — **Fichiers** : `BodyguardChauffeur.cpp`, `BodyguardTick.cpp`.

Appel depuis `TickBodyguards()` avec throttle ~500 ms (nouveau `s_nextChauffeurTick`, même pattern que `s_nextEscortTick`, `BodyguardTick.cpp:17`). Logique :

1. Invalidation : garde mort/inexistant, véhicule détruit → `StopChauffeur()` (toast).
2. **Sortie du joueur = stop implicite** : si le joueur n'est plus dans le véhicule depuis > 4 s (et qu'on ne lui a pas demandé d'y monter récemment) → `StopChauffeur()`.
3. **Waypoint déplacé** : relire `GET_FIRST_BLIP_INFO_ID(8)` + `GET_BLIP_INFO_ID_COORD` ; si écart > 25 m avec `s_lastWaypoint` → ré-émettre la tâche (2.3). Waypoint supprimé → passer en wander.
4. **Arrivée** (mode waypoint, sol) : distance 2D < 12 m → `TASK::TASK_VEHICLE_TEMP_ACTION(guard, veh, 27 /*frein*/, 3000)` (`natives.h:5643`), toast "Arrived", `s_arrived = true` (le mode reste actif, le garde attend un nouveau waypoint ou le stop). Hélico : après LandAtCoords, considérer arrivé quand `VEHICLE::IS_VEHICLE_ON_ALL_WHEELS` ou vitesse ≈ 0.
5. **Re-task filet** : si `GET_SCRIPT_TASK_STATUS(guard, hash_tâche)` indique la tâche perdue (combat l'a interrompue malgré le blocking), ré-émettre.

**Interactions à câbler (critique)** :
- `TickEscort` (`BodyguardEscort.cpp:597-651`) : en tête de boucle, `if (IsChauffeurPed(bg.Handle.GetHandle())) continue;` — le chauffeur ne doit jamais être re-taské par l'escorte. (Il n'a normalement pas d'`EscortVehicle`, mais `g_escortAutoAssignNewSpawns` + "Assign Bodyguards To Escort" pourraient le capturer : ajouter la même exclusion dans la collecte des candidats de `AssignBodyguardsToEscort`.)
- Combat (`BodyguardCombat.cpp`, fonction de tasking d'escouade — `TaskAllBodyguardsOnTarget` et le tasking individuel) : exclure `IsChauffeurPed(...)` du tasking **à pied** (les gardes assignés à l'escorte sont déjà exclus du foot-task, cf. doc §5 — appliquer la même règle). Le `SET_BLOCKING_OF_NON_TEMPORARY_EVENTS` couvre les événements ambiants ; l'exclusion couvre nos propres ordres.
- `DeleteBodyguard` / `DismissAllBodyguards` (`BodyguardManagement.cpp`) : si le ped supprimé est le chauffeur → `StopChauffeur()` d'abord.

### Étape 2.5 — Arrêt propre

**Taille : petit/moyen** — **Fichier** : `BodyguardChauffeur.cpp` (`StopChauffeur`).

1. `TASK_VEHICLE_TEMP_ACTION(guard, veh, 27, 2500)` (freiner) puis `CLEAR_PED_TASKS` différé — ou directement si le véhicule est presque à l'arrêt.
2. `SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(guard, false)`.
3. Restaurer le groupe : réutiliser le pattern `RestorePlayerGroupAfterEscort` (`BodyguardEscort.cpp:192-206`) via le champ `bg.RemovedFromGroup` — retrouver l'entrée DB par `GetBodyguardIndexInDb(handle)`.
4. Véhicule : si `s_vehicleWasSpawned`, demander au garde de sortir (`TASK_LEAVE_VEHICLE`) puis suppression **uniquement quand physiquement vide** — même règle de sécurité que `DeleteEmptySpawnedEscortVehicles` (`BodyguardEscort.cpp:128-154`) ; le véhicule du joueur n'est jamais supprimé. Si le joueur est encore dedans, laisser le véhicule en place.
5. Remise à zéro de tout l'état TU-local.

### Étape 2.6 — Menu + persistance

**Taille : petit** — **Fichiers** : `BodyguardChauffeur.cpp` (menu), `BodyguardMenu.cpp` (entrée `BodyguardMainMenu`, après "Wanted Level", ligne 494), `BodyguardConfig.cpp`, `French.json`.

Submenu `BODYGUARD_CHAUFFEUR` (REGISTER_SUBMENU en fin de `BodyguardChauffeur.cpp`, pattern `BodyguardEscort.cpp:810`) :

- Statut : "Chauffeur: <nom du garde>" ou "Chauffeur: inactive" (option inerte, pattern `BodyguardEscort.cpp:713`).
- `Drive Me To Waypoint` → `StartChauffeur(true)`.
- `Drive Me Around` → `StartChauffeur(false)`.
- `Stop Chauffeur` → `StopChauffeur()`.
- `--- Settings ---` : `Warp Into Vehicle` (toggle `g_chauffeurWarpPlayer`, défaut true) ; `Chauffeur Vehicle Model` : texter quick-pick (`""` = "Same As Escort", puis `sultan`, `baller`, `bati`, `frogger`) + `Custom Model...` avec la même validation InputBox que l'escorte (`BodyguardEscort.cpp:747-563` pattern) ; le style de conduite reste celui de l'escorte (afficher une ligne inerte "Driving Style: shared with Escort" pour l'expliquer).

Config (`BodyguardConfig.cpp`) : `chauffeur_vehicle_model` (string, défaut `""`), `chauffeur_warp_player` (bool, défaut true). Aucune ré-application par native nécessaire → lecture/écriture directes.

**Clés ini nouvelles (jalon 2)** : `chauffeur_vehicle_model`, `chauffeur_warp_player`.
**French.json nouvelles (jalon 2)** : `"Chauffeur": "Chauffeur"`, `"Drive Me To Waypoint": "Me conduire au repère"`, `"Drive Me Around": "Me conduire (libre)"`, `"Stop Chauffeur": "Arrêter le chauffeur"`, `"Warp Into Vehicle": "Téléporter dans le véhicule"`, `"Chauffeur Vehicle Model": "Véhicule du chauffeur"`, `"Same As Escort": "Même que l'escorte"`, `"Driving Style: shared with Escort": "Style de conduite : partagé avec l'escorte"`, `"Custom Model...": déjà présente ? à vérifier — sinon l'ajouter`.

**Risques jalon 2** : conflit escorte/chauffeur (couvert par `IsChauffeurPed` aux 3 points listés en 2.4) ; groupe vanilla qui éjecte le garde du siège conducteur (couvert par `REMOVE_PED_FROM_GROUP`, exactement le problème déjà résolu par l'escorte, doc §4) ; suppression du véhicule du joueur (interdite par la règle "spawned only + vide") ; `TASK_HELI_MISSION` LandAtCoords peut chercher longtemps une zone plate — prévoir un timeout (~20 s) puis hover + toast.

**Estimation jalon 2 : gros** (~400 lignes, 1 nouveau TU, 3 points d'intégration).

---

## Jalon 3 — Medic fonctionnel + commandes tactiques (audit top 1 et 2)

### Étape 3.1 — Refactor : extraire `ReviveOneBodyguard(bg)`

**Taille : petit** — **Fichier** : `BodyguardMenu.cpp` : le corps de la boucle de `ReviveAllDeadBodyguards` (`BodyguardMenu.cpp:301-327`) devient une fonction réutilisable (déclarée dans `BodyguardMenu.h`) ; `ReviveAllDeadBodyguards` l'appelle en boucle. Aucun changement de comportement.

### Étape 3.2 — `TickMedic` (soigne et ranime)

**Taille : moyen** — **Fichiers** : `BodyguardTick.cpp` (nouvelle fonction TU-locale + appel throttlé ~1 s), `BodyguardSettings.cpp` (toggles), `BodyguardConfig.cpp`.

Machine à états simple, un seul "patient" à la fois par Medic :

1. Chercher un patient : garde mort (`Exists() && IsDead()`) ou blessé (< 40 % de `GET_ENTITY_MAX_HEALTH`), hors Medic lui-même. Chercher le Medic vivant le plus proche (< 60 m).
2. Medic → `TASK::TASK_GO_TO_ENTITY(medic, patient, -1, 1.5f, 3.f, 0, 0)` + retirer temporairement du groupe (pattern escorte) pour que le groupe ne le rappelle pas.
3. À < 2.5 m : anim CPR — `TASK::TASK_PLAY_ANIM(medic, "mini@cpr@char_a@cpr_str", "cpr_pumpchest", ...)` (précharger le dict avec `STREAMING::REQUEST_ANIM_DICT`, attendre `HAS_ANIM_DICT_LOADED` sur 1-2 ticks) pendant ~4 s.
4. Fin d'anim : patient mort → `ReviveOneBodyguard(bg)` (3.1) ; patient blessé → `SET_ENTITY_HEALTH` à max. Réplique vocale du Medic si jalon 4 livré. Restaurer le groupe du Medic.
5. Timeout global 20 s par tentative (Medic bloqué → abandon + restauration).
6. Le Medic en état "soin" est exclu du tasking combat (même mécanisme d'exclusion que le chauffeur — réutiliser une fonction `IsBusyBodyguard(ped)` regroupant chauffeur + medic-en-soin).

Réglages : `Medic Revives/Heals` (toggle, défaut ON), option "Medic Heals Player" (défaut OFF) qui applique le même flow au joueur blessé (< 50 %) en soignant via `SET_ENTITY_HEALTH` (pas de revive joueur).

**Clés ini** : `medic_enabled` (bool), `medic_heal_player` (bool).
**French.json** : `"Medic Revives/Heals": "Le médecin soigne et ranime"`, `"Medic Heals Player": "Le médecin soigne le joueur"`.

### Étape 3.3 — Commandes tactiques (A2, A3, A1)

**Taille : moyen** — **Fichiers** : `BodyguardCombat.h/.cpp`, `BodyguardMenu.cpp` (section `--- Actions ---`, ligne 496), `BodyguardManagement.h`, `BodyguardTick.cpp`.

1. **Attack My Target** (A2, facile) : exposer `TaskAllBodyguardsOnTarget(Entity)` dans `BodyguardCombat.h` (actuellement TU-local) + une fonction `ResolvePlayerTargetEntity()` (réutilise la résolution visée/lock du mode 1, `GET_ENTITY_PLAYER_IS_FREE_AIMING_AT` → `GET_PLAYER_TARGET_ENTITY`, avec les exclusions `IsFriendlyToPlayer`). Bouton dans Actions : si pas de cible → toast "No target".
2. **Cease Fire** (A3, facile) : nouvelle `CeaseFireAll()` dans `BodyguardCombat.cpp` : pour chaque garde vivant `CLEAR_PED_TASKS`, reset des caches de cible internes (`s_lastTarget`…), et `s_combatSuppressedUntil = now + 5000` consulté en tête de `TickCombatResponse()`. Bouton dans Actions.
3. **Hold Position / Follow Me** (A1, moyen) : nouveau champ `bool HoldPosition{false}` dans `BodyguardEntity` (`BodyguardManagement.h:21-30`, **ajouté en fin de bloc v2**, jamais persisté). Global "Hold Positions" (toggle-action dans Actions) + par garde dans `BodyguardEntityOps` (`BodyguardSubmenu.cpp:94-103`). ON : `REMOVE_PED_FROM_GROUP` (pattern escorte, champs `RemovedFromGroup`/`WasNeverLeavesGroup` réutilisés) + `TASK::TASK_START_SCENARIO_IN_PLACE(ped, "WORLD_HUMAN_GUARD_STAND", 0, true)`. OFF : restauration groupe (pattern `RestorePlayerGroupAfterEscort`). Exclusions : un garde Hold n'est pas candidat escorte ; le combat peut toujours le tasker (une sentinelle doit riposter) mais au retour au calme le tick lui ré-applique le scénario (vérif ~2 s dans `TickBodyguards`).

**French.json** : `"Attack My Target": "Attaquer ma cible"`, `"Cease Fire": "Cessez-le-feu"`, `"Hold Position": "Tenir la position"`, `"Follow Me": "Me suivre"`, `"Hold Positions (All)": "Tenir la position (tous)"`, `"Follow Me (All)": "Me suivre (tous)"`, `"No target": "~r~Aucune cible"` (si libellé anglais utilisé pour le toast — sinon toast FR direct comme l'existant).

**Risques jalon 3** : arbitrage combat/médic/hold — centraliser les exclusions dans un helper unique ; ne pas casser l'invariant escorte (un garde Hold assigné à l'escorte : interdire le Hold si `EscortVehicle.Exists()`).

**Estimation jalon 3 : gros** (deux features moyennes ; livrable en deux sous-lots 3.1-3.2 puis 3.3 si besoin).

---

## Jalon 4 — Répliques vocales + pack qualité de vie (audit top 3 et 4)

### Étape 4.1 — Répliques vocales contextuelles (F1)

**Taille : petit/moyen** — **Fichiers** : `BodyguardCombat.cpp`, `BodyguardTick.cpp`, `BodyguardSettings.cpp`, `BodyguardConfig.cpp`.

`AUDIO::PLAY_PED_AMBIENT_SPEECH_NATIVE(ped, speech, "SPEECH_PARAMS_FORCE_SHOUTED", 0)` (`natives.h:132`) aux événements :
- entrée en combat (dans le tasking d'escouade) : un seul garde aléatoire dit `"GENERIC_INSULT_HIGH"` ou `"COVER_ME"` ;
- ordre reçu (Attack/Hold/Follow/chauffeur start) : `"GENERIC_YES"` ;
- garde qui meurt (détecté dans `UpdateBodyguardBlipsOnDeath`, `BodyguardMenu.cpp:176`) : un voisin dit `"GENERIC_FRIGHTENED_HIGH"`.
Throttle global : pas plus d'une réplique toutes les 3 s (`s_nextSpeechAt`). Toggle `Voice Lines` (défaut ON), clé ini `voice_lines_enabled`.
**French.json** : `"Voice Lines": "Répliques vocales"`.

### Étape 4.2 — Persistance des réglages de spawn (G1)

**Taille : petit** — **Fichier** : `BodyguardConfig.cpp` uniquement (le pattern existe 13 fois).

Nouvelles clés : `default_health` (long → `health`), `default_armor` (long → `armor`), `default_godmode` (bool → `godmode`), `auto_arm_new` (bool → `g_autoArmNewBodyguards`), `spawn_weapon_index` (long → `g_spawnWeaponIndex`, clampé 0..9), `arm_weapon_index` (long → `g_armWeaponIndex`, clampé 0..8). Tous définis dans `BodyguardMenu.cpp:39-46`, exposés par `BodyguardMenu.h:17-38`. Aucune native → lecture directe. (B3 formation déjà fait par la Mission 0.)

### Étape 4.3 — Barre d'état dans la Bodyguard List (G3)

**Taille : petit** — **Fichier** : `BodyguardSettings.cpp:59-100` (boucle `BodyguardList`, construction du `label` ligne 68).

`label += " [" + kRoleDisplayLabels[(int)bg.Role] + "] " + std::to_string(hp) + "/" + std::to_string(maxHp)` avec `GET_ENTITY_HEALTH`/`GET_ENTITY_MAX_HEALTH` ; si mort : suffixe `" ~r~[DEAD]"`. Pas de nouvelle clé FR pour les chiffres ; ajouter `"[DEAD]"` seulement si on passe par la traduction (sinon suffixe construit en dur comme les toasts existants — choisir la clé FR : `"[DEAD]": "[MORT]"`).

### Étape 4.4 — Espacement de formation (B1) + blips véhicules d'escorte (G4)

**Taille : petit** — **Fichiers** : `BodyguardSettings.cpp` (sous le texter Formation, ligne 238-249), `BodyguardConfig.cpp`, `BodyguardEscort.cpp`.

- B1 : `AddNumber("Formation Spacing", ...)` 1..10 m → `PED::SET_GROUP_FORMATION_SPACING(group, (float)m, -1.f, -1.f)` (`natives.h:4172`). Clé ini `formation_spacing` (long, défaut 0 = ne rien appliquer), ré-application différée via le flag `g_formationApplyPending` existant (l'étendre en "formation + spacing pending").
- G4 : dans le bloc de spawn du véhicule d'escorte (`BodyguardEscort.cpp:553` après `push_back`) : `Blip b = ADD_BLIP_FOR_ENTITY(veh); SET_BLIP_SPRITE(b, 225); SET_BLIP_COLOUR(b, 3); SET_BLIP_AS_FRIENDLY(b, true);`. Retrait automatique à la suppression du véhicule (les blips d'entité meurent avec l'entité ; sinon `REMOVE_BLIP` dans `DeleteEmptySpawnedEscortVehicles`). Appliquer aussi au véhicule chauffeur spawné (jalon 2) si livré.

**Clés ini nouvelles (jalon 4)** : `voice_lines_enabled`, `default_health`, `default_armor`, `default_godmode`, `auto_arm_new`, `spawn_weapon_index`, `arm_weapon_index`, `formation_spacing`.
**French.json nouvelles (jalon 4)** : `"Voice Lines"`, `"Formation Spacing": "Espacement de formation"`, `"[DEAD]": "[MORT]"` (si utilisé).

**Estimation jalon 4 : moyen** (que du code localisé, zéro nouveau fichier).

---

## Jalon 5 — Escorte héliportée (audit top 5, D1)

### Étape 5.1 — Réglages + spawn de l'hélico d'escorte

**Taille : moyen** — **Fichiers** : `BodyguardEscort.h/.cpp`, `BodyguardConfig.cpp`.

- Globals : `g_escortHeliEnabled` (bool, défaut false), `g_escortHeliModel` (string, défaut `"buzzard2"` — version sans armes verrouillées ; quick-picks `buzzard2`, `polmav`, `maverick`, `frogger`).
- Bouton "Spawn Escort Helicopter" dans `BodyguardEscortMenu` : spawn en l'air (~30 m au-dessus/derrière le joueur, `SET_HELI_BLADES_FULL_SPEED`), pilote = garde Driver préféré non affecté (réutiliser la sélection de pilote de `AssignBodyguardsToEscort`), passagers = gardes libres restants ; véhicule ajouté à `s_spawnedEscortVehicles` (`BodyguardEscort.cpp:37`) pour hériter **gratuitement** du cleanup, du godmode et de la promotion de pilote existants.
- `bg.EscortVehicle`/`EscortSeat` renseignés comme pour un véhicule sol → "Clear Escort Assignments" fonctionne sans changement.

### Étape 5.2 — Branche aérienne de `TickEscort`

**Taille : moyen/difficile** — **Fichier** : `BodyguardEscort.cpp` (`TickEscort`, lignes 597-651).

Dans la boucle driver : si `VEHICLE::IS_THIS_MODEL_A_HELI(GET_ENTITY_MODEL(veh))` :
- remplacer escort/follow par `TASK_HELI_MISSION(ped, veh, playerVeh /*ou 0*/, playerVeh?0:playerPed, 0,0,0, 8 /*Follow*/, 35.f, 40.f, -1.f, 60, 40 /*minHeight*/, -1.f, 0)` — suivi du joueur à ~40 m d'altitude ; le cache de tâche (`IsDriverTaskCurrent`/`MarkDriverTaskState`) est réutilisé tel quel (la cible "player in vehicle" change → re-task, comme au sol).
- **désactiver le reboard-warp** pour les gardes affectés à un hélico en vol (`QueueReboardOrClear`, appelée ligne 616) : un warp dans un hélico en l'air est acceptable, mais un `TASK_ENTER_VEHICLE` au sol vers un hélico en vol boucle à l'infini → si le véhicule est un hélico non posé, warp direct ou clear immédiat selon `g_escortReboardAfterCombat`.
- atterrissage de courtoisie : si le joueur est à pied et immobile > 15 s, mission 20 (LandAtCoords) vers un point proche ; redécollage dès que le joueur bouge (retour mission 8).

**Clés ini nouvelles (jalon 5)** : `escort_heli_enabled` (si on veut un auto-spawn — sinon rien, le bouton suffit ; recommandation : **pas** de clé enabled, juste `escort_heli_model`).
**French.json nouvelles (jalon 5)** : `"Spawn Escort Helicopter": "Faire apparaître l'hélico d'escorte"`, `"Escort Helicopter Model": "Modèle d'hélico d'escorte"`.

**Risques jalon 5** : rotor vs décor au spawn (spawner haut et loin des bâtiments) ; `EnsureEscortVehicleDriver` (`BodyguardEscort.cpp:592`) promeut déjà un passager si le pilote meurt — vérifier que la promotion warp fonctionne en vol (sinon l'hélico tombe : acceptable, le cleanup s'en charge) ; interaction chauffeur-hélico (jalon 2) : deux hélicos possibles, états indépendants, pas de partage de handle.

**Estimation jalon 5 : moyen/gros.**

---

## Récapitulatif des nouvelles clés

### `menyooConfig.ini [bodyguards]` (via `BodyguardConfig.cpp`)

| Jalon | Clé | Type | Global | Défaut |
|---|---|---|---|---|
| 1 | `hud_enabled` | bool | `g_hudEnabled` | true |
| 2 | `chauffeur_vehicle_model` | string | `g_chauffeurVehicleModel` | `""` (= escorte) |
| 2 | `chauffeur_warp_player` | bool | `g_chauffeurWarpPlayer` | true |
| 3 | `medic_enabled` | bool | `g_medicEnabled` | true |
| 3 | `medic_heal_player` | bool | `g_medicHealPlayer` | false |
| 4 | `voice_lines_enabled` | bool | `g_voiceLinesEnabled` | true |
| 4 | `default_health` / `default_armor` / `default_godmode` | long/long/bool | `health` / `armor` / `godmode` | 200/200/true |
| 4 | `auto_arm_new` / `spawn_weapon_index` / `arm_weapon_index` | bool/long/long | `g_autoArmNewBodyguards` / `g_spawnWeaponIndex` / `g_armWeaponIndex` | false/0/0 |
| 4 | `formation_spacing` | long | `g_formationSpacing` | 0 (= vanilla) |
| 5 | `escort_heli_model` | string | `g_escortHeliModel` | `"buzzard2"` |

Aucun handle persisté ; toute clé nécessitant une native au chargement passe par un flag pending consommé dans `TickBodyguards`.

### `French.json` (libellés anglais → FR)

Jalon 1 : `Bodyguard HUD`, `--- HUD ---`.
Jalon 2 : `Chauffeur`, `Drive Me To Waypoint`, `Drive Me Around`, `Stop Chauffeur`, `Warp Into Vehicle`, `Chauffeur Vehicle Model`, `Same As Escort`, `Driving Style: shared with Escort` (+ vérifier `Custom Model...`).
Jalon 3 : `Medic Revives/Heals`, `Medic Heals Player`, `Attack My Target`, `Cease Fire`, `Hold Position`, `Follow Me`, `Hold Positions (All)`, `Follow Me (All)`.
Jalon 4 : `Voice Lines`, `Formation Spacing`, `[DEAD]` (si traduit).
Jalon 5 : `Spawn Escort Helicopter`, `Escort Helicopter Model`.

---

## Risques et pièges transverses

1. **Nouveaux TU** (`BodyguardHud`, `BodyguardChauffeur`) : relancer `generate.bat` sinon MSBuild ne les compile pas — symptôme : link errors sur les nouveaux symboles.
2. **Concurrence des "propriétaires" d'un garde** : escorte, chauffeur, medic-en-soin, hold, combat peuvent vouloir tasker le même ped. Règle unique proposée : un helper `IsBusyBodyguard(ped)` (chauffeur ∪ medic-en-soin) consulté par le tasking combat à pied, l'escorte et l'auto-assign ; hold et escorte mutuellement exclusifs.
3. **Champs `BodyguardEntity`** : ajouter en **fin** de bloc v2 (`BodyguardManagement.h:29-30`), jamais persistés, `operator==` inchangé (handle only).
4. **Groupe vanilla** : chaque retrait (`REMOVE_PED_FROM_GROUP`) doit avoir un chemin de restauration garanti (mort du garde, dismiss, stop du mode) — réutiliser systématiquement les champs `RemovedFromGroup` / `WasNeverLeavesGroup` et le pattern `RestorePlayerGroupAfterEscort`.
5. **Enum submenu** : un seul nouvel ID (`BODYGUARD_CHAUFFEUR`), ajouté avant `MAX_SUBS` — ne rien réordonner.
6. **Ordre des jalons** : 3, 4 et 5 dépendent de helpers créés en 2 (exclusions) et 3.1 (revive) — les livrer dans l'ordre ; si un jalon est sauté, reporter ses helpers.

---

## Critères de test en jeu (par l'utilisateur ; copie du .asi dans le dossier GTA par l'utilisateur, doc §11)

### Jalon 1 — HUD
1. Settings → "Bodyguard HUD" ON : spawner 3 gardes → 3 lignes en bas à droite, avatars = visages corrects (comparer avec le modèle).
2. Blesser un garde (le faire encaisser) → sa barre de vie descend en direct ; retirer l'armure → barre bleue à zéro.
3. Changer l'arme d'un garde (Active Bodyguards → Arm All) → le nom d'arme affiché change.
4. Tuer un garde → ligne grisée/rouge, barres vides ; Revive Dead → ligne redevient normale.
5. Spawner 7 gardes → 7 lignes empilées sans chevaucher le HUD vanilla ; Dismiss All → panneau disparaît.
6. Toggle OFF → panneau disparaît immédiatement ; redémarrer le jeu → l'état du toggle est conservé (`hud_enabled` dans menyooConfig.ini).
7. Ouvrir/fermer le menu pause et une cutscene (mission) → pas de HUD dessiné par-dessus.

### Jalon 2 — Chauffeur
1. Poser un waypoint, "Drive Me To Waypoint" à pied : un véhicule apparaît, un garde (Driver de préférence) au volant, le joueur monte passager (ou warp selon le réglage), trajet jusqu'au repère, freinage, toast d'arrivée.
2. Même test déjà assis dans SA voiture : le garde prend le volant, le joueur passe passager, la voiture du joueur n'est **jamais** supprimée au stop.
3. Déplacer le waypoint en cours de route → le chauffeur change de destination ; supprimer le waypoint → conduite libre.
4. "Drive Me Around" : conduite libre sans destination ; "Stop Chauffeur" : arrêt, le garde sort/reste selon le véhicule, re-suit le joueur à pied (retour au groupe), véhicule spawné supprimé une fois vide.
5. Hélico (`Chauffeur Vehicle Model = frogger`) : décollage, vol vers le waypoint, atterrissage à proximité.
6. Se faire attaquer pendant le trajet (Combat Response = Full Protection) : le chauffeur **reste au volant**, les autres gardes ripostent.
7. Pendant un convoi d'escorte actif : démarrer le chauffeur → le convoi continue de suivre, le chauffeur n'est jamais re-tasker par l'escorte ; sortir du véhicule > 4 s → le mode s'arrête seul.

### Jalon 3 — Medic + commandes
1. Squad FIB (contient un Medic) ; tuer un Rifleman (godmode OFF) → le Medic court, anim de réanimation, le garde se relève avec vie/armure/rôle/blip corrects.
2. Blesser un garde < 40 % → le Medic le soigne ; "Medic Heals Player" ON + joueur blessé → soin du joueur.
3. "Attack My Target" en visant un PNJ → toute l'escouade attaque, même avec Combat Response Off ; en visant un garde → refus (pas de tir ami).
4. "Cease Fire" en pleine fusillade → tous les gardes décrochent et rien ne les re-taske pendant ~5 s.
5. "Hold Position" sur un garde → il reste sur place en scénario garde quand le joueur s'éloigne ; "Follow Me" → il reprend le suivi ; version (All) idem pour l'escouade ; un garde en escorte ne peut pas passer en Hold.

### Jalon 4 — Voix + QoL
1. Entrée en combat → une réplique criée ; ordres → "yes" ; pas de spam (≥ 3 s entre répliques) ; toggle OFF → silence.
2. Changer Default Health/Armor/Godmode, Auto-Arm, Spawn Weapon, Weapon (Arm All), Formation Spacing → redémarrer → tout est conservé.
3. Bodyguard List : chaque ligne montre `[Rôle] PV/Max` ; garde mort → `[MORT]`.
4. Formation Spacing 8 m → les gardes marchent nettement plus écartés ; véhicule d'escorte spawné → blip voiture bleu sur la minimap, disparaît au cleanup.

### Jalon 5 — Hélico d'escorte
1. "Spawn Escort Helicopter" avec 3 gardes libres → hélico en vol, pilote + passagers gardes, blip.
2. Rouler en ville → l'hélico suit à ~40 m d'altitude sans se poser ; joueur immobile 15 s à pied → il atterrit à proximité ; repartir → il redécolle.
3. Tuer le pilote (godmode OFF) → promotion d'un passager ou crash propre + cleanup ; "Clear Escort Assignments" → gardes rendus au groupe, hélico supprimé une fois vide.
4. Convoi mixte sol + hélico simultanés → aucun re-task croisé.

---

## Estimations récapitulatives

| Jalon | Contenu | Taille | Nouveaux fichiers |
|---|---|---|---|
| 1 | HUD bas-droite (headshots + barres + arme) | **moyen** | `BodyguardHud.h/.cpp` (premake) |
| 2 | Mode chauffeur sol + hélico | **gros** | `BodyguardChauffeur.h/.cpp` (premake) + enum `BODYGUARD_CHAUFFEUR` |
| 3 | Medic fonctionnel + Attack/Cease Fire/Hold | **gros** (scindable en 3a/3b) | aucun |
| 4 | Voix + persistance spawn + liste enrichie + spacing + blips escorte | **moyen** | aucun |
| 5 | Escorte héliportée | **moyen/gros** | aucun |
