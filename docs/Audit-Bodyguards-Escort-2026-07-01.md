# Audit Bodyguards / Escort — 2026-07-01

Analyse en lecture seule (aucune modification de code) demandée suite à trois symptômes observés en jeu :

1. Le menu Bodyguards est mal agencé.
2. Les gardes du corps ont du mal à monter dans le véhicule d'escorte (boucle d'ouverture de porte).
3. Un seul garde reste dans le véhicule, les autres courent dehors.
4. Spawner deux escouades produit « deux escouades différentes » au lieu d'une seule.

## Trace de l'audit (ce qui a été fait)

| Étape | Action | Fichier(s) |
|-------|--------|-----------|
| 1 | Lecture de la doc de référence v2 | `docs/Bodyguards.md`, `docs/bodyguard_dynamic_workflow_context.md` |
| 2 | Lecture complète du module escorte | `Solution/source/Submenus/Bodyguards/BodyguardEscort.cpp` (.h) |
| 3 | Lecture du menu principal + presets v1 | `BodyguardMenu.cpp` |
| 4 | Lecture des escouades (défauts, XML, spawn) | `BodyguardSquads.cpp` |
| 5 | Lecture de la gestion (DB, delete, dismiss) | `BodyguardManagement.cpp` / `.h` |
| 6 | Lecture du tick global | `BodyguardTick.cpp` |
| 7 | Lecture du spawn (groupe GTA) | `BodyguardSpawn.cpp` |
| 8 | Lecture liste/fiche garde | `BodyguardSettings.cpp`, `BodyguardSubmenu.cpp` |
| 9 | Lecture partielle du combat (interférences) | `BodyguardCombat.cpp` |
| 10 | Vérif signature `AddTexter` | `Solution/source/Menu/Menu.h:253` |
| 11 | Vérif absence de `REMOVE_PED_FROM_GROUP` dans le module (le natif existe : `natives.h:4116`) | grep sur `Submenus/Bodyguards/` |

Aucun test en jeu n'a été lancé (consigne : ne pas lancer GTA). Les causes ci-dessous sont établies par lecture du code ; les points nécessitant une confirmation en jeu sont marqués « à confirmer ».

---

## Symptôme 2+3 — Escorte : porte en boucle, un seul garde reste à bord

### Cause racine n°1 : les gardes restent membres du groupe GTA du joueur pendant l'escorte

- Au spawn, chaque garde est ajouté au groupe du joueur : `SET_PED_AS_GROUP_MEMBER` (`BodyguardSpawn.cpp:103`) + `SET_PED_NEVER_LEAVES_GROUP(true)` (`:105`).
- Lors de l'affectation à l'escorte, `SeatBodyguard` (`BodyguardEscort.cpp:101-121`) fait seulement `SET_PED_NEVER_LEAVES_GROUP(false)` + `SET_PED_KEEP_TASK(true)` puis **téléporte** le ped dans le siège (`ForcePedIntoSeat`, warp — pas d'animation de porte).
- `REMOVE_PED_FROM_GROUP` n'est **jamais appelé** (vérifié par grep ; le natif existe dans `natives.h:4116`).

Conséquence : l'IA de groupe vanilla de GTA garde la main sur ces peds. Un membre de groupe assis dans un véhicule qui n'est **pas** celui du leader est automatiquement re-tâché « suivre le leader » → il sort du véhicule et court vers le joueur. `SET_PED_NEVER_LEAVES_GROUP(false)` ne désactive pas ce comportement (ça autorise seulement le ped à quitter le groupe à distance), et `SET_PED_KEEP_TASK` ne protège rien ici car les passagers n'ont **aucune tâche script**.

### Pourquoi seul le conducteur reste

`TickEscort` (`BodyguardEscort.cpp:259-315`, appelé toutes les ~750 ms par `BodyguardTick.cpp:33-38`) :

- `if (bg.EscortSeat != -1) continue;` (`:291-292`) → **seul le siège -1 (conducteur) reçoit une tâche** (`TASK_VEHICLE_ESCORT` / `TASK_VEHICLE_FOLLOW`), ré-émise à chaque tick, ce qui écrase l'IA de groupe.
- Les passagers, sans tâche, sont éjectés par l'IA de groupe ; au tick suivant `!IsPedInVehicle` → `ClearEscortState(bg)` (`:285-289`) qui **entérine** la désaffectation.

Résultat exact du symptôme : le conducteur roule, les autres courent derrière le joueur.

### Pourquoi la boucle d'ouverture de porte

Le code du mod ne fait que des warps (pas d'animation). La boucle de porte observée vient de l'IA de groupe vanilla : quand le joueur est dans un véhicule, les membres du groupe tentent d'y entrer d'eux-mêmes ; si les sièges sont pris/contestés (7 gardes max pour ≤3 places passager), ils saisissent la poignée en boucle. Les affectations/éjections répétées ci-dessus entretiennent ce cycle. **À confirmer en jeu** : la boucle se produit sur le véhicule du joueur ou sur celui d'escorte.

Facteur aggravant : `g_combatResponseMode` vaut **4 (Full Protection) par défaut dans le code** (`BodyguardCombat.cpp:19`) alors que `docs/Bodyguards.md` §5 annonce 1. `TASK_COMBAT_PED` sur un passager le fait descendre du véhicule → casse l'escorte dès qu'une cible/menace apparaît.

### Bugs secondaires dans `BodyguardEscort.cpp`

| Bug | Localisation | Effet |
|-----|--------------|-------|
| `else break;` : le remplissage s'arrête au **premier échec** de `SeatBodyguard` | `:187` (véhicule joueur), `:241` (véhicule d'escorte) | Un seul siège contesté (course avec l'IA de groupe) et tous les gardes restants ne sont plus affectés, silencieusement → « un seul rentre » |
| Off-by-one sur les sièges : `seat <= maxPassengers` au lieu de `< maxPassengers` | `:178`, `:232` (aussi `BodyguardCombat.cpp:66,88`) | Bénin (siège inexistant → jamais libre), mais faux |
| `TASK_VEHICLE_ESCORT` ré-émis toutes les 750 ms sans condition | `:304-313` (`DriverTasked` ne gate que la libération du groupe, pas la tâche) | Redémarrage permanent de la tâche de conduite → à-coups/freinages du conducteur (le module combat, lui, ne re-tâche que sur changement de cible) |
| Émptiness check cassé à la suppression du véhicule | `IsAnyBodyguardStillInVehicle` `:125-137` + `ClearEscortState` `:335-350` | Le test repose sur le champ `EscortVehicle` (déjà remis à 0 pour les gardes précédemment « clear ») et non sur l'occupation physique → « Clear Escort Assignments » peut supprimer un véhicule **encore occupé** |
| Suppression possible d'un véhicule personnel spawné par Menyoo | `ClearEscortState:335-349` | Le garde-fou « véhicule du joueur » exige que le joueur soit **assis dedans** au moment du clear ; un véhicule perso spawné via Menyoo est une mission entity → supprimable si le joueur est à pied à côté |
| Échec du conducteur non rattrapé | `:218-228` | Si `SeatBodyguard(…, -1)` échoue, aucun autre candidat n'est essayé → véhicule plein de passagers sans conducteur |
| Garde déjà assis via l'IA de groupe re-warpé | `:147-159` (gather) | Un garde assis dans la voiture du joueur sans passer par « Assign » est considéré « pending » et peut être téléporté vers un autre siège |

## Symptôme 4 — « Deux escouades au lieu d'une »

Côté moteur, il n'y a **qu'un seul groupe** : tous les gardes rejoignent `GET_PLAYER_GROUP` (`BodyguardSpawn.cpp:103`). « L'escouade » n'existe qu'en tant que **modèle de spawn** (`SquadDef`) ; il n'y a aucun concept d'escouade au runtime. La séparation perçue vient de l'escorte :

1. **Pas de réutilisation du véhicule d'escorte existant** : à chaque « Assign Bodyguards To Escort », s'il reste des gardes non assis et que « Spawn If Full » est actif, un **nouveau** véhicule est créé (`BodyguardEscort.cpp:193-251`). Les sièges libres d'un véhicule d'escorte déjà spawné ne sont jamais considérés. Scénario type : escouade A (3 gardes) → Assign → voiture 1 (1 siège libre). Escouade B (3 gardes) → Assign → **voiture 2** au lieu de compléter la voiture 1. D'où « deux escouades ».
2. Le remplissage « sièges du joueur d'abord » peut aussi scinder un même lot entre la voiture du joueur et une voiture d'escorte.
3. Accessoirement, la Bodyguard List nomme chaque garde d'après son escouade d'origine (`ent.Name = def.name`, `BodyguardSquads.cpp:231`), ce qui renforce visuellement la séparation.

## Symptôme 1 — Agencement du menu

### Menu principal `BodyguardMainMenu` (`BodyguardMenu.cpp:456-641`)

- **9 liens de sous-menus puis ~10 réglages en vrac**, sans aucun `AddBreak` séparateur (alors que `BodyguardSpawn` en utilise). Réglages de spawn (Health/Armor/Godmode), réglages d'affichage (Blip, Role Blips), réglages de comportement (Combat Response, Formation) et une action (« Bring Bodyguards To Self ») sont entremêlés.
- **Libellés incohérents** entre le lien et le titre du sous-menu : « Squad Equipment » → titre « Squad Tools » ; « Squad Recovery » → « Squad Maintenance » ; « Squad Cleanup » → « Manage Squad ».
- **4 micro-sous-menus « Squad * »** de 2 à 4 items chacun (Tools/Presets/Recovery/Cleanup) qui pourraient tenir dans un seul sous-menu.
- **Doublon conceptuel** : « Squad Presets » (v1, presets codés en dur Police/Military/Gang/FIB/Heavy qui modifient stats/armes, `BodyguardMenu.cpp:241-248`) vs « Squads » (v2, escouades XML Military/Police/FIB/… qui spawnent des peds). Mêmes noms, comportements différents → source de confusion majeure.
- « Wanted Level » concerne le joueur, pas les gardes — sa place dans ce menu est discutable.

### Menu Escort (`BodyguardEscort.cpp:368-462`)

- Les **deux actions principales** (« Assign », « Clear ») sont en **bas**, après 5 réglages ; pas de séparateur, pas d'affichage d'état (combien de gardes assignés, quel véhicule).
- Aucune remontée d'échec partiel : si le remplissage s'arrête (cf. `else break`), le toast « Gardes du corps affectés » s'affiche quand même dès qu'un seul garde a été assis (`:253-256`).

### Menu Squad Edit (`BodyguardSquads.cpp:294-404`)

- Chaque membre = 4 lignes plates (modèle, Role, Count, Remove Member) qui se répètent : avec 4 membres, 16 lignes où on ne sait plus quel « Role » appartient à quel modèle. Un sous-menu par membre serait plus lisible.

---

## Recommandations (non appliquées — audit seul)

1. **Retirer les gardes du groupe joueur pendant l'escorte** (`REMOVE_PED_FROM_GROUP`, natif dispo `natives.h:4116`) à l'affectation, et les re-ajouter dans `ClearEscortState`. C'est la correction structurante des symptômes 2 et 3 (passagers éjectés, portes en boucle).
2. **Ne re-tâcher le conducteur que sur changement d'état** (joueur monte/descend, cible change), comme le fait déjà le module combat avec `s_lastTarget`.
3. **Remplacer `else break;` par `continue`** (tenter le garde suivant) et remonter un décompte réel « X/Y assignés ».
4. **Réutiliser le véhicule d'escorte existant** (sièges libres) avant d'en spawner un nouveau ; supporter plusieurs véhicules seulement quand le premier est plein → répond au symptôme 4.
5. **Baser l'emptiness check de suppression sur l'occupation physique** du véhicule, et ne jamais supprimer un véhicule que le mod n'a pas spawné (mémoriser le handle du véhicule d'escorte spawné plutôt que tester `MissionEntity`).
6. Pendant l'escorte, **exempter les passagers du combat response** (ou le suspendre), sinon le mode 4 par défaut les fait descendre.
7. Menu : regrouper avec des `AddBreak` (Actions / Réglages), fusionner les 4 micro-menus « Squad * », renommer ou fusionner « Squad Presets » avec les Squads v2, aligner libellés de liens et titres.
8. Doc : corriger `docs/Bodyguards.md` §5 (défaut combat = 4 dans le code, pas 1).

## Points à confirmer en jeu

- Sur quel véhicule la boucle de porte se produit (joueur vs escorte) — n'affecte pas les corrections 1-4.
- Comportement exact de l'IA de groupe après `REMOVE_PED_FROM_GROUP` sur les blips/relations (les blips sont gérés par le mod, la relation `PLAYER` est posée au spawn, donc a priori sans impact).
