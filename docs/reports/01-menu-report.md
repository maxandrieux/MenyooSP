# Rapport d'implémentation - Plan 01-menu

Date: 2026-07-04

## 1. Résumé étape par étape

1. **Toasts internationalisés**  
   Les toasts Bodyguards en français codé en dur ont été remplacés par des clés anglaises. Les messages dynamiques utilisent maintenant `Language::TranslateToSelected(...)` sur le préfixe constant avant concaténation du compteur, du nom ou du modèle.

2. **Compteur dans Bodyguard List**  
   Le titre de `BODYGUARD_LIST` affiche désormais `Bodyguard List (x/7)`, avec `MAX_BODYGUARDS` au lieu d'une valeur codée en dur.

3. **Delete Bodyguard dans le sous-menu entité**  
   Le sous-menu d'un garde contient une action `Delete Bodyguard` en fin de liste. Elle passe par `BodyguardManagement::DeleteBodyguard`, remet la sélection à zéro et retourne au menu précédent.

4. **Noms uniques pour les membres d'escouade**  
   `SpawnSquad` nomme les gardes `Squad #n` et démarre après le plus grand suffixe déjà présent pour éviter les doublons lors de respawns.

5. **Actions individuelles Heal / Bring**  
   Le sous-menu entité ajoute `Heal Bodyguard` et `Bring Bodyguard To Self`. Bring téléporte le garde sans modifier son état escorte runtime.

6. **Statut escorte non cliquable**  
   La ligne `Assigned: x/y - model` utilise maintenant `AddBreak(...)`, donc elle n'est plus sélectionnable, et son préfixe est traduit.

7. **Wanted déplacé vers Player Options**  
   L'entrée `Wanted Level` a été retirée du menu Bodyguards. `PlayerOptions.cpp` ajoute `Wanted Options`, qui ouvre le même sous-menu `BODYGUARD_WANTED`.

8. **Titre Bodyguard Settings**  
   Le titre du sous-menu settings Bodyguards est maintenant `Bodyguard Settings`; l'entrée du menu principal reste `Settings`.

9. **Commentaires enum**  
   Les IDs Bodyguards orphelins ou conservés pour stabilité sont commentés dans `submenu_enum.h` sans réordonnancement.

10. **Documentation, traduction, validation**  
    `docs/Bodyguards.md` a été mis à jour sur les sections concernées. `French.json` a été validé par parse JSON et contrôle de doublons.

## 2. Écarts par rapport au plan

- `SpawnSquad` utilise le plus grand suffixe existant au lieu d'un simple comptage des noms existants. C'est volontaire pour éviter un doublon après suppression d'un garde intermédiaire.
- Le séparateur optionnel `--- Danger ---` a été ajouté avant `Delete Bodyguard`; une clé French.json correspondante a donc été ajoutée.
- L'option facultative de compteur sur le titre `Active Bodyguards` n'a pas été ajoutée, car le plan la marquait optionnelle et le compteur obligatoire concernait `Bodyguard List`.
- `generate.bat` / Premake n'a pas été lancé: aucun nouveau fichier `.cpp/.h` n'a été créé par ce plan, et MSBuild a confirmé que les sources Bodyguards existantes sont déjà dans le projet.
- La clé `"A squad with that name already exists."` existait déjà dans `French.json`; elle a été conservée et le doublon temporaire détecté pendant la validation a été retiré.

## 3. Fichiers modifiés/créés par ce plan

- `Solution/source/Submenus/Bodyguards/BodyguardSpawn.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardMenu.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardSquads.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardEscort.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardSettings.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardSubmenu.cpp`
- `Solution/source/Submenus/PlayerOptions.cpp`
- `Solution/source/Menu/submenu_enum.h`
- `Solution/source/_Build/bin/Release/menyooStuff/Language/French.json`
- `docs/Bodyguards.md`
- `docs/reports/01-menu-report.md`

## 4. Nouvelles clés de config et French.json

Nouvelles clés de config `[bodyguards]`: aucune.

Entrées French.json ajoutées ou rendues vivantes:

- `"' applied: "`
- `"--- Danger ---"`
- `"All bodyguards dismissed."`
- `"All squads have been reset."`
- `"Armor refilled: "`
- `"Bodyguard Settings"`
- `"Bodyguard healed"`
- `"Bodyguard spawned"`
- `"Bodyguard teleported"`
- `"Bodyguards armed: "`
- `"Bodyguards assigned: "`
- `"Bodyguards healed: "`
- `"Bodyguards revived: "`
- `"Bodyguards teleported."`
- `"Bring Bodyguard To Self"`
- `"Could not load model."`
- `"Dead or missing bodyguards removed: "`
- `"Escort assignments cleared: "`
- `"Escort model set"`
- `"Heal Bodyguard"`
- `"Invalid input: "`
- `"Invalid squad name."`
- `"Invalid vehicle model"`
- `"Maximum number of bodyguards reached"`
- `"Member added: "`
- `"Model name cannot be empty."`
- `"No bodyguards to assign"`
- `"No free escort seats"`
- `"Preset '"`
- `"Squad created: "`
- `"Squad member removed."`
- `"Squad members spawned: "`
- `"Squad name cannot be empty."`
- `"Squad reset."`
- `"Unknown ped model."`
- `"Wanted Options"`

`"A squad with that name already exists."` était déjà présent et a été utilisé.

## 5. Résultat exact du build

Commande exécutée:

```powershell
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Solution\Menyoo.sln -t:Build -p:Configuration=Release -p:Platform=x64 -m -nologo
```

Résultat:

- `La génération a réussi.`
- `12 Avertissement(s)`
- `0 Erreur(s)`
- Temps écoulé: `00:02:22.71`
- Sortie: `Solution/source/_Build/bin/Release/Menyoo.asi`

Les 12 avertissements sont les C4805 connus dans `Solution/source/Memory/GTAmemory.cpp` aux lignes 1363, 1372, 1380, 1389, 1396, 1405, 1413, 1426, 1434, 1441, 1450 et 1458.

Validation supplémentaire:

- `French.json` parse correctement en `utf-8-sig`.
- Aucun doublon de clé JSON après correction.
- Recherche des anciens toasts français ciblés dans `Solution/source/Submenus/Bodyguards`: aucun résultat.

## 6. Points d'attention pour relecture

- Vérifier en jeu que `Menu::SetPreviousMenu()` après `Delete Bodyguard` retourne bien à la liste attendue dans toutes les configurations de navigation.
- Vérifier en jeu que `AddBreak(...)` rend correctement la ligne escorte dynamique et non sélectionnable.
- Vérifier en jeu le comportement Bring individuel sur un garde assigné à l'escorte: le plan attend que le tick escorte puisse le faire remonter si `Reboard After Combat` est actif.
- Le fichier `French.json` contient un mélange historique d'accents littéraux et d'échappements Unicode; le parse JSON est valide et le moteur lit les deux formes.
- Aucun test en jeu n'a été lancé, conformément à la règle de ne jamais lancer GTA V ni toucher au dossier `D:\SteamLibrary\...`.

## Revue Claude (2026-07-04)

**Verdict global : implémentation fidèle au plan, aucune erreur d'implémentation trouvée, aucune correction de code nécessaire.**

Vérifications effectuées (les 10 étapes du plan, point par point) :

1. **i18n des toasts** — les 35 sites `PrintBottomLeft/PrintBottomCentre` des fichiers Bodyguard* ont été passés en revue : plus aucune chaîne française codée en dur (grep sur caractères accentués : 0 résultat). Chaque clé anglaise utilisée dans le code existe à l'identique dans `French.json` (espaces finaux des préfixes inclus, `~r~` hors clé). `French.json` parse en `utf-8-sig`, **0 clé dupliquée** (vérifié via `object_pairs_hook`). Bonus non demandé mais correct : les libellés de presets (`Police`, `Military`, `Gang`, `FIB`, `Heavy`) ont aussi leurs clés.
2. **Compteur `Bodyguard List (x/7)`** — conforme (préfixe traduit manuellement, `MAX_BODYGUARDS` non codé en dur). Nuance basse (voir « points à surveiller »).
3. **Delete Bodyguard (sous-menu entité)** — passe bien par `BodyguardManagement::DeleteBodyguard` (invariant `BodyguardDb`/`s_bodyguards` préservé ; `DeleteBodyguard` remet lui-même `g_selectedBodyguardHandle` à 0 si besoin, le sous-menu le remet aussi à 0 puis `Menu::SetPreviousMenu()` + `return` immédiat — conforme au piège documenté du plan). Cohabite avec la suppression par touche B/RLEFT de la liste, dont le libellé `add_IB` est désormais traduit (vérifié : `add_IB` ne traduit pas lui-même, l'enrobage `TranslateToSelected` était nécessaire).
4. **Noms uniques** — `NextSquadSpawnIndex` repart du plus grand suffixe existant (`stoi` protégé par try/catch), pas de collision après suppression intermédiaire. Écart assumé et documenté dans le rapport.
5. **Heal/Bring individuels** — natives correctes (mêmes que Heal All / Bring All), `RequestControl` appelé, état escorte non touché ; vérifié dans `TickEscort` : un garde assigné hors de son véhicule passe par `QueueReboardOrClear` au tick suivant → le reboard rattrape bien un « Bring ».
6. **Ligne de statut escorte** — `AddBreak` avec préfixe `"Assigned: "` traduit ; `AddBreak` re-traduit la chaîne composée (miss loggé une seule fois par variante puis mis en cache dans `Lang::Translate` — comportement accepté par le plan, vérifié dans `Language.cpp:35-50`).
7. **Wanted → Player Options** — l'entrée « Wanted Level » a disparu du menu Bodyguards, `PlayerOptions.cpp:118` ajoute « Wanted Options » vers `SUB::BODYGUARD_WANTED`, l'enum n'a pas bougé, `REGISTER_SUBMENU(BODYGUARD_WANTED, ...)` intact, clé FR présente.
8. **Titre « Bodyguard Settings »** — conforme, clé ré-ajoutée.
9. **Commentaires enum** — commentaires ajoutés sans changement de valeur dans le périmètre du plan. (Le diff git de `submenu_enum.h` montre aussi le déplacement de `MAX_SUBS` en fin d'enum : c'est un changement **antérieur au plan 01** — travail Bodyguard v2 non commité — et les IDs de sous-menus ne sont persistés nulle part dans la config.)
10. **Docs** — `docs/Bodyguards.md` reflète bien le compteur, les actions individuelles, Delete, la ligne de statut, l'accès Wanted via Player Options et les nouveaux points de smoke test (§ correspondants vérifiés).

Problèmes trouvés :

- **Basse** (`BodyguardSettings.cpp:50-58`) : le compteur du titre compte les gardes dont le handle existe encore, alors que la limite de spawn utilise `BodyguardDb.size()`. Si un corps disparaît sans « Cleanup », l'UI peut afficher p. ex. « 5/7 » alors que le spawn refuse (7 entrées en DB). Le plan autorisait explicitement les deux variantes et « Cleanup Dead Bodyguards » résout l'écart → **non corrigé, à surveiller en jeu**.
- **Info** (rapport §1.4) : le rapport écrit « nomme les gardes `Squad #n` » ; en réalité c'est `<nom d'escouade> #n` (ex. `Police #2`), ce qui est le comportement demandé. Simple imprécision de rédaction.
- **Info** : pendant cette revue, un agent travaillait **en parallèle** sur le périmètre du plan 02 (toast « bodies missing » apparu dans `BodyguardMenu.cpp`, évolutions dans `BodyguardEscort.cpp`/`BodyguardCombat.cpp`, clé ` bodies missing` ajoutée à `French.json` pendant la lecture). Ces changements sont hors périmètre plan 01 et n'ont pas été touchés ; `French.json` reste valide (405 entrées, 0 doublon) avec eux.

Corrections appliquées : **aucune** (aucun défaut de sévérité haute ou moyenne). Aucune entrée ajoutée à `docs/LESSONS.md` (réservée aux vraies erreurs d'implémentation).

Build : `MSBuild Release x64` relancé après la revue — **« La génération a réussi », 0 erreur** (build incrémental : toutes les sorties déjà à jour, l'arbre courant — plan 01 + travail plan 02 en cours — avait déjà compilé proprement ; `Menyoo.asi` produit).
