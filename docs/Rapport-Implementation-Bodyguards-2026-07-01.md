# Rapport d'implementation Bodyguards - 2026-07-01

Ce document resume le travail effectue pour implementer le plan
`docs/Plan-Correction-Bodyguards-2026-07-01.md` dans le module Bodyguards de MenyooSP.

## Objectif traite

L'objectif etait de corriger et consolider la fonctionnalite Bodyguards autour de quatre axes :

- rendre l'escorte fiable, notamment quand plusieurs gardes ou plusieurs escouades sont affectes ;
- eviter les regressions dangereuses comme la suppression d'un vehicule personnel ou d'un vehicule encore occupe ;
- reorganiser les menus pour separer actions, reglages et outils ;
- aligner les traductions, la persistance et la documentation.

Le travail a ete fait dans le depot local :

`C:\Users\maxan\Documents\Codex\2026-06-24\j-ai-un-objectif-pour-toi\work\MenyooSP`

## Resultat global

Le plan a ete implemente dans le code et le projet compile en Release x64.

Artefact genere :

`Solution\source\_Build\bin\Release\Menyoo.asi`

Details du build :

- Taille : `4,609,536` octets
- Date locale de generation : `2026-07-01 22:34:29`
- SHA-256 : `244731ACEAA942848A105E45CE2465581B9A03FAE64DBCF28960A83FB469C8F3`

Je n'ai pas copie l'artefact dans le dossier GTA. Le plan indiquait un deploiement/test en jeu cote utilisateur, et aucune demande explicite de copie dans l'installation GTA n'a ete faite dans ce tour.

## Corrections escorte

Le coeur de l'escorte a ete repris dans `BodyguardEscort.cpp` / `BodyguardEscort.h`.

### Sortie du groupe joueur pendant l'escorte

Un champ runtime a ete ajoute a `BodyguardEntity` :

- `RemovedFromGroup`

Quand un garde est assigne a une escorte, il est retire du groupe joueur avec `REMOVE_PED_FROM_GROUP`. Cela evite que l'IA de groupe de GTA le force a descendre d'un vehicule qui n'est pas le vehicule leader du joueur.

Lors d'un clear, d'une suppression ou d'un nettoyage, le garde est re-ajoute au groupe joueur si necessaire, puis `SET_PED_NEVER_LEAVES_GROUP` est restaure avec l'etat sauvegarde dans `WasNeverLeavesGroup`.

### Remplissage des sieges

L'algorithme d'assignation ne s'arrete plus au premier garde qui echoue :

- les echecs individuels passent au candidat suivant ;
- les boucles passagers utilisent `seat < maxPassengers` ;
- le siege conducteur essaie d'abord les gardes avec role `Driver`, puis les autres ;
- un garde deja physiquement assis dans le vehicule du joueur est adopte au lieu d'etre teleporte ailleurs.

Le toast d'assignation indique maintenant le nombre affecte par rapport au nombre initialement en attente.

### Convoi unique

Une liste runtime des vehicules d'escorte crees par le module a ete ajoutee :

- `s_spawnedEscortVehicles`

L'ordre de remplissage est maintenant :

1. sieges passagers libres du vehicule joueur, si l'option est active ;
2. sieges libres des vehicules d'escorte deja crees par le module ;
3. creation d'un nouveau vehicule seulement si des gardes restent non assignes et si `Spawn Escort Vehicle If Full` est actif.

Effet attendu : si une deuxieme escouade est spawn apres une premiere affectation, un nouvel `Assign` complete le vehicule d'escorte existant avant de creer un second vehicule.

### Suppression sure des vehicules

La suppression ne se base plus sur l'heuristique `MissionEntity`.

Le module ne supprime que les vehicules presents dans `s_spawnedEscortVehicles`. Les vehicules personnels, le vehicule courant du joueur ou les vehicules crees par d'autres fonctions Menyoo ne sont pas eligibles.

Avant suppression, le code verifie physiquement les sieges avec `GET_PED_IN_VEHICLE_SEAT`. Un vehicule d'escorte n'est supprime qu'une fois vide.

`Clear Escort Assignments` demande d'abord aux gardes de quitter le vehicule avec `TASK_LEAVE_VEHICLE`, puis la suppression effective se fait quand le vehicule est vide.

### Conducteur plus fluide

Des champs runtime ont ete ajoutes a `BodyguardEntity` pour eviter de re-emettre les taches conducteur sans raison :

- `DriverTasked`
- `LastEscortPlayerInVehicle`
- `LastEscortTargetVehicle`

Le tick d'escorte ne relance plus `TASK_VEHICLE_ESCORT` / `TASK_VEHICLE_FOLLOW` a chaque passage. Il retache seulement quand l'etat suivi change, ce qui reduit le stop-and-go periodique.

Si un vehicule d'escorte cree par le module perd son conducteur, un passager est promu au siege conducteur, avec preference pour le role `Driver`.

### Reboard apres combat

Deux champs runtime ont ete ajoutes :

- `NextReboardAt`
- `ReboardAttempts`

Quand un garde assigne se retrouve a pied alors que son vehicule existe encore, le tick tente de le faire remonter. Apres plusieurs echecs, il utilise un warp de secours vers son siege assigne. Si les tentatives depassent la limite, l'affectation est nettoyee.

Cette logique est controlee par l'option persistante :

- `escort_reboard_after_combat`

Defaut : active.

### Options supplementaires

Trois options persistantes ont ete ajoutees :

- `escort_auto_assign`
- `escort_reboard_after_combat`
- `escort_vehicle_godmode`

Ces options correspondent aux reglages :

- `Auto-Assign New Spawns`
- `Reboard After Combat`
- `Escort Vehicle Godmode`

`Auto-Assign New Spawns` n'affecte les nouveaux gardes que si un convoi existe deja. Cela evite d'assigner automatiquement des gardes quand aucune escorte n'est en cours.

## Interaction combat / escorte

Le combat a ete ajuste dans `BodyguardCombat.cpp`.

Un garde avec `EscortVehicle` assigne ne recoit plus de `TASK_COMBAT_PED` a pied. S'il est deja en vehicule, il conserve la branche vehicule existante :

- `SET_VEHICLE_SHOOT_AT_TARGET`
- `TASK_VEHICLE_SHOOT_AT_PED`
- `TASK_DRIVE_BY`

But : pendant une escorte, les passagers doivent tirer depuis le vehicule au lieu de descendre pour combattre a pied.

Le mode combat par defaut reste `4`, soit `Full Protection`, et la documentation a ete corrigee pour ne plus annoncer le defaut `1`.

## Menus

### Menu principal Bodyguards

Le menu principal a ete simplifie. Il contient maintenant les entrees principales :

- `Spawn Bodyguard`
- `Bodyguard List`
- `Squads`
- `Escort Vehicle`
- `Squad Tools`
- `Settings`
- `Wanted Level`
- section `--- Actions ---`
- `Bring Bodyguards To Self`

Les anciens liens racine suivants ont ete retires du chemin principal :

- `Squad Equipment`
- `Squad Presets`
- `Squad Recovery`
- `Squad Cleanup`

Les IDs historiques restent presents pour eviter les decalages d'enum.

### Squad Tools fusionne

`Squad Tools` regroupe maintenant :

- section `--- Weapons ---`
- armement global ;
- auto-armement des nouveaux gardes ;
- arme de spawn ;
- section `--- Health ---`
- soin global ;
- restauration d'armure ;
- `Revive Dead` ;
- section `--- Equipment Presets ---`
- preset, application a l'escouade courante, utilisation pour les nouveaux gardes ;
- section `--- Cleanup ---`
- nettoyage des gardes morts ;
- renvoi de tous les gardes.

Le placeholder `Revive Dead (coming soon)` a ete remplace par une action effective.

### Revive Dead

`Revive Dead` utilise `RESURRECT_PED`, restaure sante, armure, invincibilite selon `Godmode`, role, blip et appartenance au groupe joueur.

### Settings

Le sous-menu `BODYGUARD_SETTINGS`, auparavant presque vide, contient maintenant les reglages qui etaient a la racine :

- section `--- Spawn Defaults ---`
- `Default Health`
- `Default Armor`
- `Godmode`
- `Default Spawn Role`
- section `--- Blips ---`
- `Role Blips`
- `Bodyguard Blip`
- section `--- Behaviour ---`
- `Combat Response`
- `Formation`

### Escort Vehicle

Le menu escorte a ete reorganise :

- ligne d'etat `Assigned: X/Y - model`
- `Assign Bodyguards To Escort`
- `Clear Escort Assignments`
- section `--- Settings ---`
- `Use My Seats First`
- `Spawn Escort Vehicle If Full`
- `Auto-Assign New Spawns`
- `Reboard After Combat`
- `Escort Vehicle Godmode`
- `Escort Vehicle Model`
- `Custom Model...`
- `Escort Driving Style`

### Squad Edit

Un nouvel ID a ete ajoute a la fin du bloc bodyguard dans `submenu_enum.h` :

- `BODYGUARD_SQUAD_MEMBER_EDIT`

Le menu d'edition d'une escouade affiche maintenant une ligne par membre :

- `model (Role xCount)`

L'ouverture d'un membre donne :

- `Role`
- `Count`
- `Remove Member`

Le bouton `Save Squads` a ete retire du parcours, car les modifications sauvegardent deja immediatement.

## Persistance

`BodyguardConfig.cpp` lit et sauvegarde les nouvelles cles :

- `escort_auto_assign`
- `escort_reboard_after_combat`
- `escort_vehicle_godmode`

Les handles runtime ne sont pas persistes. Les champs comme ped, vehicule, blip, siege, cache conducteur et tentatives de reboard restent strictement runtime.

## Traductions

`French.json` a ete mis a jour et valide par parsing JSON.

Nouvelles entrees ou entrees corrigees :

- `Auto-Assign New Spawns`
- `Reboard After Combat`
- `Escort Vehicle Godmode`
- `Revive Dead`
- `Equipment Presets`
- sections de menu `--- Actions ---`, `--- Settings ---`, `--- Weapons ---`, etc.

L'ancien libelle `Revive Dead (coming soon)` n'est plus utilise par le code.

## Documentation

`docs/Bodyguards.md` a ete mis a jour pour refleter :

- les nouveaux champs runtime ;
- le retrait temporaire des gardes du groupe joueur pendant l'escorte ;
- le remplissage des sieges et le convoi unique ;
- la suppression sure des vehicules ;
- le tick conducteur avec cache d'etat ;
- la re-affectation apres combat ;
- les nouvelles options persistantes ;
- le defaut `Combat Response = 4 / Full Protection` ;
- la nouvelle navigation Squads -> Squad -> Member ;
- la checklist de test en jeu.

## Fichiers modifies ou ajoutes

Fichiers modifies deja suivis par Git :

- `Solution/source/Menu/Menu.cpp`
- `Solution/source/Menu/MenuConfig.cpp`
- `Solution/source/Menu/Routine.cpp`
- `Solution/source/Menu/submenu_enum.h`
- `Solution/source/Submenus/Bodyguards/BodyguardManagement.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardManagement.h`
- `Solution/source/Submenus/Bodyguards/BodyguardMenu.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardMenu.h`
- `Solution/source/Submenus/Bodyguards/BodyguardSettings.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardSettings.h`
- `Solution/source/Submenus/Bodyguards/BodyguardSpawn.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardSpawn.h`
- `Solution/source/Submenus/Bodyguards/BodyguardSubmenu.cpp`
- `Solution/source/_Build/bin/Release/menyooStuff/Language/French.json`

Fichiers nouveaux non suivis au moment du rapport :

- `Solution/source/Submenus/Bodyguards/BodyguardCombat.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardCombat.h`
- `Solution/source/Submenus/Bodyguards/BodyguardConfig.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardConfig.h`
- `Solution/source/Submenus/Bodyguards/BodyguardDebug.h`
- `Solution/source/Submenus/Bodyguards/BodyguardEscort.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardEscort.h`
- `Solution/source/Submenus/Bodyguards/BodyguardRole.h`
- `Solution/source/Submenus/Bodyguards/BodyguardSquads.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardSquads.h`
- `Solution/source/Submenus/Bodyguards/BodyguardTick.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardTick.h`
- `Solution/source/Submenus/Bodyguards/BodyguardWanted.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardWanted.h`
- `docs/`

Note : plusieurs fichiers Bodyguards etaient deja non suivis avant cette passe. Je les ai conserves et modifies dans le cadre du plan, sans supprimer ni reinitialiser de travail existant.

## Validations faites

### JSON

Validation effectuee :

- chargement de `French.json` avec `ConvertFrom-Json`
- resultat : OK

### Projet Visual Studio

Verification effectuee :

- les nouveaux `.cpp` Bodyguards sont deja references dans `Solution/Menyoo.vcxproj`
- pas besoin de regeneration Premake pour cette passe

### Build

Commande de build :

`MSBuild Solution\Menyoo.vcxproj /p:Configuration=Release /p:Platform=x64 /m`

Resultat final :

- build reussi ;
- `0 Avertissement(s)` ;
- `0 Erreur(s)` ;
- artefact : `Solution\source\_Build\bin\Release\Menyoo.asi`.

## Checklist de test en jeu recommandee

Ces tests n'ont pas ete executes ici, car ils demandent GTA lance avec le mod deploye.

1. Spawn 7 gardes, entrer dans une berline, activer `Use My Seats First` et `Spawn Escort Vehicle If Full`, puis `Assign Bodyguards To Escort`.
   - Attendu : les sieges joueur sont remplis, l'overflow va dans un seul vehicule d'escorte, pas de boucle de porte.

2. Spawn une escouade, assigner l'escorte, spawn une deuxieme escouade, reassigner.
   - Attendu : la deuxieme escouade remplit les places libres du vehicule d'escorte existant avant creation d'un nouveau vehicule.

3. Utiliser `Clear Escort Assignments`.
   - Attendu : les gardes descendent, le vehicule d'escorte cree par le module est supprime une fois vide, le vehicule personnel/joueur reste intact.

4. Pendant l'escorte, viser ou attaquer un pieton hostile.
   - Attendu : les passagers utilisent les taches de tir vehicule/drive-by et ne descendent pas a cause d'un `TASK_COMBAT_PED` a pied.

5. Faire sortir un garde du vehicule pendant/apres combat avec `Reboard After Combat` actif.
   - Attendu : le garde tente de remonter, puis est remis dans son siege apres echecs repetes.

6. Tester `Auto-Assign New Spawns`.
   - Attendu : un garde cree pendant qu'un convoi existe rejoint le prochain siege libre ; aucun auto-assign ne se produit s'il n'existe pas de convoi.

7. Tester `Escort Vehicle Godmode`.
   - Attendu : seuls les vehicules d'escorte crees par ce module recoivent l'invincibilite/proofs.

8. Naviguer dans les menus :
   - `Bodyguards`
   - `Squad Tools`
   - `Settings`
   - `Escort Vehicle`
   - `Squads -> squad -> member`

9. Redemarrer le jeu apres modification de reglages.
   - Attendu : les cles `[bodyguards]` round-trip correctement dans `menyooConfig.ini`, et `Squads.xml` conserve les modifications de membres.

## Points de prudence

- La suppression des vehicules est maintenant volontairement stricte : seuls les vehicules presents dans la liste runtime des escort vehicles crees par le module sont supprimables.
- Les anciens sous-menus de squad non exposes a la racine peuvent rester enregistrables par ID, mais ne font plus partie du chemin utilisateur principal.
- Le test en jeu reste indispensable pour valider les comportements IA GTA : entree/sortie vehicule, drive-by, reboard, promotion conducteur.

## Etat final

Le code compile proprement et le document de reference `docs/Bodyguards.md` a ete mis a jour. Le present rapport sert de journal complet de l'implementation effectuee pour le plan du 2026-07-01.
