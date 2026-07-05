# Revue globale Bodyguards v2 — synthèse finale (2026-07-05)

Audit transversal de tout le travail Bodyguard (Mission 0 + plans 01 à 04 et leurs relectures).
Build final : **MSBuild Release x64 réussi, 0 erreur, 0 avertissement**, avec recompilation forcée
des 13 fichiers `Bodyguard*.cpp` (tous les `.obj` regénérés, `Menyoo.asi` produit).

---

## 1. État final du système (fonctionnalités par catégorie)

| Catégorie | Contenu |
|-----------|---------|
| **Rôles** | 6 rôles (Rifleman, Shotgun, Sniper, Heavy, Medic, Driver) : arme, précision, cadence, portée, blip dédié. Rôle par défaut persisté, rôle modifiable par garde. |
| **Gestion** | Spawn (limite 7, compteur x/7), liste avec PV et [DEAD], fiche par garde (rôle, garde-robe, voix, armes, loadouts, Heal, Bring, Hold, Delete), Heal/Refill/Revive/Arm All, presets de stats, Cleanup Dead, Dismiss All. |
| **Escouades** | 7 escouades par défaut (Police, Military, FIB, Black Ops, Michael, Trevor, Franklin), éditables/résettables, persistées dans `Squads.xml`, noms uniques (`Police #1`, `#2`, ...). |
| **Escorte véhicule** | Sièges du joueur d'abord, cascade de véhicules spawnés (max 4, anti-empilement), Drivers réservés au volant, reboard après combat, godmode véhicule, catch-up teleport (voitures + gardes à pied, jamais l'hélico), realistic boarding, hélicoptère d'escorte avec atterrissage de courtoisie, blips véhicules, purge des véhicules vides (grâce 12 s, immédiate sur Clear). |
| **Combat** | 5 modes (Off / Player Target / Retaliate / Wanted Threats / Full Protection), arbitre de cibles par priorité, anti-bégaiement, exclusions anti-tir-allié (joueur, groupe, gardes, relations amicales, véhicule à occupant allié), drive-by en véhicule, relâchement doux des tâches. |
| **Tactique** | Attack My Target, Cease Fire (~5 s de suppression), Hold Position / Follow Me (global et par garde, refusé si escorte). |
| **Medic** | Réanime/soigne les gardes (<40 % PV ou morts), anim CPR ~4 s, timeout 20 s, option soin du joueur (<50 %). |
| **Chauffeur** | Drive Me To Waypoint / Around, préférence rôle Driver, véhicule du joueur ou spawné (modèle dédié ou "Same As Escort"), hélico avec atterrissage unique, warp ou entrée réaliste du joueur, filet de ré-émission de la tâche de conduite, arrêt propre + nettoyage du véhicule. |
| **HUD** | Panneau bas-droite par garde : avatar (headshot, rafraîchi ~10 s sans clignotement), nom + arme courante, barres vie/armure, mort grisé ; masqué en cutscene/pause ; toggle persisté. |
| **Wanted** | Page "Wanted Options" (depuis Player Options) : set/clear/max/fake/lock (réutilise `selfFreezeWantedLevel`), lock persisté et ré-armé au chargement. |
| **Divers** | Voix contextuelles (throttle 3 s), formation + espacement persistés (application différée sans native dans la lecture config), blips de mort (sprite 274). |

## 2. Cohérence transversale — arbitrage des systèmes (résultat de la matrice)

Neuf systèmes donnent des ordres aux gardes. Garde-fous **vérifiés présents** :

- Combat ne taske jamais un garde occupé (chauffeur/medic) : `BodyguardCombat.cpp:199` ; Cease Fire les épargne : `BodyguardCombat.cpp:525`.
- L'escorte n'affecte jamais un occupé/Hold : `BodyguardEscort.cpp:736` (assign), `:681` (hélico) ; le nettoyage d'état orphelin les épargne : `BodyguardEscort.cpp:958-963`.
- Catch-up à pied épargne Hold, occupés et combattants : `BodyguardEscort.cpp:1085-1093` ; jamais de warp d'hélico : `:913-917`.
- Le Medic ne choisit jamais un garde escorte/Hold/chauffeur comme soignant : `BodyguardTick.cpp:109-110, :131-132`.
- Hold refusé si escorte : `BodyguardTick.cpp:315-319` ; le tick Hold ne relance pas le scénario pendant un combat ou si déjà en scénario : `:284-290`.
- Le chauffeur n'est jamais choisi parmi escorte/Hold : `BodyguardChauffeur.cpp:172`.
- Cycle de vie : `DeleteBodyguard` / `CleanupDeadBodyguards` / `DismissAllBodyguards` stoppent le chauffeur si concerné et appellent `ClearEscortState` (`BodyguardManagement.cpp:132-134, :184-186, :209+218`) ; `ClearEscortState` remet tous les champs runtime à zéro (`BodyguardEscort.cpp:1108-1139`) ; Dismiss All purge aussi les véhicules d'escorte et tous les headshots HUD (`:243-244`).
- HUD : slot libéré dès qu'un ped n'est plus tracké (`BodyguardHud.cpp:92-96`), libération totale sur Dismiss/toggle OFF.
- Blips véhicules retirés avant chaque suppression (4 chemins dans `BodyguardEscort.cpp`) ; blip chauffeur retiré dans `StopChauffeur` (`BodyguardChauffeur.cpp:441`).

**Défauts trouvés (listés, NON corrigés — voir §4).**

## 3. Tick global (`TickBodyguards`, `BodyguardTick.cpp:376-448`)

Ordre vérifié sain : HUD **chaque frame** (dessin léger, maintenance headshots throttlée à 500 ms) →
formation différée (une fois) → chauffeur 500 ms → early-out DB vide (avec purge escorte maintenue) →
blips 400 ms → combat chaque frame (scans internes throttlés 200/500/700 ms) → medic 1 s → hold 2 s →
escorte + catch-up 750 ms. Aucun sous-système lourd hors bloc throttlé.

## 4. Risques résiduels (classés par sévérité)

| # | Sévérité | Description | Où |
|---|----------|-------------|-----|
| 1 | **Significatif** | `PickChauffeur` n'exclut pas un Medic en plein soin (`IsMedicBusy`). Si le medic en soin est choisi comme chauffeur, `TickMedic` ré-émet `TASK_GO_TO_ENTITY` par-dessus la conduite (`BodyguardTick.cpp:251-254`) puis, au timeout de 20 s, `ResetMedic(true)` le remet dans le groupe vanilla **en pleine conduite** (`:145-158`). Fix d'une ligne à prévoir dans `BodyguardChauffeur.cpp:167-181`. Probabilité faible (fenêtre de ~20 s + il faut que le medic soit le candidat retenu). | `BodyguardChauffeur.cpp:172` |
| 2 | Moyen | `ReleaseFinishedCombatTasks` ne saute pas les gardes occupés (asymétrie avec `CeaseFireAll`) : un garde `CombatTasked` devenu chauffeur subit un `CLEAR_PED_TASKS` quand la cible disparaît. Auto-réparé en ≤500 ms par le filet chauffeur, donc impact limité à un à-coup ; `StartChauffeur` ne remet pas non plus `CombatTasked=false`. | `BodyguardCombat.cpp:252-297` |
| 3 | Moyen | "Bring Bodyguards To Self" (global) et "Bring Bodyguard To Self" (fiche) téléportent sans exclure chauffeur actif, medic en soin ou gardes assis en escorte ; `SET_ENTITY_COORDS_NO_OFFSET` sur un ped assis peut déplacer son véhicule. Les systèmes se rattrapent (reboard/filet), mais l'effet peut être brutal — à observer en jeu. | `BodyguardMenu.cpp:574-615`, `BodyguardSubmenu.cpp:130-152` |
| 4 | Petit | Hold Position ne mémorise pas de point d'ancrage : après un combat (ou un Bring), le garde reprend son scénario **là où il se trouve**, pas à son poste d'origine. | `BodyguardTick.cpp:273-296` |
| 5 | Petit | Heal individuel (fiche) ne réanime pas un garde "dead or dying" (pas de `RESURRECT_PED`), contrairement à Heal All. | `BodyguardSubmenu.cpp:118-128` |
| 6 | Petit | `default_health` / `default_armor` sans clamp (l'InputBox accepte un négatif ; la config ne borne pas). | `BodyguardConfig.cpp:58-59`, `BodyguardSettings.cpp:158-184` |
| 7 | Petit | La map `s_lastVehicleWarpAt` du catch-up ne purge jamais ses entrées (croissance négligeable en session). | `BodyguardEscort.cpp:906` |
| 8 | Cosmétique | HUD : "Unarmed" et les noms d'armes ne passent pas par la traduction (drawstring brut) ; le blip legacy (`blipIcon`) n'est pas persisté. | `BodyguardHud.cpp:137-143` |
| 9 | Déjà connu | Atterrissages hélico (chauffeur + escorte) sans timeout si le jeu ne trouve pas de zone plate ; filet chauffeur basé sur `GET_SCRIPT_TASK_STATUS == 7` à confirmer en conditions réelles ; Cease Fire ne suspend que 5 s en Full Protection. | (revue plan 04) |

## 5. Configuration `[bodyguards]` — tableau final (30 clés)

Lecture/écriture **parfaitement symétriques** dans `BodyguardConfig.cpp` ; aucune clé fantôme, aucun réglage
de menu non persisté (hors choix volontairement de session : preset sélectionné, blip legacy, niveaux Set/Fake wanted).

| Clé | Type | Défaut | Clamp lecture |
|-----|------|--------|---------------|
| `default_spawn_role` | string | `Rifleman` | clé inconnue → Rifleman |
| `role_blips_enabled` | bool | true | — |
| `hud_enabled` | bool | true | — |
| `escort_use_my_seats_first` | bool | true | — |
| `escort_spawn_if_full` | bool | false | — |
| `escort_auto_assign` | bool | false | — |
| `escort_reboard_after_combat` | bool | true | — |
| `escort_vehicle_godmode` | bool | false | — |
| `escort_catchup_teleport` | bool | true | — |
| `escort_realistic_boarding` | bool | false | — |
| `escort_vehicle_model` | string | `police` | validé à l'usage |
| `escort_driving_style_index` | int | 0 | borné à l'usage |
| `escort_heli_model` | string | `buzzard2` | validé à l'usage (doit être un hélico) |
| `chauffeur_vehicle_model` | string | `""` (= Same As Escort) | validé à l'usage |
| `chauffeur_warp_player` | bool | true | — |
| `combat_response_mode` | int | 4 | 0..4 sinon 4 |
| `combat_wanted_min_stars` | int | 2 | 1..5 sinon 2 |
| `medic_enabled` | bool | true | — |
| `medic_heal_player` | bool | false | — |
| `voice_lines_enabled` | bool | true | — |
| `default_health` | int | 200 | aucun (risque §4.6) |
| `default_armor` | int | 200 | aucun (risque §4.6) |
| `default_godmode` | bool | true | — |
| `auto_arm_new` | bool | false | — |
| `spawn_weapon_index` | int | 0 | 0..9 sinon 0 |
| `arm_weapon_index` | int | 0 | 0..8 sinon 0 |
| `wanted_lock_enabled` | bool | false | ré-arme `selfFreezeWantedLevel` |
| `wanted_lock_level` | int | 0 | appliqué tel quel (menu borne 0..5) |
| `formation_index` | int | 0 | 0..3 sinon 0, application différée |
| `formation_spacing` | int | 0 | 0..10 sinon 0, application différée |

`ReadBodyguardConfig` n'appelle **aucune native** (formation + wanted lock différés/ré-armés) et charge les
escouades (`LoadSquads`, pur fichier). Invariants respectés : aucun handle/timestamp runtime persisté,
aucun ID `submenu_enum.h` réordonné, spawn uniquement via `SpawnBodyguardPed`.

## 6. French.json

- **JSON valide, 0 doublon, 440 clés.**
- Croisement code → JSON sur tous les fichiers `Bodyguard*` (147 libellés vérifiés) : 2 clés manquantes
  trouvées et **ajoutées** (`No member selected`, `Squad Member` — libellés de repli de l'éditeur de membre).
- Aucun accent codé en dur dans le code ; les modèles/armes/noms propres restent techniques (non traduits).

## 7. Check-list de test en jeu (ordonnée du plus critique au plus cosmétique)

À dérouler à la prochaine session GTA (copier `Menyoo.asi` + `French.json` dans le dossier GTA, jeu fermé, d'abord).

**Cœur (bloquant si cassé)**
1. Spawner 3 gardes : compteur `x/7`, blips de rôle corrects, toast FR ; 8e spawn refusé proprement.
2. Delete individuel (liste + fiche), Cleanup Dead, Dismiss All : compteur, blips et HUD toujours cohérents, aucun garde fantôme, aucun véhicule d'escorte orphelin.
3. Combat Full Protection : viser un piéton → l'escouade attaque ; viser un garde/allié → aucune réaction ; se faire attaquer → riposte ; étoiles ≥ 2 → les gardes engagent la police, à 1 étoile → non.
4. Cease Fire : le combat cesse ~5 s ; **le chauffeur en course ne s'arrête pas** ; le medic finit son soin.
5. Medic : blesser un garde (<40 % PV) → le medic vient, anim CPR ~4 s, soin ; tuer un garde → réanimation ; medic bloqué → abandon à 20 s puis retour au groupe ; "Medic Heals Player" ON → vous soigne sous 50 %.
6. HUD bas-droite : avatar + nom + arme + barres vie/armure par garde ; mort → ligne grisée ; pas de clignotement d'avatar avec 7 gardes (attendre >10 s) ; toggle OFF → disparition immédiate.

**Escorte**
7. "Use My Seats First" : en voiture, Assign → les gardes prennent vos sièges sans en sortir spontanément.
8. 7 gardes + "Spawn If Full" : cascade de véhicules (max 4) sans empilement, Drivers au volant.
9. Conduite : le convoi suit sans à-coups (escorte en véhicule, follow à pied) ; détruire un véhicule d'escorte → gardes relâchés, épave supprimée.
10. Changer de voiture (auto-assign ON) : réaffectation après ~2 s ; l'ancienne voiture est quittée.
11. Reboard : faire sortir les gardes (combat), fin du combat → ils remontent ; warp après échecs répétés.
12. Catch-up : semer le convoi (>150 m, hors écran) → il réapparaît derrière vous sur une route ; l'hélico n'est JAMAIS téléporté ; un garde Hold n'est jamais rapatrié.
13. Clear Escort Assignments : tout le monde descend, véhicules spawnés supprimés dès qu'ils sont vides (sans attendre 12 s), votre véhicule perso intact.
14. Hélico d'escorte : spawn, suit en vol sans saccade, atterrit près de vous après ~8 s à pied ; **surveiller** un éventuel vol stationnaire sans fin (pas de timeout).

**Chauffeur**
15. Drive Me To Waypoint : Driver préféré, conduite au point, toast "Arrived" ; déplacement du waypoint en route → il suit le nouveau.
16. Warp Into Vehicle OFF → vous montez à pied ; ON → warp direct ; Stop Chauffeur → frein, garde revient au groupe, véhicule spawné supprimé une fois vide.
17. Chauffeur hélico : vol + atterrissage au waypoint (émis une fois) ; Drive Me Around : errance, re-tâche ~30 s.
18. Chauffeur × combat : se faire attaquer pendant la course → la voiture continue (filet ≤500 ms) ; le chauffeur meurt → mode stoppé proprement avec toast.

**Tactique / divers**
19. Hold Position (fiche) : le garde reste en faction (l'anim ne redémarre pas toutes les 2 s) ; il se défend si attaqué puis reprend la faction (note : à l'endroit du combat, pas à son poste — connu) ; Hold refusé pour un garde d'escorte (toast rouge).
20. Hold Positions (All) / Follow Me (All) : bascule globale, gardes occupés non cassés.
21. Attack My Target : fonctionne même Combat Response Off ; cible alliée refusée ("No target" / rien).
22. Revive Dead : morts relevés, corps recyclés comptés "bodies missing".
23. Wanted Options (Player Options) : Set/Clear/Force Max/Fake ; Lock Level 2 + Lock ON → tient après un Set/Clear ; Never Wanted domine ; le lock survit à un redémarrage.
24. Squads : Police ×2 → `Police #1`..`#6` ; éditer rôle/compte d'un membre, créer une escouade, reset ; vérifier `Squads.xml` (pas de handles).
25. Persistance : modifier rôle par défaut, HUD, réglages escorte, mode combat, min stars, formation + espacement, medic, voix, modèles → redémarrer → tout restauré, formation ré-appliquée à la marche.
26. Bring To Self (global + fiche) **avec convoi/chauffeur actifs** : observer le comportement (risque §4.3 — véhicules potentiellement déplacés) ; français de tous les toasts au passage.
27. Voix : répliques aux ordres/à une mort, pas de spam (throttle 3 s) ; toggle OFF = silence total.

---

*Corrections mineures appliquées lors de cette revue : 2 clés ajoutées à `French.json` ; section Chauffeur de `docs/Bodyguards.md` complétée (Warp Into Vehicle, atterrissage émis une fois, filet de ré-émission). Aucun code modifié.*

---

## Addendum (2026-07-05, boucle principale) — découverte n°1 corrigée

Le défaut SIGNIFICATIF n°1 (le mode Chauffeur pouvait choisir le Medic en pleine réanimation) est **corrigé** : `PickChauffeur` (`BodyguardChauffeur.cpp:172`) exclut désormais aussi `IsMedicBusy(...)` (helper déjà exporté par `BodyguardTick.h`). Rebuild MSBuild Release x64 : réussi, 0 erreur.

Restent listés (non corrigés, à traiter si gênants en jeu) : l'asymétrie de relâchement combat n°2 (auto-réparée en ≤500 ms par le filet chauffeur), Bring To Self sans exclusions n°3, et les petits n°4.
