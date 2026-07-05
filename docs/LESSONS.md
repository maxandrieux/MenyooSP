# LESSONS — journal des erreurs et pièges

> Journal cumulatif des erreurs commises, régressions, et pièges rencontrés pendant le développement du système Bodyguard (et du mod en général).
> **But** : ne pas refaire deux fois la même erreur. À passer en contexte à Codex/Claude avant chaque nouvelle implémentation.
>
> **Règle** : à chaque relecture de plan/implémentation, ajouter une entrée. Une erreur = une entrée. On n'efface jamais une entrée (on peut ajouter « ✅ résolu le … » à la fin).

---

## Gabarit d'entrée (copier-coller)

```
### [AAAA-MM-JJ] Titre court de l'erreur
- **Contexte** : quel plan / quel fichier / quelle session (Codex ? Claude ?).
- **Erreur** : ce qui a été fait de travers ou raté.
- **Cause** : pourquoi c'est arrivé.
- **Conséquence** : ce que ça a cassé / le temps perdu.
- **Correctif** : comment on l'a réparé.
- **À faire à l'avenir** : la règle à retenir pour ne pas recommencer.
```

---

## Pièges connus (checklist rapide avant de coder)

Ces points ne sont pas des erreurs déjà commises mais des pièges identifiés à l'analyse — à vérifier systématiquement :

- [ ] **Nouveaux fichiers `.cpp`/`.h` → lancer `generate.bat` (premake) AVANT le build**, sinon ils ne sont pas dans le projet Visual Studio et le build les ignore silencieusement.
- [ ] **`BodyguardDb` et `s_bodyguards` toujours modifiés ensemble** — spawn uniquement via `SpawnBodyguardPed`. Ne jamais toucher une liste sans l'autre.
- [ ] **Jamais persister un handle runtime** (Ped/Vehicle/Blip int, `EscortVehicle`, `EscortSeat`…) dans l'ini ou le XML.
- [ ] **Ne jamais réordonner les IDs existants de `submenu_enum.h`** — ajouter uniquement en fin de plage bodyguard.
- [ ] **Aucune native GTA dans `ReadBodyguardConfig`** (appelé trop tôt) — différer l'application au premier `TickBodyguards()`.
- [ ] **Tout nouveau libellé/toast = clé anglaise + entrée `French.json`** (les toasts sont traduits par chaîne entière, match exact, `~r~` hors de la clé, espaces finaux significatifs).
- [ ] **Ne jamais lancer le jeu ni toucher `D:\SteamLibrary\...`** — build uniquement ; la copie du `.asi` est faite par l'utilisateur.

---

## Erreurs d'implémentation

_(à remplir au fil des relectures)_

### [2026-07-04] Utiliser un heredoc Unix dans PowerShell
- **Contexte** : plan `docs/plans/02-optimisations.md`, validation JSON de `French.json` après ajout des libellés.
- **Erreur** : j'ai lancé une commande Python avec la syntaxe heredoc Unix `python - <<'PY'`, qui n'est pas acceptée par PowerShell.
- **Cause** : réflexe de shell Unix alors que la session courante est PowerShell.
- **Conséquence** : la première validation JSON a échoué avant d'exécuter Python, sans vérifier le fichier.
- **Correctif** : relance avec un here-string PowerShell pipé vers `python -`, puis validation réussie.
- **À faire à l'avenir** : sous PowerShell, utiliser `@' ... '@ | python -` ou une commande `python -c`, jamais `python - <<`.

### [2026-07-04] Copier une signature native sans vérifier le wrapper local
- **Contexte** : plan `02-optimisations`, ajout de la détection d'épave dans `BodyguardEscort.cpp`.
- **Erreur** : j'ai appelé `ENTITY::IS_ENTITY_DEAD(veh)` avec un seul argument, comme dans le pseudo-code du plan.
- **Cause** : le wrapper local `natives.h` expose `IS_ENTITY_DEAD(Entity entity, BOOL p1)`.
- **Conséquence** : premier build échoué avec `error C2660: 'ENTITY::IS_ENTITY_DEAD' : la fonction ne prend pas 1 arguments`.
- **Correctif** : appel corrigé en `ENTITY::IS_ENTITY_DEAD(veh, FALSE)` puis recompilation.
- **À faire à l'avenir** : dès qu'un plan ajoute une native, vérifier sa signature exacte dans `Solution/source/Natives/natives.h` avant le build.

### [2026-07-04] Confondre `Vector3` et `Vector3_t` dans une native PATHFIND
- **Contexte** : plan `02-optimisations`, téléportation de rattrapage du convoi via `GET_NTH_CLOSEST_VEHICLE_NODE_WITH_HEADING`.
- **Erreur** : j'ai passé un `Vector3*` à une native qui attend `Vector3_t*`.
- **Cause** : le code courant utilise souvent le wrapper `Vector3`, mais certaines natives exposées demandent la structure bas niveau `Vector3_t`.
- **Conséquence** : premier build échoué avec `error C2664` sur l'argument 5 de `GET_NTH_CLOSEST_VEHICLE_NODE_WITH_HEADING`.
- **Correctif** : remplacement de la variable de sortie par `Vector3_t`, dont les champs `x/y/z` restent compatibles avec la suite du code.
- **À faire à l'avenir** : pour toutes les natives à paramètre de sortie vectoriel, vérifier si la signature demande `Vector3_t*` au lieu de `Vector3*`.

### [2026-07-04] Ajouter une clé French.json déjà existante
- **Contexte** : plan `docs/plans/01-menu.md`, mise à jour des toasts Bodyguards dans `French.json`.
- **Erreur** : j'ai temporairement ajouté une deuxième entrée `"A squad with that name already exists."` alors que la clé existait déjà plus haut dans le fichier.
- **Cause** : j'ai suivi la liste du plan sans d'abord vérifier toutes les clés existantes dans le JSON complet.
- **Conséquence** : un JSON avec clé dupliquée aurait pu masquer la première valeur selon le parseur et rendre les diffs plus difficiles à relire.
- **Correctif** : validation avec `json.load(..., object_pairs_hook=...)`, détection du doublon, puis suppression de l'entrée ajoutée en trop.
- **À faire à l'avenir** : avant d'ajouter des clés i18n, charger le JSON et comparer la liste des nouvelles clés avec les clés existantes.

### [2026-07-04] Patcher des lignes accentuées depuis une sortie terminal mojibake
- **Contexte** : plan `01-menu`, édition de `French.json` et lecture de fichiers contenant des accents.
- **Erreur** : un patch initial sur `French.json` a échoué parce que le contexte copié depuis la sortie terminal affichait les accents sous forme corrompue (`Ã©`, `Ã `, etc.).
- **Cause** : la console PowerShell n'affichait pas l'UTF-8 comme le fichier était réellement encodé.
- **Conséquence** : perte de temps et risque de créer des remplacements incorrects si on force le patch avec du mauvais contexte.
- **Correctif** : reprise par petits patchs ancrés sur des lignes simples, et vérification avec Python en `utf-8-sig`.
- **À faire à l'avenir** : pour les fichiers accentués, éviter de faire confiance au rendu terminal; utiliser des ancres ASCII ou afficher les lignes via Python avec encodage contrôlé.

### [2026-07-04] PowerShell peut casser les recherches complexes avec guillemets et parenthèses
- **Contexte** : vérifications `rg` après implémentation.
- **Erreur** : une recherche avec une expression contenant guillemets, parenthèses et `|` a échoué côté PowerShell avant même d'exécuter `rg`.
- **Cause** : quoting PowerShell trop fragile pour une expression longue en ligne de commande.
- **Conséquence** : faux signal de problème possible si on lit vite l'échec comme un résultat de recherche.
- **Correctif** : relance avec une expression plus simple et mieux quotée.
- **À faire à l'avenir** : pour les recherches regex complexes sous PowerShell, simplifier la regex, utiliser des quotes simples, ou découper en plusieurs recherches.

### [2026-07-04] Patcher un contexte large après drift de fichier
- **Contexte** : plan `docs/plans/03-bugfix.md`, ajout du relâchement de combat dans `BodyguardCombat.cpp`.
- **Erreur** : un patch initial trop large a échoué parce que le contexte exact autour de `IsBodyguardDriver` / `TaskBodyguardOnTarget` ne correspondait plus assez précisément au fichier courant.
- **Cause** : le fichier avait déjà beaucoup évolué dans le worktree Bodyguards, et le patch mélangeait plusieurs changements indépendants dans un seul bloc.
- **Conséquence** : perte de temps limitée et risque, si on insistait, d'appliquer une modification au mauvais endroit.
- **Correctif** : découpage en patchs plus petits : définition globale, helper de combat, marquage `CombatTasked`, relâchement périodique, seuil wanted, puis wiring du tick.
- **À faire à l'avenir** : quand un fichier a déjà beaucoup bougé, relire les lignes proches et appliquer un changement logique par patch plutôt qu'un gros patch multi-zone.

### [2026-07-04] Offset de cascade « 6 − 8·i » : le 2ᵉ véhicule spawnait sur le joueur
- **Contexte** : plan `docs/plans/02-optimisations.md` lot 1 (cascade de véhicules d'escorte), `BodyguardEscort.cpp` / `SpawnOneEscortVehicle`, implémentation Codex relue par Claude.
- **Erreur** : la formule de placement du plan `Y = 6 − 8·convoyIndex` a été recopiée telle quelle ; pour `i = 1` elle donne `Y = −2 m`, soit un `CREATE_VEHICLE` en chevauchement avec le joueur à pied ou avec sa propre voiture (~±2,5 m) — précisément l'empilement que le lot devait empêcher.
- **Cause** : le pseudo-code du plan contenait l'erreur ; ni le plan ni l'implémentation n'ont vérifié la valeur numérique produite pour i = 1 contre l'encombrement réel d'un véhicule.
- **Conséquence** : au 2ᵉ véhicule de cascade (scénario 7 gardes + `police`), collision/éjection physique au spawn, surtout quand le joueur est dans sa voiture.
- **Correctif** : `Y = +6` pour le véhicule 0, `Y = −8·i` pour les suivants (−8, −16, −24) — espacement ≥ 8 m entre véhicules et aucun retour dans la zone du joueur.
- **À faire à l'avenir** : pour tout placement par offset, dérouler numériquement la formule pour les 2-3 premiers index et la comparer à l'encombrement des entités (véhicule ≈ 5 m de long) avant de valider ; ne pas faire confiance à un pseudo-code de plan pour de la géométrie.

### [2026-07-05] Ne pas faire confiance à une sortie de plan tronquée
- **Contexte** : plan `docs/plans/04-features.md`, première lecture terminal du plan complet.
- **Erreur** : la sortie `Get-Content -Raw` a été tronquée par la limite d'affichage alors que le plan devait être lu en entier avant de coder.
- **Cause** : le plan est long et la sortie outil a coupé le milieu sans que le fichier lui-même soit incomplet.
- **Conséquence** : risque d'implémenter seulement les jalons visibles et de manquer des consignes du milieu du plan.
- **Correctif** : relecture par tranches numérotées `1-170`, `171-340`, puis `341+` avant toute édition.
- **À faire à l'avenir** : quand un plan long affiche `truncated output`, relire immédiatement par plages de lignes et ne coder qu'après avoir vu la fin du fichier.

### [2026-07-05] Confondre l'emplacement de `MAX_BODYGUARDS`
- **Contexte** : plan `04-features`, nouveau module `BodyguardHud.cpp`.
- **Erreur** : j'ai référencé `BodyguardManagement::MAX_BODYGUARDS` après avoir seulement inclus `BodyguardManagement.h`.
- **Cause** : la constante est déclarée dans `BodyguardSpawn.h`, dans le namespace `BodyguardManagement`, pas dans `BodyguardManagement.h`.
- **Conséquence** : premier build échoué avec `error C2039: 'MAX_BODYGUARDS' n'est pas membre de 'sub::BodyguardMenu::BodyguardManagement'`.
- **Correctif** : inclure `BodyguardSpawn.h` dans `BodyguardHud.cpp`, ce qui rend la constante disponible.
- **À faire à l'avenir** : avant d'utiliser une constante namespace dans un nouveau fichier, vérifier le header qui la déclare avec `rg`, pas seulement le namespace logique.

### [2026-07-05] Copier le nom d'une native du plan sans vérifier le wrapper local
- **Contexte** : plan `04-features`, mode Chauffeur libre.
- **Erreur** : le plan mentionnait `TASK_VEHICLE_WANDER_STANDARD`, mais cette fonction n'existe pas dans le wrapper local.
- **Cause** : le nom du plan ne correspondait pas à `Solution/source/Natives/natives.h`.
- **Conséquence** : risque d'ajouter un appel impossible à compiler.
- **Correctif** : vérification dans `natives.h` et utilisation de `TASK::TASK_VEHICLE_DRIVE_WANDER`.
- **À faire à l'avenir** : pour chaque native nouvelle, vérifier le nom exact exposé par le wrapper local avant de patcher.

### [2026-07-05] Patcher `French.json` depuis une sortie console mojibake
- **Contexte** : plan `04-features`, ajout des nouvelles clés de menu et toasts dans `French.json`.
- **Erreur** : un patch manuel a échoué parce que le contexte copié depuis PowerShell affichait les accents sous forme corrompue.
- **Cause** : le fichier est UTF-8, mais la console affichait certains caractères accentués en mojibake.
- **Conséquence** : perte de temps et risque d'ancrer un patch sur du texte qui n'existe pas réellement dans le fichier.
- **Correctif** : validation préalable sans doublons, puis insertion ciblée des clés manquantes en conservant le reste du fichier, suivie d'une validation JSON.
- **À faire à l'avenir** : pour `French.json`, éviter les contextes accentués issus du terminal ; utiliser des ancres ASCII ou une insertion structurée validée par `json.load`.

### [2026-07-04] Recopier une formule de placement de l'esquisse du plan sans la valider géométriquement
- **Contexte** : plan `02-optimisations.md` lot 1, cascade de véhicules d'escorte (`BodyguardEscort.cpp`, `SpawnOneEscortVehicle`).
- **Erreur** : l'offset de placement `6 - 8*convoyIndex` (repris tel quel de l'esquisse du plan) plaçait le 2ᵉ véhicule à Y = -2 m, c'est-à-dire par-dessus le joueur / son véhicule (qui s'étend sur ~±2,5 m). Empilement physique au lieu d'une file.
- **Cause** : l'esquisse du plan contenait elle-même la formule bancale ; elle a été recopiée sans tracer les positions réelles pour index 0, 1, 2.
- **Conséquence** : les véhicules 2..N se seraient chevauchés/percutés au spawn (le bug même que le lot 2 « anti-empilement » devait éviter). Détecté et corrigé à la relecture.
- **Correctif** : `offsetY = (convoyIndex == 0) ? 6.f : -8.f * convoyIndex` — 1er véhicule devant, suivants en file derrière.
- **À faire à l'avenir** : pour toute formule de position/offset, calculer mentalement les 2-3 premières valeurs concrètes avant d'accepter le code, même quand il vient d'une esquisse de plan validée.

### [2026-07-05] Le nettoyage d'état orphelin de TickEscort remettait Hold/Medic dans le groupe
- **Contexte** : plan `04-features` (jalon 3), `BodyguardEscort.cpp` / boucle `TickEscort`, implémentation Codex relue par Claude.
- **Erreur** : la boucle de `TickEscort` traitait tout garde `RemovedFromGroup` sans `EscortVehicle` comme un état orphelin à réparer (`ClearEscortState` → retour au groupe vanilla). Or Hold Position et le Medic en soin sont précisément dans cet état, légitimement. Seul le chauffeur était exclu.
- **Cause** : le champ `RemovedFromGroup` a gagné de nouveaux « propriétaires » (hold, medic) sans que tous les consommateurs existants du champ soient réaudités.
- **Conséquence** : toutes les 750 ms, un garde en Hold ou un Medic en soin était réintégré au groupe (le groupe le fait re-suivre le joueur), puis re-retiré par son propre tick — tir à la corde permanent, les deux features étaient inutilisables en pratique.
- **Correctif** : exclusion `IsBusyBodyguard(ped) || bg.HoldPosition` en tête de boucle de `TickEscort` (+ même exclusion dans `TickEscortCatchupOnFoot`).
- **À faire à l'avenir** : quand un nouveau mode réutilise un champ d'état partagé (`RemovedFromGroup`, `KEEP_TASK`…), lister TOUS les lecteurs existants du champ (`rg RemovedFromGroup`) et vérifier chacun, pas seulement les points d'intégration cités par le plan.

### [2026-07-05] CeaseFireAll clear les tâches de tous les gardes, y compris chauffeur et medic
- **Contexte** : plan `04-features` (jalon 3), `BodyguardCombat.cpp` / `CeaseFireAll`, implémentation Codex relue par Claude.
- **Erreur** : `CeaseFireAll` faisait `CLEAR_PED_TASKS` sur tous les gardes vivants sans consulter `IsBusyBodyguard`, alors que le tasking combat, lui, la consultait.
- **Cause** : l'exclusion « busy » a été câblée à l'entrée du combat (tasking) mais pas à sa sortie (cease fire) — les deux chemins mutent pourtant les mêmes peds.
- **Conséquence** : un « Cessez-le-feu » pendant un trajet chauffeur arrêtait la voiture définitivement (tâche de conduite perdue, aucune ré-émission avant le correctif filet) ; idem pour un soin en cours.
- **Correctif** : `CeaseFireAll` saute les gardes `IsBusyBodyguard` (les flags combat sont quand même remis à zéro) ; ajout du filet `GET_SCRIPT_TASK_STATUS` dans `TickChauffeur` en défense en profondeur.
- **À faire à l'avenir** : toute exclusion d'arbitrage (busy/hold) doit être appliquée symétriquement aux deux sens d'une feature (donner des ordres ET les annuler).

### [2026-07-05] Cache de tâche contourné pour les hélicos : re-task toutes les 750 ms
- **Contexte** : plan `04-features` (jalon 5), `BodyguardEscort.cpp` / branche aérienne de `TickEscort`, implémentation Codex relue par Claude.
- **Erreur** : `TASK_HELI_MISSION` (suivi et atterrissage) était ré-émis à chaque tick escorte, en sautant le cache `IsDriverTaskCurrent` pourtant prévu par le plan (« le cache de tâche est réutilisé tel quel ») ; même schéma côté chauffeur pour la mission d'atterrissage (500 ms).
- **Cause** : l'état « atterrissage » n'entrait pas dans la clé du cache existant, donc le plus simple localement était de re-tasker toujours — exactement le bégaiement que le plan 02 avait éliminé au sol.
- **Conséquence** : IA hélico réinitialisée en permanence (vol saccadé, approche d'atterrissage relancée en boucle, hélico qui ne se pose jamais).
- **Correctif** : cache réutilisé pour le suivi ; atterrissage encodé dans le cache avec la sentinelle `targetVeh == veh` (clé impossible en suivi) ; côté chauffeur, flag `s_heliLandingIssued` à émission unique.
- **À faire à l'avenir** : jamais de tâche IA ré-émise inconditionnellement dans un tick ; si un nouvel état ne rentre pas dans la clé du cache, étendre la clé (ou une sentinelle documentée), pas contourner le cache.

### [2026-07-05] Siège -2 passé à SET_PED_INTO_VEHICLE
- **Contexte** : plan `04-features` (jalon 2), `BodyguardChauffeur.cpp` / installation du joueur passager, implémentation Codex relue par Claude.
- **Erreur** : `SET_PED_INTO_VEHICLE(ped, veh, -2)` en fallback « n'importe quel siège » — cette convention n'existe que pour `TASK_ENTER_VEHICLE` ; le reste du codebase résout d'abord un siège concret (`FirstFreeSeat`, cf. `VehicleSpawner.cpp:117`).
- **Cause** : confusion entre les conventions de sièges des deux natives.
- **Conséquence** : si le siège 0 était occupé (garde déjà passager), le warp du joueur échouait silencieusement, puis le mode chauffeur s'auto-arrêtait 4 s plus tard (« joueur sorti du véhicule »).
- **Correctif** : helper `FirstFreePassengerSeat` (siège concret) ; véhicule plein → refus propre avec toast avant toute mutation d'état.
- **À faire à l'avenir** : pour les valeurs de siège magiques (-1/-2), vérifier la convention native par native et regarder comment le codebase existant résout le même problème.

### [2026-07-05] Catch-up teleport véhicule appliqué aux hélicos en vol
- **Contexte** : plan `04-features` (jalon 5 × plan 02 lot catch-up), `BodyguardEscort.cpp` / warp des véhicules d'escorte, implémentation Codex relue par Claude.
- **Erreur** : l'hélico d'escorte étant ajouté à `s_spawnedEscortVehicles` (pour hériter du cleanup), il héritait AUSSI du catch-up teleport : à > 150 m hors écran, warp sur un nœud routier près du joueur + `SET_VEHICLE_ON_GROUND_PROPERLY` — un hélico en vol plaqué au sol, voire téléporté dans un tunnel.
- **Cause** : réutiliser une liste existante fait hériter de TOUS ses consommateurs, pas seulement de ceux qu'on visait (cleanup/godmode) ; personne n'a réaudité les autres boucles qui itèrent la liste.
- **Conséquence** : hélico d'escorte écrasé au sol/dans un tunnel dès que le joueur roule vite ou passe sous terre.
- **Correctif** : les hélicos sont exclus du warp véhicule (leur mission Follow les ramène en volant).
- **À faire à l'avenir** : avant d'inscrire un nouveau type d'entité dans une liste trackée existante, `rg` tous les consommateurs de la liste et valider chaque comportement hérité.
