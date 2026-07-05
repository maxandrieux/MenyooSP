# Rapport d'implémentation — Plan 2 Optimisations en jeu

Date : 2026-07-04  
Plan source : `docs/plans/02-optimisations.md`

## 1. Résumé étape par étape

### Lot 1 — Cascade de véhicules d'escorte + anti-empilement
- Ajout de `SpawnOneEscortVehicle`, qui crée un véhicule d'escorte mission entity, le déverrouille, applique le godmode courant, le pose au sol, démarre le moteur et l'enregistre dans `s_spawnedEscortVehicles`.
- La création est maintenant en cascade bornée : jusqu'à 4 véhicules par assignation, arrêt sur échec de spawn, et arrêt si aucun garde n'a pu être assis.
- Placement anti-empilement : premier véhicule devant le joueur, suivants derrière en file via l'offset `6 - 8 * convoyIndex`.
- Suppression explicite du véhicule précis si le modèle ne permet d'asseoir personne.

### Lot 2 — Véhicule détruit + changement de véhicule joueur
- Ajout d'un helper d'épave (`IS_ENTITY_DEAD(..., FALSE)` ou véhicule non driveable).
- Les gardes affectés à une épave sont libérés via `ClearEscortState`.
- La purge supprime les épaves spawnées par le module dès qu'aucun garde vivant ne leur est encore affecté.
- Détection du changement de véhicule joueur avec mémoire statique : les gardes restés affectés à l'ancien véhicule personnel sont invités à sortir puis libérés.
- Si `Auto-Assign New Spawns` est actif, une réassignation automatique est lancée après 2 secondes de stabilité du nouveau véhicule.

### Lot 3 — Drivers réservés
- Avant remplissage passager, les gardes au rôle `Driver` sont repoussés en fin de liste quand un véhicule d'escorte existe ou va être spawné.
- `FillDriverSeat` conserve ensuite sa préférence Driver pour choisir les volants.

### Lot 4 — Performance + bornes de sièges
- Scans combat Retaliate/Wanted limités à 128 peds au lieu du chemin par défaut à très grosse allocation.
- Godmode des véhicules d'escorte réappliqué une seule fois par tick quand le toggle change, au lieu de le réexécuter dans les boucles chaudes.
- Suppression du déverrouillage répété dans le chemin conducteur chaud.
- `TickShootOnTarget` est throttlé à 200 ms.
- Spawn d'escouade : l'auto-assign convoi est différé pendant les spawns puis exécuté une seule fois à la fin.
- Blip de spawn : seul le nouveau ped reçoit son blip, au lieu de rafraîchir toute la liste.
- Bornes de sièges combat corrigées en `< maxPass`.

### Lot 5 — Téléportation de rattrapage
- Nouveau réglage `g_escortCatchupTeleport`, défaut ON, persisté dans `menyooConfig.ini`.
- Véhicules du convoi distants de plus de 150 m, hors écran, avec conducteur bodyguard vivant : téléportation derrière le joueur sur un noeud routier si possible, sinon offset derrière le joueur.
- Anti-spam par véhicule : au plus un warp toutes les 5 secondes.
- Cache de tâche conducteur invalidé après warp.
- Ajout de `TickEscortCatchupOnFoot()` appelé dans le tick 750 ms : les gardes à pied, hors escorte, hors véhicule, hors combat, distants de plus de 150 m et hors écran sont ramenés près du joueur.

### Lot 6 — Embarquement réaliste optionnel
- Nouveau réglage `g_escortRealisticBoarding`, défaut OFF, persisté dans `menyooConfig.ini`.
- Si activé et si le garde est à moins de 25 m du véhicule, l'assignation initiale utilise `TASK_ENTER_VEHICLE`.
- Le filet existant de reboard/warp reste actif après 8 secondes puis plusieurs tentatives.

### Lot 7 — Anti-bégaiement combat
- Ajout des champs runtime `LastCombatTarget` et `LastCombatTaskAt` dans `BodyguardEntity` sans persistance.
- `TaskBodyguardOnTarget` n'émet plus une tâche si le garde combat déjà la même cible.
- Les trois sources de cible combat proposent maintenant une cible à un arbitre commun.
- Priorité : Player Target, puis Retaliate, puis Wanted Threats.
- `TickCombatResponse` émet au maximum une assignation de cible par frame.

## 2. Écarts par rapport au plan

- Les dépendances du plan 3 étaient déjà présentes dans le code actuel : `ClearEscortState` sur véhicule disparu, purge avec délai de grâce, et `CombatTasked`/relâchement de fin de combat. Je les ai réutilisées au lieu de les réimplémenter.
- `s_spawnedEscortVehicles` n'est pas un `std::vector<Vehicle>` mais un vecteur de `SpawnedEscortVehicle { veh, emptySinceMs }`. Les suppressions ciblées utilisent donc `remove_if` sur `rec.veh`.
- La purge d'épave ne supprime pas un véhicule tant qu'un garde vivant lui est encore affecté ; elle le supprime au passage suivant après libération. C'est volontaire pour éviter de supprimer un véhicule sous un garde survivant.
- La native PATHFIND locale exige `Vector3_t*`, pas `Vector3*`; l'implémentation suit le wrapper du projet.
- Je n'ai pas relancé `generate.bat` : aucun nouveau fichier `.cpp`/`.h` n'a été créé par ce plan.

## 3. Fichiers modifiés / créés

- `Solution/source/Submenus/Bodyguards/BodyguardEscort.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardEscort.h`
- `Solution/source/Submenus/Bodyguards/BodyguardCombat.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardManagement.h`
- `Solution/source/Submenus/Bodyguards/BodyguardSpawn.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardSpawn.h`
- `Solution/source/Submenus/Bodyguards/BodyguardSquads.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardTick.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardConfig.cpp`
- `Solution/source/_Build/bin/Release/menyooStuff/Language/French.json`
- `docs/Bodyguards.md`
- `docs/LESSONS.md`
- `docs/reports/02-optimisations-report.md`

## 4. Nouvelles clés de config et French.json

### `menyooConfig.ini [bodyguards]`

- `escort_catchup_teleport` : bool, défaut `true`, global `g_escortCatchupTeleport`.
- `escort_realistic_boarding` : bool, défaut `false`, global `g_escortRealisticBoarding`.

### `French.json`

- `"Catch-Up Teleport": "Téléportation de rattrapage"`
- `"Realistic Boarding": "Embarquement réaliste (sans téléportation)"`

Validation JSON : fichier valide, 0 clé dupliquée, 407 clés.

## 5. Résultat exact du build

Commande exécutée :

```powershell
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Solution\Menyoo.sln -t:Build -p:Configuration=Release -p:Platform=x64 -m -nologo
```

Premier build : échec avec 2 erreurs de signature natives (`IS_ENTITY_DEAD` et `Vector3_t*` pour PATHFIND), corrigées.

Build final :

```text
Menyoo.vcxproj -> C:\Users\maxan\Documents\Codex\2026-06-24\j-ai-un-objectif-pour-toi\work\MenyooSP\Solution\source\_Build\bin\Release\Menyoo.asi
La génération a réussi.
    0 Avertissement(s)
    0 Erreur(s)
Temps écoulé 00:01:35.82
```

Les 12 warnings C4805 connus dans `GTAmemory.cpp` ne sont pas apparus sur ce build incrémental final.

## 6. Points d'attention pour relecture

- La purge d'épaves est volontairement prudente : elle attend que les gardes vivants aient été libérés avant suppression.
- `Catch-Up Teleport` est ON par défaut ; il ne téléporte que hors écran et au-delà de 150 m.
- `Realistic Boarding` est OFF par défaut ; le comportement fiable par warp reste le défaut.
- `AssignBodyguardsToEscort()` peut maintenant être appelé depuis le tick après changement de véhicule si l'auto-assign est actif ; cet appel reste dans le tick 750 ms et réutilise un toggle existant.
- Aucun handle runtime ajouté (`LastCombatTarget`) n'est persisté.
- Je n'ai pas lancé GTA V et je n'ai pas touché au dossier `D:\SteamLibrary\...`.

## 7. LESSONS

Trois entrées ont été ajoutées à `docs/LESSONS.md` pour les pièges rencontrés pendant cette implémentation :

- heredoc Unix utilisé par erreur dans PowerShell ;
- signature locale de `ENTITY::IS_ENTITY_DEAD` à deux arguments ;
- sortie PATHFIND en `Vector3_t*` et non `Vector3*`.

## Revue Claude (2026-07-04) — PARTIELLE (interrompue par limite de session)

L'agent de revue Fable 5 a été coupé par une limite de session avant d'avoir rédigé son verdict complet. Ce qu'il a eu le temps de faire (reconstitué depuis son transcript, build revérifié ensuite par le loop principal) :

**Analyse effectuée** : diff PlayerOptions, submenu_enum.h, validation JSON de French.json, mots-clés de docs/Bodyguards.md, build.

**5 corrections appliquées au code (toutes dans `BodyguardEscort.cpp`, compilées avec succès) :**
1. Champ `deleteWhenEmpty` ajouté à `SpawnedEscortVehicle`.
2. Purge : si `deleteWhenEmpty`, suppression immédiate du véhicule vidé (contourne la grâce de 12 s, qui ne couvre plus que la purge automatique du tick). Véhicules occupés toujours protégés.
3. **Bug réel corrigé dans la cascade (`SpawnOneEscortVehicle`)** : l'offset `6 - 8*index` plaçait le véhicule #2 à -2 m (chevauchement joueur/véhicule joueur qui s'étend sur ~±2,5 m). Corrigé : véhicule #0 à +6 m devant, les suivants en file derrière à `-8*index`.
4. `AssignBodyguardsToEscort` réarme la purge normale (`deleteWhenEmpty = false`) sur les véhicules survivants (un Assign après un Clear les réutilise, grâce comprise).
5. `ClearAllEscortAssignments` met `deleteWhenEmpty = true` : action explicite de l'utilisateur = disparition immédiate dès que le dernier occupant sort.

Résultat : la décision "Clear = suppression immédiate, grâce = purge auto seulement" (point hérité de la revue du plan 03) est TRANCHÉE et implémentée.

**Build** : MSBuild Release x64 réussi, 0 erreur / 0 avertissement (recompilation forcée de BodyguardEscort.cpp confirmée par le loop principal).

**NON TERMINÉ — à reprendre** : vérification complète lot par lot des lots 2 à 7 du plan 02 (libération sur épave, changement de véhicule joueur, réservation Drivers, correctifs perf, catch-up teleport, realistic boarding, anti-bégaiement combat) ; re-validation finale de French.json avec les clés du plan 02 ; entrées docs/LESSONS.md. Une nouvelle passe de revue est nécessaire pour couvrir ces lots.

## Revue Claude — SUITE ET FIN (Opus 4.8, 2026-07-04)

Reprise après l'interruption de l'agent Fable 5. Vérification complète des 7 lots contre le code actuel + build.

**VERDICT GLOBAL : implémentation fidèle et de bonne qualité. Aucune correction supplémentaire nécessaire** (les 5 corrections de l'agent Fable 5 sur `BodyguardEscort.cpp` sont conservées et compilent).

Vérification lot par lot :
- **Lot 1 (cascade + anti-empilement)** ✔ Boucle bornée (`kMaxEscortVehiclesPerAssign = 4`), test de progression (`pending.size() == before`), sortie sur échec `CREATE_VEHICLE`, un seul `Load`/`Unload`, suppression du véhicule raté par handle précis (`remove_if` sur `rec.veh`). Placement corrigé (`BodyguardEscort.cpp:461`) : véhicule #0 à +6 m, suivants à `-8*index` (plus de chevauchement à -2 m).
- **Lot 2 (épave + changement de véhicule)** ✔ `IsVehicleWrecked` (`IS_ENTITY_DEAD(veh,FALSE) || !IS_VEHICLE_DRIVEABLE`) libère le garde (`:866`) et purge l'épave même occupée de cadavres (`:205`). Détection du changement de véhicule joueur (`:752`) qui ne libère QUE depuis un véhicule non-escorte, ré-affectation anti-rebond 2 s conditionnée à `escort_auto_assign`.
- **Lot 3 (Drivers réservés)** ✔ `stable_partition` (`:663`) quand débordement prévisible.
- **Lot 4 (perf + bornes)** ✔ `GetNearbyPeds(...,128)` (`BodyguardCombat.cpp:357,381`), godmode via `ReapplyEscortGodmodeIfChanged` une fois/tick (`:741`), unlock retiré du chemin chaud, throttle 200 ms mode 1 (`:301`), `deferConvoyAssign` (`BodyguardSpawn.cpp:130` + `BodyguardSquads.cpp:259,267`), bornes `< maxPass` (`BodyguardCombat.cpp:70,92`).
- **Lot 5 (rattrapage)** ✔ Véhicules (`:802`, anti-spam 5 s, pathfind) + gardes à pied (`TickEscortCatchupOnFoot`, câblé `BodyguardTick.cpp:58`). Clé `escort_catchup_teleport` (défaut ON).
- **Lot 6 (embarquement réaliste)** ✔ `SeatBodyguard` (`:368`) TASK_ENTER_VEHICLE si < 25 m, filet reboard via `NextReboardAt+8000`. Clé `escort_realistic_boarding` (défaut OFF).
- **Lot 7 (anti-bégaiement)** ✔ `LastCombatTarget`/`LastCombatTaskAt` en fin de `BodyguardEntity` (jamais persistés), court-circuit `IS_PED_IN_COMBAT` (`:193`), arbitre `ProposeSquadTarget` par priorité (joueur 0 > riposte 1 > wanted 2), émission unique/frame (`:472`). Intègre le relâchement `CombatTasked` du plan 03.

**Non-régression / invariants** ✔ French.json valide (407 clés, 0 doublon, toutes les clés du plan 02 présentes) ; aucune chaîne française codée en dur dans les 4 fichiers ; aucun handle/timestamp persisté ; `ReadBodyguardConfig` sans natives (formation différée) ; plans 01/03 intacts.

**Build** : MSBuild Release x64 réussi, 0 erreur / 0 avertissement (recompilation forcée de `BodyguardEscort.cpp` vérifiée).

**Points à surveiller en jeu (non bloquants, choix de conception) :**
1. **Sortie immédiate vs grâce de 8 s** : quand le joueur QUITTE son véhicule perso (playerVeh → 0), la détection de changement (`:752`) libère aussitôt les gardes de ce véhicule ; la temporisation « joueur à pied > 8 s » du bugfix n°6 devient alors partiellement redondante pour ce cas. Comportement acceptable (les gardes descendent quand vous descendez), mais à confirmer en jeu si vous préfériez qu'ils restent assis quelques secondes.
2. **Mode 4, cible sous priorités indépendantes** : l'arbitre `ProposeSquadTarget` tranche PAR FRAME ; comme chaque source a son propre throttle, la squad peut encore alterner entre la cible visée (prio 0) et une menace police (prio 2) toutes les quelques secondes. Nettement mieux qu'avant, mais pas une priorité stricte inter-frames.
3. `s_lastVehicleWarpAt` (`:804`) est une map statique jamais purgée : fuite négligeable (poignées de véhicules, recyclées par le jeu).

Aucun de ces points n'est un bug ; ils sont notés pour le test en jeu.
