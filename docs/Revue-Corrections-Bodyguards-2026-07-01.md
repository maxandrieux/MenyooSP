# Revue et correctifs post-implémentation Bodyguards — 2026-07-01

Revue du travail décrit dans [Rapport-Implementation-Bodyguards-2026-07-01.md](Rapport-Implementation-Bodyguards-2026-07-01.md)
(implémentation du [plan](Plan-Correction-Bodyguards-2026-07-01.md) par un agent tiers), suivie de
correctifs, d'un rebuild et du déploiement dans le jeu.

## Verdict de la revue

L'implémentation est **conforme au plan** sur les points structurants : sortie du groupe joueur
pendant l'escorte (`REMOVE_PED_FROM_GROUP` + restauration), remplissage des sièges tolérant aux
échecs, convoi unique via `s_spawnedEscortVehicles`, suppression limitée aux véhicules du mod avec
test d'occupation physique, conducteur re-tâché seulement sur changement d'état, promotion d'un
passager, reboard après combat, menus fusionnés/sectionnés, Settings, Revive Dead, 3 nouvelles clés
INI, French.json valide (374 entrées, toutes les nouvelles clés présentes), enum ids ajoutés en fin
de bloc, registration par `unordered_map` (déplacement de `MAX_SUBS` sans risque).

## Problèmes trouvés et corrigés (6)

| # | Problème | Fichier | Correctif |
|---|---|---|---|
| 1 | **Détection de combat inopérante pour le reboard** : `IS_PED_IN_COMBAT(ped, playerPed)` teste « en combat contre le joueur », toujours faux pour un garde → tentative de remonter en véhicule en pleine fusillade | `BodyguardEscort.cpp` (`QueueReboardOrClear`) | Nouveau `IsPedBusyFighting()` : `IS_PED_IN_COMBAT(ped, 0) \|\| IS_PED_SHOOTING \|\| IS_PED_IN_MELEE_COMBAT` |
| 2 | **`Clear Escort Assignments` éjecte aussi du véhicule du joueur** (`TASK_LEAVE_VEHICLE` sans distinction) → gardes qui sautent de votre voiture en marche | `BodyguardEscort.cpp` (`ClearEscortState`) | Sortie forcée uniquement si `IsSpawnedEscortVehicle(veh)` ; dans la voiture du joueur, le garde reste assis (re-groupé, toléré par l'IA de groupe) |
| 3 | **Fuite du véhicule d'escorte orphelin** : `TickBodyguards` sort immédiatement quand `BodyguardDb` est vide → après « Dismiss All » ou suppression du dernier garde, le véhicule vide n'est jamais supprimé | `BodyguardEscort.{h,cpp}`, `BodyguardTick.cpp`, `BodyguardManagement.cpp` | Export de `CleanupEscortVehicles()` ; appelée par le tick même à base vide (throttle 750 ms) et à la fin de `DismissAllBodyguards` |
| 4 | **Affichage du rôle cassé dans l'éditeur de membre** : `AddTexter("Role", roleIdx, { label })` avec tableau à 1 élément → `AddTexter` affiche l'index brut (« 2 » au lieu de « Sniper ») pour tout rôle ≠ 0 (comportement vérifié dans `Menu.cpp:1566-1573`) | `BodyguardSquads.cpp` (`BodyguardSquadMemberEdit`) | Index passé à 0 (convention tableau mono-élément) |
| 5 | **Régression : accents perdus** dans tous les nouveaux toasts (« Gardes affectes », « Modele de vehicule invalide », « Entree invalide », etc.) alors que la v2 affichait le français accentué | `BodyguardEscort.cpp`, `BodyguardMenu.cpp`, `BodyguardSettings.cpp`, `BodyguardSquads.cpp` | 16 chaînes réaccentuées (réécriture UTF-8, cohérente avec les chaînes existantes du module) |
| 6 | **Code mort** : les 3 anciens sous-menus (`Squad Maintenance` / `Manage Squad` / `Squad Presets`) restaient définis et enregistrés, avec le placeholder « Revive Dead (coming soon) » ; et `Revive Dead` ne sortait pas les peds de l'état ragdoll | `BodyguardMenu.{h,cpp}` | Fonctions + registrations supprimées (ids d'enum conservés, jamais réordonnés) ; `CLEAR_PED_TASKS_IMMEDIATELY` ajouté après `RESURRECT_PED` |

## Points vérifiés sans correctif nécessaire

- `submenu_enum.h` : `MAX_SUBS` déplacé en fin d'enum — vérifié sans danger (registration par map ;
  seul usage de borne : garde de désambiguïsation dans `Teleport_Submenus.cpp:60`, rendue plus
  correcte par le déplacement).
- `Menu.cpp` : `SetSub_delayed` initialisé à `-1` au lieu de `0` — correct (0 est un id valide ;
  la sentinelle testée ligne 828 est `-1`).
- `BodyguardConfig.cpp` : les 3 nouvelles clés lues/écrites, aucun handle persisté.
- Combat : les gardes escortés ne reçoivent plus `TASK_COMBAT_PED` à pied (branche véhicule
  conservée) — conforme.
- Auto-assign : déclenché seulement si un convoi existe (`HasActiveEscortConvoy`) — conforme.
- French.json : parse OK, toutes les nouvelles entrées présentes.

## Build et déploiement

- Build : MSBuild `Menyoo.sln` Release x64 → **succès**, artefact
  `Solution/source/_Build/bin/Release/Menyoo.asi` (4 663 296 octets, 2026-07-01 22:47).
- GTA V vérifié **non lancé** avant copie.
- Déploiement dans `D:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced` (autorisé
  explicitement par l'utilisateur ce jour) :
  - ancien `Menyoo.asi` sauvegardé en `Menyoo.asi.bak` ;
  - nouveau `Menyoo.asi` copié ;
  - `menyooStuff\Language\French.json` copié.

## Tests en jeu à faire (utilisateur)

Reprendre la checklist du rapport d'implémentation (§ « Checklist de test en jeu recommandée »),
avec une attention particulière aux points touchés par les correctifs :

1. Reboard : faire descendre un garde pendant un combat → il ne doit tenter de remonter
   qu'une fois le combat terminé.
2. `Clear Escort Assignments` pendant que des gardes sont dans VOTRE voiture → ils restent
   assis (pas de saut en marche) ; ceux du véhicule d'escorte descendent, puis le véhicule
   est supprimé une fois vide.
3. `Dismiss All Bodyguards` avec un convoi actif → le véhicule d'escorte disparaît aussi.
4. Squads → escouade → membre : le rôle affiche bien son libellé (pas un chiffre).
5. Toasts en français accentué.
6. Revive Dead : les gardes se relèvent immédiatement.

En cas de problème : restaurer `Menyoo.asi.bak` (jeu fermé).
