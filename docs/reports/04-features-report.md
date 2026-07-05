# Rapport d'implémentation - Plan 04 Features Bodyguards

Date: 2026-07-05

## 1. Résumé étape par étape

1. Lecture préalable effectuée dans l'ordre demandé: `docs/LESSONS.md`, `docs/Bodyguards.md`, puis `docs/plans/04-features.md` relu par tranches après une sortie tronquée.
2. Jalon 1 - HUD Bodyguards: ajout de `BodyguardHud.h/.cpp`, rendu bas-droite par frame, cache de headshots non persisté, barres vie/armure, arme courante, toggle Settings et libération des slots au toggle OFF / Dismiss All.
3. Jalon 2 - Chauffeur: ajout de `BodyguardChauffeur.h/.cpp`, enum `BODYGUARD_CHAUFFEUR` ajouté en fin de plage Bodyguard, menu Chauffeur, conduite waypoint/libre, support véhicule joueur ou véhicule spawn, support hélico, stop propre et exclusions escorte/combat.
4. Jalon 3 - Medic + tactique: extraction `ReviveOneBodyguard`, tick medic heal/revive, options Medic, commandes Attack My Target / Cease Fire / Hold Position et exclusions busy.
5. Jalon 4 - Voix + QoL: voix contextuelles throttled, persistance des réglages de spawn, état rôle/PV dans Bodyguard List, formation spacing, blips sur véhicules d'escorte et véhicules chauffeur spawnés.
6. Jalon 5 - Hélico d'escorte: modèle persistant `escort_heli_model`, bouton Spawn Escort Helicopter, seating pilote/passagers via les règles escortes, suivi aérien `TASK_HELI_MISSION`, landing de courtoisie et cleanup via la liste des véhicules d'escorte.
7. Premake lancé via `.\generate.bat` après création de nouveaux `.cpp/.h`.
8. `docs/Bodyguards.md`, `docs/LESSONS.md` et `French.json` mis à jour.

## 2. Écarts par rapport au plan

- Le wrapper local n'a pas `TASK_VEHICLE_WANDER_STANDARD`; le mode Chauffeur libre utilise `TASK::TASK_VEHICLE_DRIVE_WANDER`.
- Pas de clé `escort_heli_enabled`: conformément à la recommandation du plan, le bouton manuel suffit; seule `escort_heli_model` est persistée.
- Le cleanup Chauffeur supprime seulement les véhicules spawnés par Chauffeur quand ils deviennent physiquement vides; s'ils restent occupés, ils sont laissés en place pour respecter la règle de sécurité.
- Aucune copie vers `D:\SteamLibrary\...` et aucun lancement GTA.

## 3. Fichiers modifiés/créés

Créés: `BodyguardHud.h/.cpp`, `BodyguardChauffeur.h/.cpp`, `docs/reports/04-features-report.md`.

Modifiés principaux: `BodyguardTick.cpp/.h`, `BodyguardEscort.cpp/.h`, `BodyguardCombat.cpp/.h`, `BodyguardConfig.cpp`, `BodyguardMenu.cpp/.h`, `BodyguardSettings.cpp`, `BodyguardSubmenu.cpp`, `BodyguardManagement.cpp/.h`, `submenu_enum.h`, `French.json`, `docs/Bodyguards.md`, `docs/LESSONS.md`.

Générés par Premake: `Solution/Menyoo.vcxproj`, `Solution/Menyoo.vcxproj.filters`.

## 4. Nouvelles clés

Config `[bodyguards]`: `hud_enabled`, `chauffeur_vehicle_model`, `chauffeur_warp_player`, `medic_enabled`, `medic_heal_player`, `voice_lines_enabled`, `default_health`, `default_armor`, `default_godmode`, `auto_arm_new`, `spawn_weapon_index`, `arm_weapon_index`, `formation_spacing`, `escort_heli_model`.

French.json ajouté: `Bodyguard HUD`, `--- HUD ---`, `Chauffeur`, `Chauffeur: `, `inactive`, `Drive Me To Waypoint`, `Drive Me Around`, `Stop Chauffeur`, `Warp Into Vehicle`, `Chauffeur Vehicle Model`, `Same As Escort`, `Driving Style: shared with Escort`, `No available bodyguard`, `No waypoint; driving around`, `Arrived`, `Chauffeur stopped`, `Medic Revives/Heals`, `Medic Heals Player`, `Attack My Target`, `Cease Fire`, `Hold Position`, `Follow Me`, `Hold Positions (All)`, `Follow Me (All)`, `No target`, `Voice Lines`, `Formation Spacing`, `[DEAD]`, `Spawn Escort Helicopter`, `Escort Helicopter Model`, `Escort guards cannot hold position`.

Validation JSON: OK, 0 doublon détecté.

## 5. Résultat exact du build

Commande finale:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Solution\Menyoo.sln -t:Build -p:Configuration=Release -p:Platform=x64 -m -nologo
```

Résultat final:

```text
La génération a réussi.
    0 Avertissement(s)
    0 Erreur(s)

Temps écoulé 00:01:21.03
```

Sortie: `Solution/source/_Build/bin/Release/Menyoo.asi`.

## 6. Points d'attention pour relecture

- Fonctionnalités gameplay non testées en jeu par consigne; validation faite par compilation uniquement.
- Les comportements IA hélico/chauffeur peuvent dépendre des limites natives GTA (`TASK_HELI_MISSION`, landing), à vérifier en smoke test.
- `docs/LESSONS.md` contient les pièges rencontrés pendant cette implémentation; il n'y a donc pas de mention "aucune erreur".

## Revue Claude (2026-07-05)

**Verdict : implémentation complète et fidèle sur les 5 jalons, mais 2 bugs majeurs d'interaction croisée et 7 défauts de robustesse trouvés — tous corrigés. Build vert après corrections.**

### Complétude jalon par jalon

- **Jalon 1 — HUD** : complet. Rendu par frame (appelé en tête de `TickBodyguards`), cache headshots throttlé 500 ms, libération sur ped absent de la DB / toggle OFF / Dismiss All, avatar via `REGISTER_PEDHEADSHOT`/`IS_PEDHEADSHOT_VALID+READY`/`GET_PEDHEADSHOT_TXD_STRING`/`DRAW_SPRITE`, barres vie/armure, arme via `GET_CURRENT_PED_WEAPON`+`GetWeaponLabel`, garde mort grisé/rouge, garde cutscene/pause, toggle `hud_enabled` persisté + section `--- HUD ---` dans Settings.
- **Jalon 2 — Chauffeur** : complet à une exception près : le « re-task filet » (plan 2.4 §5, `GET_SCRIPT_TASK_STATUS`) manquait — ajouté à la relecture (voir corrections). Le timeout ~20 s « hover + toast » de l'atterrissage hélico (plan, section Risques jalon 2) n'est **pas** implémenté (listé, non bloquant : le stop implicite et le stop manuel couvrent le cas). Le helper commun `SpawnTrackedBodyguardVehicle` proposé en option n'a pas été extrait (duplication spawn escorte/chauffeur, acceptable).
- **Jalon 3 — Medic + tactique** : complet. `ReviveOneBodyguard` extrait, `TickMedic` (patient mort/blessé < 40 %, medic < 60 m, GO_TO + anim CPR préchargée + timeout 20 s, revive via la logique existante, option joueur < 50 %), `Attack My Target`/`Cease Fire`/`Hold Position` (global + par garde), champ `HoldPosition` ajouté en fin de bloc v2, Hold refusé si escorte.
- **Jalon 4 — Voix + QoL** : complet. Répliques (entrée en combat, ordres, mort d'un garde) avec throttle 3 s + toggle persisté ; persistance `default_health/armor/godmode`, `auto_arm_new`, `spawn_weapon_index`, `arm_weapon_index` (avec clamps) ; Bodyguard List `[Rôle] PV/Max` + `[DEAD]` ; `formation_spacing` (menu + pending au chargement) ; blips sur véhicules d'escorte et véhicule chauffeur.
- **Jalon 5 — Escorte héliportée** : complet. `Spawn Escort Helicopter` (pilote Driver préféré, passagers, véhicule tracké donc cleanup/godmode/promotion hérités), branche aérienne de `TickEscort` (mission 8), reboard-warp direct en vol, atterrissage de courtoisie (déclenché à 8 s d'immobilité au lieu des 15 s du plan — seuil partagé avec la logique sol existante, acceptable), `escort_heli_model` persisté.
- **Intégration build** : `Solution/Menyoo.vcxproj` contient bien `BodyguardHud.cpp/.h` et `BodyguardChauffeur.cpp/.h` ; recompilation forcée vérifiée (timestamps des `.obj`).
- **French.json** : JSON valide, 0 doublon, toutes les clés nouvelles présentes ; aucune chaîne accentuée en dur dans les fichiers Bodyguards.
- **Invariants** : respectés (spawn via `SpawnBodyguardPed` inchangé, aucun handle persisté, `BODYGUARD_CHAUFFEUR` ajouté en fin de plage, aucune native dans `ReadBodyguardConfig` — formation/spacing différés via pending).

### Problèmes trouvés (par sévérité) et corrections appliquées

1. **MAJEUR — Tir à la corde escorte vs Hold/Medic** (`BodyguardEscort.cpp`, boucle `TickEscort`) : un garde Hold Position ou un Medic en soin est `RemovedFromGroup` sans `EscortVehicle` ; le nettoyage d'état orphelin de `TickEscort` le remettait dans le groupe vanilla toutes les 750 ms (`ClearEscortState` restaure le groupe), cassant Hold Position et le soin en boucle. **Corrigé** : exclusion `IsBusyBodyguard(ped) || bg.HoldPosition` en tête de boucle (l'exclusion chauffeur seule existait mais était insuffisante).
2. **MAJEUR — `CeaseFireAll` volait la tâche du chauffeur/medic** (`BodyguardCombat.cpp`, `CeaseFireAll`) : `CLEAR_PED_TASKS` sur TOUS les gardes vivants, y compris le chauffeur en conduite (tâche de conduite perdue, aucune ré-émission) et le medic en soin. **Corrigé** : les gardes `IsBusyBodyguard` sont sautés (leurs flags combat sont quand même remis à zéro).
3. **MOYEN — Catch-up teleport pouvait plaquer un hélico d'escorte au sol** (`BodyguardEscort.cpp`, warp véhicules) : un hélico > 150 m hors écran était téléporté sur un nœud routier (tunnel possible) + `SET_VEHICLE_ON_GROUND_PROPERLY`. **Corrigé** : hélicos exclus du warp véhicule (leur mission Follow les ramène en vol).
4. **MOYEN — Ré-émission de `TASK_HELI_MISSION` toutes les 750 ms** (`BodyguardEscort.cpp`, branche aérienne de `TickEscort`) : le cache `IsDriverTaskCurrent` était contourné pour les hélicos (bégaiement IA — l'anti-pattern corrigé au plan 02). **Corrigé** : cache réutilisé pour le suivi ; l'atterrissage de courtoisie est mis en cache avec la sentinelle `targetVeh == veh` (jamais une clé de suivi légitime), chaque transition re-taske exactement une fois.
5. **MOYEN — Chauffeur : filet de ré-émission manquant** (plan 2.4 §5, `BodyguardChauffeur.cpp`, `TickChauffeur`) : si la tâche de conduite était perdue (événement externe), rien ne la ré-émettait (le tick ne re-taskait que sur waypoint déplacé). **Ajouté** : `GET_SCRIPT_TASK_STATUS == 7` sur la tâche attendue (`SCRIPT_TASK_VEHICLE_DRIVE_TO_COORD_LONGRANGE` / `SCRIPT_TASK_VEHICLE_DRIVE_WANDER` / `SCRIPT_TASK_HELI_MISSION`) déclenche `TaskDrive()`.
6. **MOYEN — Chauffeur hélico : mission d'atterrissage ré-émise toutes les 500 ms** (`BodyguardChauffeur.cpp`, arrivée waypoint) : ré-émettre LandAtCoords à chaque tick relance l'approche et peut empêcher de se poser. **Corrigé** : émission unique (`s_heliLandingIssued`), ré-armée à chaque nouveau `TaskDrive`.
7. **PETIT — `SET_PED_INTO_VEHICLE` avec siège `-2`** (`BodyguardChauffeur.cpp`, warp passager) : `-2` (« n'importe quel siège ») est une convention de `TASK_ENTER_VEHICLE`, pas de `SET_PED_INTO_VEHICLE` ; le warp échouait si le siège 0 était occupé. **Corrigé** : helper `FirstFreePassengerSeat` ; véhicule plein au démarrage → toast `No free escort seats` + abandon propre (avant toute mutation d'état).
8. **PETIT — Hold Position : scénario relancé toutes les 2 s** (`BodyguardTick.cpp`, `TickHoldPositions`) : `TASK_START_SCENARIO_IN_PLACE` ré-émis en boucle (redémarrage d'animation). **Corrigé** : skip si `IS_PED_USING_ANY_SCENARIO`.
9. **PETIT — HUD : avatar clignotant à 7 gardes** (`BodyguardHud.cpp`, refresh 10 s) : le re-register se faisait AVANT la libération du vieux slot ; à `MAX_BODYGUARDS` le cap de taille refusait l'enregistrement et l'avatar disparaissait un tick. **Corrigé** : libération d'abord, ré-enregistrement ensuite. Aussi exclusion Hold/busy du catch-up à pied (`TickEscortCatchupOnFoot`) : une sentinelle éloignée ne doit jamais être télé-transportée vers le joueur.

### Non-régression plans 01/02/03

Vérifiée sur pièces : revive via `IS_PED_DEAD_OR_DYING`, `ClearEscortState` sûr sur handle mort, grâce 12 s + `deleteWhenEmpty`, relâchement `CombatTasked`, `combat_wanted_min_stars`, sortie des passagers abandonnés, cascade `+6/-8*i`, Drivers réservés (`stable_partition`), catch-up (à pied + véhicule), realistic boarding, anti-bégaiement (`LastCombatTarget`, `IsDriverTaskCurrent`), arbitre de cibles (`ProposeSquadTarget`), compteur x/7, Delete/Heal/Bring individuels, Wanted dans Player Options — tous intacts.

### Build

`MSBuild Release x64` : succès, 0 erreur (recompilation effective de `BodyguardHud.cpp`, `BodyguardChauffeur.cpp`, `BodyguardCombat.cpp`, `BodyguardTick.cpp`, `BodyguardEscort.cpp` vérifiée sur les timestamps des `.obj`). Aucun test en jeu (consigne).

### À surveiller en jeu

- Atterrissage hélico (chauffeur et escorte) : pas de timeout hover 20 s ; si `TASK_HELI_MISSION(20)` ne trouve pas de zone plate, l'hélico peut chercher longtemps.
- Convention `GET_SCRIPT_TASK_STATUS` (7 = tâche absente) : standard mais à confirmer en jeu sur le filet chauffeur.
- HUD bas-droite vs HUD vanilla (position/échelle à ajuster visuellement) ; collision possible si `menuPos` est déplacé en bas à droite (assumé par le plan).
- `Cease Fire` ne suspend le combat que 5 s (`s_combatSuppressedUntil`) : en Full Protection, les gardes peuvent se ré-engager après.
