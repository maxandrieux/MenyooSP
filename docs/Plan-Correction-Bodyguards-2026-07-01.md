# Plan de correction Bodyguards — 2026-07-01

Plan d'implémentation complet (aucune ligne de code modifiée à ce stade). Fondé sur l'audit
[Audit-Bodyguards-Escort-2026-07-01.md](Audit-Bodyguards-Escort-2026-07-01.md) et la référence
[Bodyguards.md](Bodyguards.md).

## Objectif

Corriger l'ensemble de la fonctionnalité Bodyguards :

- **Escorte fiable** : tous les gardes montent, restent à bord, un seul « convoi » quelle que soit
  l'origine des gardes (une ou plusieurs escouades spawées).
- **Menu lisible** : actions séparées des réglages, sous-menus fusionnés, libellés cohérents.
- **Robustesse** : plus de suppression de véhicule occupé ni de véhicule personnel, interactions
  combat/escorte assainies.
- **Cohérence doc + traductions**.

## Vérifications préalables effectuées (trace)

| Vérification | Résultat |
|---|---|
| `submenu_enum.h:286-301` : ids bodyguard existants | `BODYGUARD_SETTINGS` existe déjà (placeholder vide `BodyguardOps_`, `BodyguardSettings.cpp:100-103`) → réutilisable pour le sous-menu Réglages ; tout nouvel id sera **ajouté en fin de bloc**, jamais inséré au milieu |
| `MenuConfig.cpp` : persistance d'ids de sous-menus | Aucune (pas de `lastSub`/`SUB::` persisté) → fusion/suppression de sous-menus sans risque de config corrompue |
| Natif `REMOVE_PED_FROM_GROUP` | Disponible (`natives.h:4116`), jamais utilisé dans le module |
| Nouveaux fichiers nécessaires | Aucun prévu → **pas de regen Premake**, MSBuild incrémental suffit |

---

## Vue d'ensemble des phases

| Phase | Contenu | Taille | Dépend de |
|---|---|---|---|
| 1 | Cœur escorte : sortie du groupe, remplissage des sièges, réutilisation du véhicule (convoi unique) | L | — |
| 2 | Robustesse : suppression sûre des véhicules, interaction combat/escorte, conducteur sans à-coups | M | 1 |
| 3 | Refonte des menus (principal, Escort, Squad Tools fusionné, Squad Edit) | M | — (parallélisable) |
| 4 | Qualité de vie escorte/escouades (options, promotion conducteur, ré-affectation auto) | M | 1, 2 |
| 5 | i18n (French.json) + persistance des nouvelles options | S | 3, 4 |
| 6 | Docs, build final, checklist de test en jeu | S | tout |

Chaque phase se termine par un build Release x64 qui compile proprement ; les phases 1-2 sont
testables en jeu indépendamment de la 3.

---

## Phase 1 — Cœur escorte (corrige : porte en boucle, un seul garde à bord, deux escouades)

### 1.1 Sortir les gardes du groupe joueur pendant l'escorte

Fichiers : `BodyguardEscort.cpp`, `BodyguardManagement.h`.

- Ajouter à `BodyguardEntity` un champ runtime `bool RemovedFromGroup{false}` (jamais persisté,
  comme les autres champs v2).
- Dans `SeatBodyguard` (`BodyguardEscort.cpp:101`) : avant le warp, `REMOVE_PED_FROM_GROUP(ped)`
  + `RemovedFromGroup = true`. Conserver `SET_PED_KEEP_TASK(true)`.
- Dans `ClearEscortState` (`:317`) : si `RemovedFromGroup`, ré-ajouter au groupe
  (`SET_PED_AS_GROUP_MEMBER(ped, GET_PLAYER_GROUP(PLAYER_ID()))`) puis restaurer
  `SET_PED_NEVER_LEAVES_GROUP(ped, bg.WasNeverLeavesGroup)` et remettre le flag à false.
  Les chemins mort/suppression/dismiss passent déjà par `ClearEscortState`
  (`BodyguardManagement.cpp:130,180,209`) → couverts sans travail supplémentaire.
- Pourquoi : c'est l'IA de groupe vanilla qui éjecte les passagers d'un véhicule non-leader et
  déclenche les boucles de porte. La retirer du jeu pendant l'escorte est la correction racine.

### 1.2 Remplissage des sièges fiable

Fichier : `BodyguardEscort.cpp` (`AssignBodyguardsToEscort`).

- Remplacer les deux `else break;` (`:187`, `:241`) par `continue` → un échec sur un garde
  n'abandonne plus les suivants.
- Corriger l'off-by-one : boucles de sièges `seat < maxPassengers` (`:178`, `:232`).
- Échec du conducteur (`:218-228`) : si `SeatBodyguard(…, -1)` échoue, essayer le candidat
  suivant au lieu d'abandonner le siège conducteur.
- Gather (`:147-159`) : un garde déjà physiquement assis dans le véhicule du joueur (entré par
  l'IA de groupe) est **adopté** (on renseigne `EscortVehicle`/`EscortSeat` sur son siège actuel)
  au lieu d'être re-téléporté ailleurs.
- Toast honnête : « Gardes affectés : X/Y » (Y = pending initial), en rouge si X < Y.

### 1.3 Convoi unique : réutiliser le véhicule d'escorte existant

Fichier : `BodyguardEscort.cpp`.

- Ajouter une liste runtime `static std::vector<Vehicle> s_spawnedEscortVehicles` : tout véhicule
  créé par « Assign » y est ajouté ; purge des handles morts à chaque tick/assign.
- Nouvel ordre de remplissage dans `AssignBodyguardsToEscort` :
  1. sièges du véhicule du joueur (si option active) ;
  2. **sièges libres des véhicules d'escorte déjà spawnés** (nouveau) ;
  3. seulement s'il reste des gardes : spawn d'un nouveau véhicule (option « Spawn If Full »).
- Effet : spawner l'escouade B après l'escouade A puis ré-appuyer sur « Assign » complète la
  voiture existante → un seul convoi. Plusieurs véhicules uniquement quand le premier est plein.
- La liste sert aussi de source de vérité pour la suppression (phase 2.1) : on ne supprime que
  ce que le mod a créé.

### Critères d'acceptation phase 1

- 7 gardes + berline 4 places + « Spawn If Full » : 3 dans la voiture du joueur, 4 dans UNE
  voiture d'escorte ; personne ne descend spontanément ; plus de boucle de porte.
- Deux escouades de 3 spawnées successivement + 2 « Assign » : pas de second véhicule tant que
  le premier a des sièges libres.

---

## Phase 2 — Robustesse

### 2.1 Suppression de véhicule sûre

Fichier : `BodyguardEscort.cpp` (`ClearEscortState:335-350`, `IsAnyBodyguardStillInVehicle:125-137`).

- Ne supprimer que les véhicules présents dans `s_spawnedEscortVehicles` (fini l'heuristique
  `MissionEntity_get()` qui peut matcher la voiture personnelle spawnée via Menyoo).
- Test de vacuité **physique** : itérer les sièges (`GET_PED_IN_VEHICLE_SEAT`) au lieu de se fier
  aux champs `EscortVehicle` déjà nettoyés — corrige la suppression d'un véhicule encore occupé
  par « Clear Escort Assignments ».
- Option retenue : à « Clear », faire d'abord descendre proprement les gardes
  (`TASK_LEAVE_VEHICLE`) puis supprimer le véhicule une fois vide (au tick suivant), plutôt que
  de supprimer sous eux.

### 2.2 Conducteur sans à-coups

Fichier : `BodyguardEscort.cpp` (`TickEscort:259-315`).

- Mémoriser par garde l'état de la dernière tâche émise (joueur en véhicule ? handle du véhicule
  suivi). Ré-émettre `TASK_VEHICLE_ESCORT`/`TASK_VEHICLE_FOLLOW` **uniquement** quand cet état
  change ou quand la tâche est terminée (`GET_SCRIPT_TASK_STATUS`), sur le modèle de
  `s_lastTarget` dans `BodyguardCombat.cpp`. Corrige les freinages toutes les 750 ms.
- Conducteur mort/disparu : promouvoir un passager (le premier, préférence rôle Driver) au siège
  -1 et le tâcher — le convoi ne devient plus une voiture garée.

### 2.3 Interaction combat ↔ escorte

Fichier : `BodyguardCombat.cpp` (`TaskBodyguardOnTarget:158`, `TaskAllBodyguardsOnTarget:197`).

- Un garde avec `EscortVehicle` assigné ne reçoit **jamais** `TASK_COMBAT_PED` à pied (qui le
  fait descendre). Il garde les tâches véhicule (drive-by / `TASK_VEHICLE_SHOOT_AT_PED`), déjà
  gérées par la branche « in vehicle ». Concrètement : dans la branche à pied, skipper les gardes
  escortés.
- Trancher le défaut `g_combatResponseMode` : **conserver 4 (Full Protection)** et corriger
  `docs/Bodyguards.md` §5 qui annonce 1 (incohérence doc/code relevée à l'audit).

### Critères d'acceptation phase 2

- « Clear Escort Assignments » : gardes descendent, véhicule d'escorte supprimé une fois vide,
  voiture personnelle jamais touchée.
- Viser un piéton pendant l'escorte : les passagers tirent en drive-by, personne ne descend.
- Conduite d'escorte fluide (pas de stop-and-go périodique).

---

## Phase 3 — Refonte des menus

### 3.1 Menu principal (`BodyguardMenu.cpp:456-641`)

Structure cible (les `AddBreak` n'existent pas aujourd'hui dans ce menu) :

```
Bodyguards
├── Spawn Bodyguard
├── Bodyguard List
├── Squads
├── Escort Vehicle
├── Squad Tools            ← fusion des 4 micro-menus (3.2)
├── Settings               ← nouveau, réutilise l'id BODYGUARD_SETTINGS existant (3.3)
├── Wanted Level
├── --- Actions ---
└── Bring Bodyguards To Self
```

- Supprimer les liens « Squad Equipment / Squad Presets / Squad Recovery / Squad Cleanup ».
- Aligner libellé du lien et titre du sous-menu partout.

### 3.2 « Squad Tools » fusionné (id `BODYGUARD_SQUAD_TOOLS` conservé)

Fusion de `BodyguardSquadTools` + `BodyguardSquadMaintenance` + `BodyguardManageSquad` +
`BodyguardSquadPresets` en un seul sous-menu sectionné :

```
Squad Tools
├── --- Weapons ---
│   Weapon (texter) / Arm All Bodyguards / Auto-Arm New Bodyguards / Spawn Weapon
├── --- Health ---
│   Heal All / Refill Armor All
├── --- Equipment Presets ---      ← renommage de « Squad Presets » (lève l'ambiguïté avec Squads)
│   Preset / Apply To Current Squad / Use For New Bodyguards / Apply Preset
└── --- Cleanup ---
    Cleanup Dead Bodyguards / Dismiss All Bodyguards
```

- Les fonctions et registrations des 3 menus absorbés sont retirées ; leurs ids d'enum restent
  en place (inutilisés) pour ne pas décaler le bloc — nettoyage éventuel plus tard.
- Le placeholder « Revive Dead (coming soon) » : voir phase 4.3 (implémentation optionnelle) ;
  sinon le retirer plutôt que d'afficher un bouton mort.

### 3.3 Sous-menu « Settings » (réutilise `BODYGUARD_SETTINGS` / `BodyguardOps_`)

Déplace hors du menu principal tous les réglages :

```
Settings
├── --- Spawn Defaults ---
│   Default Health / Default Armor / Godmode / Default Spawn Role
├── --- Blips ---
│   Role Blips / Bodyguard Blip (legacy)
└── --- Behaviour ---
    Combat Response / Formation
```

### 3.4 Menu Escort (`BodyguardEscort.cpp:368-462`) : actions en tête + état

```
Escort Vehicle
├── (ligne d'état : « Assignés : 4/6 — police », non cliquable)
├── Assign Bodyguards To Escort
├── Clear Escort Assignments
├── --- Settings ---
├── Use My Seats First
├── Spawn Escort Vehicle If Full
├── Escort Vehicle Model / Custom Model...
└── Escort Driving Style
```

La ligne d'état se calcule depuis `BodyguardDb` (nb de gardes avec `EscortVehicle` valide) et
`s_spawnedEscortVehicles`.

### 3.5 Squad Edit (`BodyguardSquads.cpp:294-404`) : un sous-menu par membre

- Nouvel id `BODYGUARD_SQUAD_MEMBER_EDIT` (**ajouté en fin de bloc** dans `submenu_enum.h`).
- `BODYGUARD_SQUAD_EDIT` liste : Spawn Squad, puis une entrée par membre
  (`s_m_y_swat_01  (Heavy x2)`), puis Add Member... / Reset This Squad.
- Le sous-menu membre porte Role / Count / Remove Member. Fini les 4 lignes répétées par membre.
- « Save Squads » disparaît (chaque modification sauve déjà immédiatement — bouton redondant).

### Critères d'acceptation phase 3

- Menu principal ≤ 8 liens + 1 action ; plus aucun réglage en vrac à la racine.
- Chaque libellé de lien == titre du sous-menu ouvert.
- Navigation Squads : liste → escouade → membre, sans répétition ambiguë.

---

## Phase 4 — Qualité de vie (« voir large », options désactivables)

Toutes persistées dans `menyooConfig.ini [bodyguards]` (phase 5).

| # | Option / comportement | Détail | Défaut proposé |
|---|---|---|---|
| 4.1 | **Auto-assign new spawns** (toggle menu Escort) | Un garde spawné pendant qu'un convoi existe est automatiquement affecté au prochain siège libre | OFF |
| 4.2 | **Ré-affectation après combat** | Dans `TickEscort` : un garde dont l'`EscortVehicle` est encore valide mais qui est à pied ET hors combat (`IS_PED_IN_COMBAT` false) est re-tâché de remonter (`TASK_ENTER_VEHICLE` puis warp de secours après délai) au lieu d'être immédiatement « clear » (`:285-289`) | ON |
| 4.3 | **Revive Dead** (Squad Tools → Health) | Implémente le placeholder : `RESURRECT_PED` + restore santé/armure/rôle/blip + ré-entrée dans le groupe ; sinon retirer le bouton | à décider |
| 4.4 | **Escort vehicle godmode** (toggle menu Escort) | `SET_ENTITY_INVINCIBLE` sur les véhicules de `s_spawnedEscortVehicles`, aligné sur le godmode des gardes | OFF |

4.2 change la sémantique actuelle du tick (le « clear au premier constat hors véhicule » devient
« tentative de retour, clear après N échecs ») — c'est la correction UX la plus visible après la
phase 1.

---

## Phase 5 — i18n + persistance

- `French.json` : ajouter les nouveaux libellés (Settings, sections `---`, ligne d'état, options
  4.x, « Equipment Presets », etc.) ; retirer les libellés devenus orphelins. Règles existantes :
  jamais de noms propres/modèles/armes traduits ; valider le JSON avec un parseur avant build.
- `BodyguardConfig.cpp` : nouvelles clés `escort_auto_assign`, `escort_reboard_after_combat`,
  `escort_vehicle_godmode` (+ celles retenues en 4.x). Aucun handle runtime persisté (invariant
  v2 conservé).
- Toasts : conserver le français en dur actuel (cohérent avec l'existant du module).

## Phase 6 — Docs, build, tests

- Mettre à jour `docs/Bodyguards.md` : §4 (nouvel algorithme d'escorte + convoi + options), §5
  (défaut = 4), §9 (fichiers), §12 (nouvelle checklist de test).
- Build : MSBuild Release x64 incrémental (aucun nouveau fichier → pas de Premake). Vérifier
  zéro warning nouveau.
- Déploiement/test en jeu : par l'utilisateur (copie de `Menyoo.asi` + `French.json`, GTA fermé),
  suivant la checklist §12 mise à jour. Points de test clés :
  1. escorte 7 gardes / berline + « Spawn If Full » (critères phase 1) ;
  2. deux escouades → un convoi ;
  3. clear → descente propre, véhicule supprimé, voiture perso intacte ;
  4. combat pendant escorte → drive-by sans descente, retour à bord après combat (4.2) ;
  5. round-trip config/French.json.

---

## Risques et inconnues (à surveiller au premier test en jeu)

| Risque | Impact | Mitigation |
|---|---|---|
| Comportement des peds **hors groupe** assis sans tâche (sortie spontanée du siège par l'ambient AI) | Casserait la phase 1 | Fallback prévu : tâche passive (`TASK_STAND_STILL` longue) ou re-warp de garde dans `TickEscort` ; à activer seulement si constaté |
| Ré-ajout au groupe à « Clear » alors que le groupe est plein (7) | Garde orphelin qui ne suit plus | Vérifier `GET_GROUP_SIZE` ; MAX_BODYGUARDS=7 rend le cas théorique, à logguer via `dbg::Log` |
| `REMOVE_PED_FROM_GROUP` et relations : le garde ne doit pas devenir hostile/neutre | Combat friendly-fire | La relation `PLAYER` est posée au spawn (`BodyguardSpawn.cpp:104`) et `IsFriendlyToPlayer` teste aussi la DB → a priori couvert ; test en jeu dédié |
| Fusion de menus : régression de navigation (ids réutilisés) | Menu cassé | Ne jamais réordonner l'enum ; nouveaux ids en fin de bloc ; build + navigation complète après phase 3 |
| 4.2 (retour à bord) : oscillation entrer/sortir si combat mal détecté | Comportement erratique | Hystérésis : délai mini entre deux tentatives + compteur d'échecs avant clear |

## Décisions à valider (défauts proposés si pas d'avis)

1. **« Equipment Presets »** : renommage + fusion dans Squad Tools (proposé) — alternative :
   suppression pure du système v1, les Squads v2 le remplacent.
2. **Revive Dead (4.3)** : implémenter (proposé) ou retirer le placeholder.
3. **Défaut Combat Response** : garder 4 / Full Protection (proposé) ou revenir à 1.

## Ordre de livraison recommandé

1 → 2 (un seul lot testable « escorte fiable »), puis 3 (menu), puis 4+5 (options + i18n),
puis 6. Si besoin de réduire : la phase 4 est entièrement optionnelle, et 3.5 (Squad Edit) est
détachable du reste de la phase 3.
