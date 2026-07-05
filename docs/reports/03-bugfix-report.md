# Rapport d'implémentation — Plan 03 bugfix Bodyguard v2

Date : 2026-07-04

Plan source : `docs/plans/03-bugfix.md`

## 1. Résumé étape par étape

### Étape 1 — Revive Dead / Heal All

- `Revive Dead` ranime maintenant aussi les gardes en état GTA "dead or dying" (`IS_PED_DEAD_OR_DYING`) même si `IS_ENTITY_DEAD` ne les comptait pas encore morts.
- Les corps dont le handle n'existe plus sont comptés comme introuvables et affichés dans le toast.
- `Heal All Bodyguards` récupère aussi les gardes "dead or dying" avant d'appliquer la santé, pour éviter un garde couché avec des PV pleins.

### Étape 2 — Véhicule d'escorte disparu

- `TickEscort` nettoie maintenant l'état d'escorte si le handle du véhicule assigné a disparu et si le garde avait réellement un état d'escorte actif (`RemovedFromGroup` ou `EscortSeat != -2`).
- Cela restaure l'appartenance au groupe joueur au lieu de laisser le garde orphelin.

### Étape 3 — Grâce avant suppression des véhicules d'escorte vides

- `s_spawnedEscortVehicles` stocke maintenant un petit enregistrement runtime avec `veh` et `emptySinceMs`.
- Un véhicule d'escorte spawné n'est supprimé que s'il est vide, non assigné à un garde vivant, et vide depuis 12 secondes.
- Les boucles de réutilisation et de maintenance des véhicules spawné ont été adaptées au nouveau type interne.

### Étape 4 — Relâchement de `SET_PED_KEEP_TASK`

- `BodyguardEntity` a reçu `CombatTasked`, champ runtime ajouté en fin de classe et jamais persisté.
- Les ordres de combat marquent les gardes taskés.
- `TickCombatResponse` relâche périodiquement les tâches de combat terminées quand la cible est morte/disparue, et force aussi le relâchement si le mode Combat Response est coupé.
- Les gardes avec état d'escorte ne reçoivent pas de `CLEAR_PED_TASKS`; seul leur cache conducteur est réinitialisé pour laisser `TickEscort` reprendre.

### Étape 5 — Seuil d'étoiles Wanted Threats

- Nouveau réglage `g_wantedThreatMinStars`, défaut 2.
- `FindWantedThreat` ne cherche plus de cible police tant que le niveau de recherche est inférieur à ce seuil.
- Ajout du menu `Wanted Response Min Stars` sous Settings -> Behaviour.
- Ajout de la persistance `[bodyguards] combat_wanted_min_stars`, clampée à 1..5 dans `ReadBodyguardConfig`, sans native GTA.

### Étape 6 — Passagers bloqués dans le véhicule personnel

- `TickEscort` suit depuis quand le joueur est à pied.
- Un garde passager dans un véhicule non spawné par le module est libéré si le joueur reste à pied plus de 8 secondes et s'éloigne de plus de 15 m.
- Les véhicules d'escorte spawné ne sont pas concernés : ils continuent de suivre le joueur à pied via le conducteur.

## 2. Écarts par rapport au plan

- Le plan disait que le fragment de toast "corps introuvables" pouvait rester codé en français. J'ai ajouté une clé anglaise (`" bodies missing"`) et une entrée `French.json`, parce que la règle de mission demandait que tout nouveau toast/libellé ait une clé anglaise + traduction.
- Je n'ai pas lancé `generate.bat` : le plan ne créait aucun nouveau `.cpp/.h`, et le build MSBuild a confirmé que les fichiers Bodyguards existants étaient déjà inclus et compilés.
- Je n'ai pas compilé après chaque étape individuelle. J'ai compilé une fois après les six étapes et les mises à jour de langue/doc ; le build Release x64 a réussi sans correction de compilation.

## 3. Fichiers modifiés / créés

- `Solution/source/Submenus/Bodyguards/BodyguardMenu.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardEscort.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardCombat.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardCombat.h`
- `Solution/source/Submenus/Bodyguards/BodyguardManagement.h`
- `Solution/source/Submenus/Bodyguards/BodyguardSettings.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardConfig.cpp`
- `Solution/source/_Build/bin/Release/menyooStuff/Language/French.json`
- `docs/Bodyguards.md`
- `docs/LESSONS.md`
- `docs/reports/03-bugfix-report.md`

Note : le worktree contenait déjà de nombreuses modifications Bodyguards et docs avant cette implémentation ; cette liste correspond aux fichiers touchés pour le plan 03.

## 4. Nouvelles clés de config et entrées French.json

### Config `[bodyguards]`

| Clé | Type | Défaut | Clamp | Global |
|---|---:|---:|---:|---|
| `combat_wanted_min_stars` | long | 2 | 1..5 | `g_wantedThreatMinStars` |

### French.json

| Clé anglaise | Valeur FR |
|---|---|
| `Wanted Response Min Stars` | `Étoiles min. avant riposte police` |
| ` bodies missing` | ` corps introuvables` |

## 5. Résultat exact du build

Commande exécutée :

```powershell
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Solution\Menyoo.sln -t:Build -p:Configuration=Release -p:Platform=x64 -m -nologo
```

Résultat :

```text
La génération a réussi.
    0 Avertissement(s)
    0 Erreur(s)

Temps écoulé 00:01:40.17
```

Artefact :

```text
Solution\source\_Build\bin\Release\Menyoo.asi
SHA256: CA16BB0E74682C8C5C99B29836FAD5E689234FF6BF2E690C5F45C5758A99BB84
```

Validation langue :

```text
French.json parse OK ; duplicate keys=[]
```

## 6. Points d'attention pour le relecteur

- Le relâchement combat est volontairement doux (`CLEAR_PED_TASKS`, pas `_IMMEDIATELY`) pour éviter un pop visuel.
- Les gardes d'escorte ne sont pas nettoyés comme les gardes à pied après combat : le convoi garde la propriété de leurs tâches.
- Après `Clear Escort Assignments`, un véhicule d'escorte spawné peut rester jusqu'à environ 12 secondes après s'être vidé ; c'est attendu.
- Le nouveau fragment de toast `" bodies missing"` commence par un espace pour s'insérer après le nombre dans `BodyguardMenu.cpp`.
- Un seul piège d'implémentation a été rencontré : un patch large sur `BodyguardCombat.cpp` a échoué après drift de contexte ; il a été repris en patchs plus petits et documenté dans `docs/LESSONS.md`.

Je n'ai pas lancé GTA V et je n'ai pas touché au dossier `D:\SteamLibrary\...`.

## Revue Claude (2026-07-04)

**Contexte de revue** : relecture faite pendant que l'implémentation du plan 02 (optimisations) était en cours dans le même worktree. La revue s'est limitée au périmètre du plan 03 ; les vérifications ont été refaites sur l'état courant des fichiers (post-drift plan 02) pour confirmer que les correctifs du plan 03 ont survécu.

### Verdict

**Fidèle au plan — aucune correction de code nécessaire.** Les 6 correctifs sont implémentés conformément au contrat, les invariants sont respectés, le rapport de Codex correspond à la réalité du code.

### Vérifications effectuées (état courant des fichiers)

1. **Revive Dead** (`BodyguardMenu.cpp`, `ReviveAllDeadBodyguards`) : critère étendu `!IsBodyguardAlive(bg) || IS_PED_DEAD_OR_DYING(ped, true)` ✔ ; ordre `RequestControl → RESURRECT_PED → CLEAR_PED_TASKS_IMMEDIATELY → max health/health/armour → godmode → groupe → rôle → blip` ✔ ; comptage `missing` des handles disparus + toast avec clés i18n (`"Bodyguards revived: "`, `" bodies missing"`, `~o~` hors clé) ✔. Bonus « Heal All récupère les dying » implémenté comme recommandé par le plan ✔.
2. **TickEscort handle mort** (`BodyguardEscort.cpp:829-834`) : `ClearEscortState(bg)` appelé sous garde `RemovedFromGroup || EscortSeat != -2` ✔ (pas de spam pour les gardes jamais affectés). `ClearEscortState` restaure bien le groupe et `WasNeverLeavesGroup` même avec un handle véhicule invalide (via `RestorePlayerGroupAfterEscort`, qui gère aussi le handle ped invalide) ✔.
3. **Grâce purge véhicules** (`DeleteEmptySpawnedEscortVehicles`) : struct `SpawnedEscortVehicle { veh, emptySinceMs }`, grâce 12 s via `GET_GAME_TIMER` ✔ ; protection « occupé physiquement OU affecté à un garde vivant » avec reset du timestamp à 0 ✔ ; adaptations mécaniques (`IsSpawnedEscortVehicle`, `PurgeSpawnedEscortVehicles`, `push_back({veh, 0})`, boucles) ✔.
4. **Relâchement KEEP_TASK** (`BodyguardCombat.cpp`) : champ `CombatTasked` ajouté en fin de `BodyguardEntity`, jamais persisté (vérifié : ni ini, ni `Squads.xml` qui ne sérialise que model/role/count) ✔ ; marquage après chaque `SET_PED_KEEP_TASK(TRUE)` ✔ ; `ReleaseFinishedCombatTasks` throttlé 1 s, gated sur cible morte/disparue, skip des gardes encore en combat ✔ ; branche escorte : pas de `CLEAR_PED_TASKS`, juste reset `DriverTasked`/`CombatTasked` ✔ ; appelé avant l'early-return mode 0 avec `forceRelease` (écart mineur vs plan, amélioration cohérente avec l'intention « couper le mode relâche les gardes ») ✔.
5. **Seuil Wanted** : `g_wantedThreatMinStars` défini/extern ✔ ; gate en tête de `FindWantedThreat` ✔ ; texter « Wanted Response Min Stars » sous Behaviour ✔ ; clé `combat_wanted_min_stars` lue/écrite dans `BodyguardConfig.cpp`, clamp 1..5 défaut 2, **aucune native** dans `ReadBodyguardConfig` ✔ ; entrée `French.json` présente ✔.
6. **Passagers abandonnés** (`BodyguardEscort.cpp:866-883`) : uniquement `EscortSeat != -1` (passagers), uniquement `!IsSpawnedEscortVehicle(veh)`, joueur à pied > 8 s (`s_playerOnFootSince`) et distance > 15 m ; `TASK_LEAVE_VEHICLE` + `SET_PED_KEEP_TASK(true)` + `ClearEscortState` ✔.

**Non-régression plan 01 / invariants** : `French.json` re-validé (Python `utf-8-sig` + `object_pairs_hook`) : parse OK, 405 clés, 0 doublon ; clés plan 01 (`Delete Bodyguard`, toasts) intactes. `submenu_enum.h` : aucun ID réordonné par le plan 03. `BodyguardDb`/`s_bodyguards` : aucune étape n'ajoute/retire d'entrée. `docs/Bodyguards.md` : §4 (grâce 12 s, handle mort, libération passagers), §5 (seuil wanted, relâchement KEEP_TASK), §7 (`combat_wanted_min_stars`, `CombatTasked` jamais persisté) et §8 (tick) exacts.

### Problèmes relevés

- **Mineur (comportement, assumé par le plan)** : « Clear Escort Assignments » ne supprime plus le véhicule spawné dès qu'il se vide — il reste ~12 s après la descente du dernier garde (effet secondaire explicitement assumé par le plan, documenté dans `docs/Bodyguards.md` et le rapport). Si on veut que ce bouton reste une suppression immédiate, il faudrait un flag « delete dès vide » posé par `ClearAllEscortAssignments` (jamais de suppression avec occupants). **Non corrigé ici** : `DeleteEmptySpawnedEscortVehicles` est en pleine zone de réécriture du plan 02 (branche `IsVehicleWrecked`, cascade de véhicules) — à trancher lors de la revue du plan 02.
- Aucun autre problème : pas d'erreur d'implémentation trouvée (pas de nouvelle entrée LESSONS).

### Corrections appliquées

Aucune (rien à corriger dans le périmètre du plan 03).

### Build

- 1ᵉʳ essai : échec `LNK1104` (Menyoo.asi verrouillé — build concurrent de la session plan 02, pas une erreur de code ; tous les .obj avaient compilé sans erreur).
- 2ᵉ essai : **La génération a réussi — 0 Avertissement(s), 0 Erreur(s)** (`Menyoo.asi` daté 2026-07-04 00:56). Nota : ce build valide l'arbre entier, donc aussi le code plan 02 partiel présent à cet instant. Les 12 warnings C4805 « connus » de GTAmemory.cpp ne sont plus apparus.

### À revérifier lors de la revue du plan 02

- La branche `IsVehicleWrecked` ajoutée dans `DeleteEmptySpawnedEscortVehicles` (suppression immédiate d'un véhicule détruit non affecté) et dans `TickEscort` (ClearEscortState sur véhicule wrecked) : cohérence avec la grâce de 12 s du plan 03.
- La question « Clear Escort Assignments = suppression immédiate ? » (voir ci-dessus).
- `SpawnOneEscortVehicle` / cascade de véhicules, `TickEscortCatchupOnFoot`, `g_escortCatchupTeleport`, `g_escortRealisticBoarding` + leurs clés ini (`escort_catchup_teleport`, `escort_realistic_boarding`) : hors périmètre plan 03, non revus ici.
- `French.json` re-modifié par le plan 02 après cette revue : re-valider (doublons/parse) en fin de plan 02.
