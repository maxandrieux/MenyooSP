# Plan 1 — Optimisation du menu Bodyguards (clarté / efficacité)

> Basé sur la section « menu » de l'audit du 2026-07, vérifié contre le code **post-nettoyage Mission 0** (2026-07-04).
> Aucune logique combat / escorte / tick n'est modifiée par ce plan : uniquement libellés, feedback, i18n et petites options de menu.

---

## Objectif et périmètre

Rendre le menu Bodyguards utilisable à 100 % dans la langue choisie par l'utilisateur et plus découvrable :

1. Internationaliser tous les **toasts** (petits messages en bas d'écran) aujourd'hui codés en dur en français (P1-1 de l'audit).
2. Afficher le **compteur d'effectif « x/7 »** dans « Bodyguard List » (P3-10).
3. Ajouter un bouton **« Delete Bodyguard »** dans le sous-menu entité (P3-9).
4. Donner des **noms uniques** aux membres d'escouade spawnés (« Police #2 ») (P3-11).
5. Ajouter des actions **individuelles** Heal / Bring dans le sous-menu entité (P3-11).
6. Rendre la **ligne de statut escorte** non cliquable (P3-12).
7. **Déplacer « Wanted Level »** vers Player Options (P2-5).
8. Renommer le titre « Settings » en « Bodyguard Settings » (quick win n°2).
9. Commenter les IDs d'enum orphelins (P4-13).

### Déjà fait (Mission 0 — ne PAS replanifier)

| Recommandation audit | Statut |
|---|---|
| P1-2 « Equipment Presets » → « Stat Presets » | ✅ fait |
| P1-3 regroupement armement (Auto-Arm + Spawn Weapon dans Settings → Spawn Defaults, libellé « overrides role weapon », preset ne touche plus l'auto-arm) | ✅ fait |
| P1-4 « Bodyguard Blip » legacy masqué quand Role Blips est ON | ✅ fait |
| P2-6 « Squad Tools » → « Active Bodyguards », remonté sous « Bodyguard List » | ✅ fait |
| P2-7 réglages de spawn regroupés dans « Spawn Defaults » | ✅ fait |
| P2-8 formation persistée (`formation_index`, application différée au tick) | ✅ fait |
| P4-14 clés mortes French.json supprimées (`Squad Tools`, `Equipment Presets`, `Bodyguard Settings`…) | ✅ fait |
| Quick win n°1 clé `"Bodyguard Options"` ajoutée | ✅ fait |

---

## Rappel technique clé (à lire avant l'étape 1)

`Game::Print::PrintBottomLeft(std::string)` et `PrintBottomCentre` passent déjà la chaîne **entière** par `Language::TranslateToSelected` (`Scripting/Game.cpp:301` et `:339`). La traduction est un **match exact** sur toute la chaîne (`Language.cpp:35-50`). Conséquences :

- **Toast statique** → il suffit de mettre la chaîne **anglaise** dans le code + une entrée dans `French.json`. Rien d'autre.
- **Toast dynamique** (compteur, nom) → la concaténation ne matchera jamais ; il faut traduire le **préfixe constant soi-même** puis concaténer, comme le fait déjà `BodyguardMenu.cpp:454` :
  ```cpp
  Game::Print::PrintBottomLeft(Language::TranslateToSelected("Bodyguards armed: ") + std::to_string(n));
  ```
  (le second passage par `TranslateToSelected` dans `PrintBottomLeft` ne trouvera pas la chaîne complète et la renverra telle quelle — comportement voulu ; ça produit une ligne « Missing translation » unique par variante dans le log, bruit acceptable et déjà présent pour la ligne preset).
- Les codes couleur `~r~` doivent rester **hors** de la clé : `"~r~" + Language::TranslateToSelected("Invalid vehicle model")`.
- Inclure `"../../Menu/Language.h"` dans les fichiers qui ne l'ont pas encore (seul `BodyguardMenu.cpp` l'a aujourd'hui).

---

## Étapes

### Étape 1 — Internationaliser tous les toasts (P1-1) — **moyen/gros (mécanique)**

Remplacer chaque chaîne française codée en dur par sa clé anglaise (statique) ou par le motif préfixe traduit + valeur (dynamique). Inventaire exhaustif vérifié dans le code actuel :

**`BodyguardSpawn.cpp`** (ajouter l'include `Language.h`) :
- l.68 et l.149 `"Maximum de 7 gardes du corps atteint."` → `Language::TranslateToSelected("Maximum number of bodyguards reached") + " (" + std::to_string(MAX_BODYGUARDS) + ")"` (au passage, le « 7 » n'est plus en dur — `MAX_BODYGUARDS` vient de `BodyguardSpawn.h:34`).
- l.77 `"Impossible de charger le modèle."` → `"Could not load model."` (statique).
- l.132 `"Garde du corps créé"` → `"Bodyguard spawned"` (statique).

**`BodyguardMenu.cpp`** :
- l.391 → `TranslateToSelected("Bodyguards armed: ") + std::to_string(n)`
- l.402 → `TranslateToSelected("Bodyguards healed: ")` + n
- l.411 → `TranslateToSelected("Armor refilled: ")` + n
- l.420 → `TranslateToSelected("Bodyguards revived: ")` + n
- l.454 → `TranslateToSelected("Preset '") + TranslateToSelected(p.label) + TranslateToSelected("' applied: ") + std::to_string(n)`
- l.468 → `TranslateToSelected("Dead or missing bodyguards removed: ")` + n
- l.476 → `"All bodyguards dismissed."` (statique)
- l.536 → `"Bodyguards teleported."` (statique)

**`BodyguardSquads.cpp`** (ajouter l'include `Language.h`) :
- l.240 → `TranslateToSelected("Squad members spawned: ")` + spawned
- l.265 → `"Squad name cannot be empty."` — l.269 → `"Invalid squad name."` — l.273 → `"A squad with that name already exists."` (statiques)
- l.282 → `TranslateToSelected("Squad created: ") + name` (le nom d'escouade n'est jamais traduit)
- l.291 → `"All squads have been reset."` — l.361 → `"Squad reset."` — l.423 → `"Squad member removed."` (statiques)
- l.333 → `"Model name cannot be empty."` — l.341 → `"Unknown ped model."` (statiques)
- l.351 → `TranslateToSelected("Member added: ") + model` (nom de modèle jamais traduit)

**`BodyguardEscort.cpp`** (ajouter l'include `Language.h`) :
- l.520 → `"No bodyguards to assign"` — l.568 → `"No free escort seats"` — l.772 → `"Escort model set"` (statiques)
- l.561 et l.776 → `"~r~" + TranslateToSelected("Invalid vehicle model")`
- l.570 / l.572 → `TranslateToSelected("Bodyguards assigned: ") + std::to_string(assigned) + "/" + std::to_string(initialPending)` (préfixer `"~r~"` pour la variante l.570)
- l.725 → `TranslateToSelected("Escort assignments cleared: ")` + cleared

**`BodyguardSettings.cpp`** (ajouter l'include `Language.h`) :
- l.148 et l.162 → `"~r~" + TranslateToSelected("Invalid input: ") + inputStr`
- l.86 et l.91 : les libellés de touche `Menu::add_IB(..., "Supprimer le garde")` → passer la clé anglaise existante `"Delete Bodyguard"`. **Vérifier d'abord** si `Menu::add_IB` traduit son texte (chercher `TranslateToSelected` dans son implémentation, `Menu.cpp`) ; sinon envelopper : `Menu::add_IB(VirtualKey::B, Language::TranslateToSelected("Delete Bodyguard"))`.

**Nouvelles entrées `French.json`** (bloc bodyguard existant, ordre alphabétique du fichier respecté) :

```json
"' applied: ": "' appliqué : ",
"A squad with that name already exists.": "Une escouade porte déjà ce nom.",
"All bodyguards dismissed.": "Tous les gardes du corps ont été renvoyés.",
"All squads have been reset.": "Toutes les escouades ont été réinitialisées.",
"Armor refilled: ": "Armures restaurées : ",
"Bodyguard spawned": "Garde du corps créé",
"Bodyguards armed: ": "Gardes du corps armés : ",
"Bodyguards assigned: ": "Gardes affectés : ",
"Bodyguards healed: ": "Gardes du corps soignés : ",
"Bodyguards revived: ": "Gardes ranimés : ",
"Bodyguards teleported.": "Gardes du corps téléportés.",
"Could not load model.": "Impossible de charger le modèle.",
"Dead or missing bodyguards removed: ": "Gardes morts ou manquants retirés : ",
"Escort assignments cleared: ": "Affectations d'escorte réinitialisées : ",
"Escort model set": "Modèle d'escorte défini",
"Invalid input: ": "Entrée invalide : ",
"Invalid squad name.": "Nom d'escouade invalide.",
"Invalid vehicle model": "Modèle de véhicule invalide",
"Maximum number of bodyguards reached": "Nombre maximum de gardes du corps atteint",
"Member added: ": "Membre ajouté : ",
"Model name cannot be empty.": "Le nom du modèle ne peut pas être vide.",
"No bodyguards to assign": "Aucun garde du corps à affecter",
"No free escort seats": "Aucun siège libre pour l'escorte",
"Preset '": "Préréglage '",
"Squad created: ": "Escouade créée : ",
"Squad member removed.": "Membre d'escouade retiré.",
"Squad members spawned: ": "Membres d'escouade créés : ",
"Squad name cannot be empty.": "Le nom de l'escouade ne peut pas être vide.",
"Squad reset.": "Escouade réinitialisée.",
"Unknown ped model.": "Modèle de PNJ inconnu."
```

(Attention aux **espaces finaux** dans les clés-préfixes : ils font partie du match exact. Valider le JSON après édition : `python -c "import json;json.load(open('...French.json',encoding='utf-8-sig'))"`.)

Natives : aucune nouvelle.

### Étape 2 — Compteur « x/7 » dans Bodyguard List — **petit**

`BodyguardSettings.cpp`, `BodyguardList()` l.49 :

```cpp
const size_t alive = BodyguardDb.size(); // ou count des handles existants
AddTitle(Language::TranslateToSelected("Bodyguard List") + " (" +
         std::to_string(alive) + "/" + std::to_string(BodyguardManagement::MAX_BODYGUARDS) + ")");
```

- Inclure `BodyguardSpawn.h` (déjà inclus l.11) pour `MAX_BODYGUARDS`.
- `AddTitle` traduit lui-même sa chaîne (match exact) : comme la chaîne composée ne matchera pas, on traduit le préfixe manuellement — même motif que les toasts dynamiques.
- La clé `"Bodyguard List"` existe déjà dans `French.json` (entrée du menu principal) : rien à ajouter.
- Optionnel : même traitement sur le titre « Active Bodyguards » (`BodyguardMenu.cpp:375`) avec le nombre de gardes vivants.

### Étape 3 — Bouton « Delete Bodyguard » dans le sous-menu entité — **petit**

`BodyguardSubmenu.cpp`, `BodyguardEntityOps()`, après « Loadouts » (l.103) :

```cpp
bool bDelete = false;
AddOption("Delete Bodyguard", bDelete);
if (bDelete)
{
    sub::BodyguardMenu::BodyguardManagement::DeleteBodyguard(*SelectedBodyguard);
    g_selectedBodyguardHandle = 0;      // évite le titre "Bodyguard (missing)" fugace
    Menu::SetPreviousMenu();            // retour à la liste (Menu.cpp:616)
}
return; // ne rien rendre d'autre après la suppression sur cette frame
```

- `DeleteBodyguard` (BodyguardManagement.cpp) maintient déjà l'invariant BodyguardDb + `s_bodyguards` — ne surtout pas réécrire la suppression.
- La clé `"Delete Bodyguard"` existe déjà (`French.json:315`).
- Placer l'option **en dernier** et idéalement derrière un `AddBreak("--- Danger ---")` pour éviter un clic accidentel (nouvelle clé `"--- Danger ---": "--- Danger ---"` facultative — sinon pas de break).

### Étape 4 — Noms uniques pour les membres d'escouade — **petit**

`BodyguardSquads.cpp`, `SpawnSquad()` (l.221-241) : suffixer un index au nom passé à `SpawnBodyguardPed` :

```cpp
int memberIdx = 0; // par session de spawn de cette escouade
for (const auto& m : def.members)
    for (int i = 0; i < m.count; ++i)
    {
        ...
        ++memberIdx;
        std::string bgName = def.name + " #" + std::to_string(memberIdx);
        Ped ped = BodyguardManagement::SpawnBodyguardPed(model, bgName, m.role);
        ...
    }
```

- Pour éviter les collisions quand on respawne la même escouade (« Police #1 » deux fois), démarrer `memberIdx` à `1 + nombre de gardes déjà présents dont Name commence par def.name + " #"` (simple boucle sur `BodyguardDb`).
- `BodyguardEntity::Name` n'est utilisé que pour l'affichage (liste + titre du sous-menu entité) — aucune logique ne compare les noms de gardes, donc aucun risque sur squads/escorte/combat. Les noms ne sont jamais traduits.
- Le spawn classique (hors escouade) garde le libellé du modèle, inchangé.

### Étape 5 — Actions individuelles Heal / Bring dans le sous-menu entité — **petit/moyen**

`BodyguardSubmenu.cpp`, `BodyguardEntityOps()`, entre « Loadouts » et « Delete Bodyguard » :

```cpp
AddBreak("--- Actions ---"); // clé déjà traduite (utilisée dans le menu principal)

bool bHeal = false;
AddOption("Heal Bodyguard", bHeal);
if (bHeal)
{
    Ped ped = SelectedBodyguard->Handle.GetHandle();
    SelectedBodyguard->Handle.RequestControl();
    ENTITY::SET_ENTITY_MAX_HEALTH(ped, sub::BodyguardMenu::health);
    ENTITY::SET_ENTITY_HEALTH(ped, sub::BodyguardMenu::health, 0);
    PED::SET_PED_ARMOUR(ped, sub::BodyguardMenu::armor);
    Game::Print::PrintBottomLeft("Bodyguard healed");
}

bool bBring = false;
AddOption("Bring Bodyguard To Self", bBring);
if (bBring)
{
    // Même téléport que "Bring Bodyguards To Self" (BodyguardMenu.cpp:499-537), pour 1 seul ped :
    // playerPos + forward * 3.0 + (0,0,0.2), via ENTITY::SET_ENTITY_COORDS_NO_OFFSET.
}
```

- Extern nécessaires : `health` / `armor` sont déclarés dans `BodyguardMenu.h` (vérifier ; sinon `extern` local comme le fait `BodyguardSpawn.cpp:31-33`).
- Includes à ajouter dans `BodyguardSubmenu.cpp` : `Game.h` (toasts) — `natives.h` est déjà là.
- Natives : `SET_ENTITY_MAX_HEALTH`, `SET_ENTITY_HEALTH`, `SET_PED_ARMOUR`, `GET_ENTITY_COORDS`, `GET_ENTITY_FORWARD_VECTOR`, `SET_ENTITY_COORDS_NO_OFFSET` — toutes déjà utilisées ailleurs dans le module.
- **Piège escorte** : « Bring » sur un garde assis dans un véhicule d'escorte le sort du siège ; c'est le comportement existant de « Bring Bodyguards To Self » (qui téléporte tout le monde sans état d'âme), donc cohérent. Ne PAS toucher `EscortSeat`/`EscortVehicle` : le tick escorte (reboard) le fera remonter tout seul si « Reboard After Combat » est actif — mentionner ce comportement dans docs/Bodyguards.md.
- Nouvelles clés `French.json` :
  ```json
  "Heal Bodyguard": "Soigner le garde",
  "Bring Bodyguard To Self": "Rappeler ce garde à soi",
  "Bodyguard healed": "Garde soigné",
  "Bodyguard teleported": "Garde téléporté"
  ```

### Étape 6 — Ligne de statut escorte non cliquable + traduite — **petit**

`BodyguardEscort.cpp:713` : remplacer l'`AddOption` inerte par un `AddBreak` (même rendu « texte non sélectionnable » que la ligne d'aide de `BodyguardWanted.cpp:87`) :

```cpp
AddBreak(Language::TranslateToSelected("Assigned: ")
         + std::to_string(assigned) + "/" + std::to_string(alive)
         + " - " + g_escortVehicleModel);
```

- Nouvelle clé : `"Assigned: ": "Affectés : "` (le nom de modèle reste brut, jamais traduit).
- Vérifier visuellement en jeu que `AddBreak` accepte une chaîne dynamique (il est déjà utilisé avec une longue phrase dans Wanted — oui).

### Étape 7 — Déplacer « Wanted Level » vers Player Options — **petit**

Choix retenu (P2-5) : **déplacer l'entrée**, pas le code (le sous-menu `BODYGUARD_WANTED` et son enum ne bougent pas — invariant « never reorder »).

1. `BodyguardMenu.cpp:494` : supprimer `AddOption("Wanted Level", ..., SUB::BODYGUARD_WANTED);` du menu Bodyguards.
2. `Solution/source/Submenus/PlayerOptions.cpp` : près du toggle « Never Wanted » (l.124), ajouter :
   ```cpp
   AddOption("Wanted Options", null, nullFunc, SUB::BODYGUARD_WANTED);
   ```
   (repérer la section « wanted » existante de Player Options et s'y insérer proprement — lire le contexte autour de l.124 avant d'éditer).
3. Nouvelle clé : `"Wanted Options": "Options de recherche"`.
4. `docs/Bodyguards.md` §6 : noter que la page est désormais atteinte via Player Options (le fichier source et l'enum restent bodyguard pour la stabilité).

Alternative moins risquée si on veut éviter de toucher `PlayerOptions.cpp` : garder l'entrée dans Bodyguards mais la renommer `"Wanted Level (Player)"`. À décider au moment de l'implémentation ; le déplacement est recommandé.

### Étape 8 — Titre « Settings » → « Bodyguard Settings » — **petit**

- `BodyguardSettings.cpp:134` : `AddTitle("Settings")` → `AddTitle("Bodyguard Settings")`.
- Ré-ajouter la clé (supprimée en Mission 0 car morte — elle redevient vivante) : `"Bodyguard Settings": "Réglages des gardes du corps"`.
- L'entrée du menu Bodyguards reste « Settings » (le contexte suffit et la clé globale `"Settings"` existe déjà) ; seul le **titre** du sous-menu change pour ne plus se confondre avec le Settings global de Menyoo.

### Étape 9 — Commenter les IDs d'enum orphelins (P4-13) — **petit**

`Solution/source/Menu/submenu_enum.h` : ajouter un commentaire (aucun changement de valeur, aucun réordonnancement) :

```cpp
BODYGUARDOPS,        // unused, kept for stability (never reorder)
...
BODYGUARD_MAIN,      // unused, kept for stability (never reorder)
...
BODYGUARD_WARDROBE,  // unused, kept for stability (never reorder)
```

(`BODYGUARD_SQUAD_MAINTENANCE` / `MANAGE_SQUAD` / `SQUAD_PRESETS` sont déjà documentés par le commentaire de `BodyguardMenu.cpp:548-550` ; on peut dupliquer la note dans l'enum pour la trouver sur place.)

### Étape 10 — Documentation, validation, build — **petit**

1. `docs/Bodyguards.md` : §2 (compteur de liste, actions individuelles, Delete), §4 (ligne de statut non cliquable), §6 (accès via Player Options), §12 (nouveaux points de smoke test ci-dessous).
2. Valider `French.json` (script Python json.load) — aucune clé dupliquée, espaces finaux préservés.
3. Build : MSBuild Release x64 incrémental (aucun nouveau fichier → pas de premake). Zéro nouvel avertissement attendu.

---

## Risques et pièges

- **Invariant DB** : toute suppression passe par `DeleteBodyguard` / `RemoveBodyguardByHandle` (jamais d'`erase` manuel) ; tout spawn par `SpawnBodyguardPed`. Les étapes 3-5 ne créent aucun nouveau chemin.
- **Traduction = match exact** : ne pas oublier les espaces finaux dans les clés-préfixes ; garder `~r~` hors des clés ; ne jamais traduire noms d'escouades, noms de gardes, modèles.
- **Double passage `TranslateToSelected`** sur les toasts dynamiques : chaque variante inédite ajoute une ligne « Missing translation » au log et une entrée au cache. Bénin (comportement déjà présent), mais ne pas « corriger » en supprimant le log global.
- **`Menu::SetPreviousMenu()` après Delete** : appeler puis `return` immédiatement — ne pas continuer à rendre des options du sous-menu sur la même frame. `GetSelectedBodyguard()` tolère déjà un handle perdu, mais remettre `g_selectedBodyguardHandle = 0` évite le log SELECTION_LOST.
- **Bring individuel vs escorte** : la téléportation d'un garde assigné le fait sortir du véhicule ; le reboard du tick le rattrape. Ne pas réinitialiser son état escorte à la main.
- **Étape 7** : `PlayerOptions.cpp` est hors du périmètre bodyguard — modification d'une seule ligne, ne rien réordonner autour ; l'enum `BODYGUARD_WANTED` ne bouge pas.
- **Aucune nouvelle clé de config** `[bodyguards]` dans ce plan (rien de nouveau à persister).

## Nouvelles clés French.json (récapitulatif)

Étape 1 : les 30 clés listées à l'étape 1. Étape 5 : `Heal Bodyguard`, `Bring Bodyguard To Self`, `Bodyguard healed`, `Bodyguard teleported`. Étape 6 : `"Assigned: "`. Étape 7 : `Wanted Options`. Étape 8 : `Bodyguard Settings`. (Étapes 2-4, 9-10 : aucune.)

## Nouvelles clés de config `[bodyguards]`

Aucune.

---

## Critères de test en jeu (checklist utilisateur)

1. **Toasts FR** (langue = Français) : spawner un garde (« Garde du corps créé »), dépasser 7 gardes, Arm All / Heal All / Refill / Revive / Cleanup / Dismiss, appliquer un preset, spawner une escouade, créer/reset une escouade, ajouter un membre invalide (`abc`), assigner/effacer l'escorte, saisir un modèle de véhicule invalide → **tous** les toasts sont en français, avec les bons compteurs.
2. **Toasts EN** (langue = English/défaut) : refaire 3-4 des actions ci-dessus → toasts en anglais, plus aucun mot français à l'écran.
3. **Compteur** : « Bodyguard List (0/7) » vide, « (3/7) » après une escouade de 3, « (7/7) » au max.
4. **Delete individuel** : Bodyguard List → garde → « Delete Bodyguard » → le ped disparaît (avec son blip), retour automatique à la liste, compteur décrémenté ; la touche B/RLEFT dans la liste fonctionne toujours et son libellé d'aide est traduit.
5. **Noms uniques** : spawner l'escouade Police → la liste montre « Police #1 », « Police #2 », « Police #3 » ; respawner Police → « Police #4 »… pas de doublon.
6. **Heal/Bring individuels** : blesser un garde → Heal Bodyguard → vie/armure pleines ; s'éloigner → Bring Bodyguard To Self → il apparaît devant le joueur ; un garde assigné à l'escorte re-monte en voiture au tick suivant (Reboard ON).
7. **Ligne escorte** : « Affectés : 2/3 - police » ne se surligne plus / ne se clique plus ; les valeurs se mettent à jour après Assign/Clear.
8. **Wanted déplacé** : plus d'entrée « Wanted Level » dans Bodyguards ; « Wanted Options » dans Player Options ouvre la même page ; lock au redémarrage toujours fonctionnel (round-trip ini inchangé).
9. **Titre Settings** : le sous-menu affiche « Réglages des gardes du corps » (FR) / « Bodyguard Settings » (EN).
10. **Non-régression** : spawn/squads/escorte/combat/wanted/formation se comportent comme avant (aucune logique modifiée).

## Estimation globale

| Étape | Taille |
|---|---|
| 1 Toasts i18n | moyen/gros (mécanique, ~35 sites, 5 fichiers) |
| 2 Compteur x/7 | petit |
| 3 Delete Bodyguard | petit |
| 4 Noms uniques | petit |
| 5 Heal/Bring individuels | petit/moyen |
| 6 Statut escorte | petit |
| 7 Wanted → Player Options | petit |
| 8 Titre Bodyguard Settings | petit |
| 9 Commentaires enum | petit |
| 10 Docs + build | petit |

Total : **moyen** (une session), dominé par l'étape 1.
