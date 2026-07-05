# Plan 3 — Correction des bugs Bodyguard v2

> Basé sur la section « bugs » de l'audit. **Toutes les références fichier:ligne ci-dessous ont été re-vérifiées contre le code actuel (post-nettoyage « Mission 0 »)** — elles sont exactes au moment de la rédaction.
>
> Rappel de la cascade unique identifiée par l'audit :
> combat → les gardes sortent du véhicule d'escorte → le mod supprime le véhicule « vide » 750 ms plus tard → `TickEscort` voit un handle mort et `continue` sans restaurer le groupe → gardes définitivement hors groupe → `SET_PED_KEEP_TASK(TRUE)` de combat jamais relâché → certains meurent en état « dying » et « Revive Dead » les ignore.

## Objectif et périmètre

Corriger les 6 bugs confirmés, dans l'ordre du moindre risque, sans toucher à la logique fonctionnelle voulue (rôles, formation, wanted lock, config). Fichiers concernés :

- `Solution/source/Submenus/Bodyguards/BodyguardEscort.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardCombat.cpp` / `BodyguardCombat.h`
- `Solution/source/Submenus/Bodyguards/BodyguardMenu.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardManagement.h`
- `Solution/source/Submenus/Bodyguards/BodyguardSettings.cpp`
- `Solution/source/Submenus/Bodyguards/BodyguardConfig.cpp`
- `Solution/source/_Build/bin/Release/menyooStuff/Language/French.json`
- `docs/Bodyguards.md` (mise à jour doc en fin de plan)

Hors périmètre : traduction des toasts FR codés en dur (P1-1), déplacement de « Wanted Level » (P2-5), toute nouvelle feature.

---

## Étape 1 — « Revive Dead » ranime aussi les gardes « en train de mourir » + message « X ranimés, Y introuvables » (PETIT)

**Bug vérifié.** `BodyguardMenu.cpp:301-327` (`ReviveAllDeadBodyguards`) : la ligne 306 saute tout garde pour lequel `IsBodyguardAlive(bg)` est vrai. Or `IsBodyguardAlive` (`BodyguardManagement.h:40-43`) repose sur `IS_ENTITY_DEAD` (`GTAentity.cpp:225`) : un ped **à terre, mortellement blessé mais santé > 0** (« dying ») est compté vivant → jamais ranimé. Et un corps dé-streamé (`!bg.Handle.Exists()`) est sauté **en silence** (même ligne 306) : l'utilisateur voit « Gardes ranimés : 0 » sans explication.

**Fichier / symboles :** `BodyguardMenu.cpp` — `ReviveAllDeadBodyguards()` (namespace anonyme) et son appelant dans `BodyguardSquadTools()` (bloc `bRevive`, lignes 415-422).

**Diff attendu (esquisse) :**

```cpp
// Signature : renvoie le nombre ranimé, remplit missing (corps disparus).
int ReviveAllDeadBodyguards(int& missing)
{
    int n = 0;
    missing = 0;
    for (auto& bg : sub::BodyguardMenu::BodyguardDb)
    {
        if (!bg.Handle.Exists())
        {
            ++missing;               // corps recyclé par le moteur : irrécupérable
            continue;
        }

        Ped ped = bg.Handle.GetHandle();
        // "dying" (au sol, fatalement blessé, santé encore > 0) doit être
        // ranimable au même titre que "dead" (IS_ENTITY_DEAD).
        const bool needsRevive = !IsBodyguardAlive(bg) ||
                                 PED::IS_PED_DEAD_OR_DYING(ped, true);
        if (!needsRevive)
            continue;

        bg.Handle.RequestControl();
        PED::RESURRECT_PED(ped);
        TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped);   // inchangé (sort du ragdoll)
        // ... suite inchangée (max health, health, armour, godmode,
        //     SET_PED_AS_GROUP_MEMBER, blip, rôle) ...
        ++n;
    }
    return n;
}
```

Appelant (`bRevive`) :

```cpp
int missing = 0;
int n = ReviveAllDeadBodyguards(missing);
std::string msg = "Gardes ranimés : " + std::to_string(n);
if (missing > 0)
    msg += " ~o~(" + std::to_string(missing) + " corps introuvables)";
Game::Print::PrintBottomLeft(msg);
sub::BodyguardMenu::BodyguardManagement::DbgLogSquadState(
    "REVIVE_DEAD affected=" + std::to_string(n) + " missing=" + std::to_string(missing));
```

Option de confort (recommandée, même TU) : dans `HealAllAliveBodyguards()` (`BodyguardMenu.cpp:272-285`), si `PED::IS_PED_DEAD_OR_DYING(ped, true)` est vrai, appliquer le même traitement RESURRECT + CLEAR_TASKS avant le heal — sinon « Heal All » laisse le garde couché avec des PV pleins.

**Natives utilisées :** `PED::IS_PED_DEAD_OR_DYING`, `PED::RESURRECT_PED`, `TASK::CLEAR_PED_TASKS_IMMEDIATELY` (déjà présentes dans le TU).

**Pourquoi c'est sûr :** fonction appelée uniquement au clic menu (pas dans le tick) ; ne touche ni BodyguardDb (pas d'ajout/suppression, invariant Db/s_bodyguards intact) ni l'escorte. `RESURRECT_PED` sur un ped « dying » est sans effet néfaste (il le remet debout, ce qui est le but). Le toast reste en FR codé en dur, comme tous les autres toasts du TU (cohérent avec l'existant ; P1-1 traité ailleurs).

**Validation en jeu :** blesser mortellement un garde (godmode off, tir jambe/torse) pour qu'il soit au sol **sans** blip « mort » (274) → « Revive Dead » doit afficher ≥ 1 et le garde se relève et suit. Puis tuer un garde, s'éloigner de ~300 m en véhicule 1-2 min (le corps dé-streame), revenir → « Revive Dead » affiche « Gardes ranimés : 0 (1 corps introuvables) ».

---

## Étape 2 — `TickEscort` : nettoyer l'état d'escorte quand le véhicule a disparu (PETIT — cœur de la cascade « ils ne me suivent plus »)

**Bug vérifié.** `BodyguardEscort.cpp:601-602` :

```cpp
if (!bg.EscortVehicle.Exists())
    continue;
```

Si le véhicule d'escorte a été supprimé/détruit/dé-streamé, `Exists()` (= `DOES_ENTITY_EXIST`, `GTAentity.cpp:884`) est faux → `continue` **sans** `ClearEscortState(bg)`. Le garde, sorti du groupe par `RemoveFromPlayerGroupForEscort` (`BodyguardEscort.cpp:182-190`), reste orphelin pour toujours (`RestorePlayerGroupAfterEscort`, lignes 192-206, jamais rappelé). Le cas voisin « handle encore vivant mais plus un véhicule » est, lui, bien traité (lignes 606-610 → `ClearEscortState`). `AssignBodyguardsToEscort` nettoie aussi correctement (ligne 505). Le trou est spécifique à cette branche du tick.

**Fichier / symbole :** `BodyguardEscort.cpp` — `TickEscort()`, boucle par garde.

**Diff attendu :**

```cpp
if (!bg.EscortVehicle.Exists())
{
    // Handle mort (véhicule supprimé/détruit/dé-streamé) alors qu'un état
    // d'escorte est actif : restaurer le groupe au lieu de laisser le garde
    // orphelin. Les gardes jamais affectés (EscortVehicle = 0, EscortSeat = -2,
    // RemovedFromGroup = false) ne déclenchent rien.
    if (bg.RemovedFromGroup || bg.EscortSeat != -2)
        ClearEscortState(bg);
    continue;
}
```

**Piège important :** ne PAS appeler `ClearEscortState` sans condition. Pour un garde jamais affecté, `EscortVehicle` vaut `GTAentity(0)` et `Exists()` est aussi faux ; un appel inconditionnel exécuterait `RestorePlayerGroupAfterEscort` (donc `SET_PED_NEVER_LEAVES_GROUP`) + `DeleteEmptySpawnedEscortVehicles` **par garde, toutes les 750 ms** — spam de natives inutile. La garde `RemovedFromGroup || EscortSeat != -2` distingue « état d'escorte actif » de « jamais affecté ».

**Pourquoi c'est sûr :** `ClearEscortState` (lignes 654-685) est déjà conçu pour ce cas : il teste `bg.EscortVehicle.Exists()` en tête, gère le handle ped invalide, et remet tous les champs à leur valeur par défaut. On réutilise le même chemin que la branche 606-610 existante.

**Validation en jeu :** affecter des gardes à un véhicule d'escorte spawné, provoquer un combat pour qu'ils en sortent, détruire le véhicule à la roquette (ou laisser le bug 3 le supprimer si testé avant l'étape 3) → dans les ~1,5 s, les gardes doivent revenir en formation derrière le joueur (blips inchangés, ils re-suivent à pied). Sans le fix, ils restent plantés/autonomes.

---

## Étape 3 — Délai de grâce avant suppression d'un véhicule d'escorte « vide » (MOYEN — cause racine de la cascade)

**Bug vérifié.** `BodyguardEscort.cpp:128-154` (`DeleteEmptySpawnedEscortVehicles`) : tout véhicule spawné par le mod est supprimé (lignes 143-149 : `SET_ENTITY_AS_MISSION_ENTITY` + `Delete()`) dès que `IsVehiclePhysicallyOccupied` (lignes 112-126) ne trouve personne assis **à cet instant**. Purge appelée toutes les ~750 ms : `TickEscort()` ligne 578, `TickBodyguards()` via `CleanupEscortVehicles()` (`BodyguardEscort.cpp:462-465`, appel `BodyguardTick.cpp:37`), plus `AssignBodyguardsToEscort` (485), `ClearEscortState` (684), `ClearAllEscortAssignments` (699). Or le véhicule est légitimement vide pendant : le combat (les gardes descendent ; `QueueReboardOrClear`, lignes 411-444, attend la fin du combat), la marche de ré-embarquement (`TASK_ENTER_VEHICLE` ligne 439, timeout 3000 ms > période 750 ms), une éjection par le jeu. Le critère ne vérifie jamais `bg.EscortVehicle == veh`.

**Fichier / symboles :** `BodyguardEscort.cpp` uniquement (tout est dans le namespace anonyme) — `s_spawnedEscortVehicles`, `IsSpawnedEscortVehicle`, `PurgeSpawnedEscortVehicles`, `DeleteEmptySpawnedEscortVehicles`, et les 2 boucles qui itèrent la liste (`AssignBodyguardsToEscort` ligne 527, `TickEscort` ligne 592). `HasActiveEscortConvoy` (ligne 470) utilise `.empty()` : OK après refactor.

**Diff attendu (esquisse) :**

```cpp
struct SpawnedEscortVehicle
{
    Vehicle veh = 0;
    int emptySinceMs = 0;   // 0 = occupé ou affecté (pas de compte à rebours)
};
static std::vector<SpawnedEscortVehicle> s_spawnedEscortVehicles;
static constexpr int kEscortVehicleEmptyGraceMs = 12000; // 12 s de grâce

bool AnyAliveGuardAssignedTo(Vehicle veh)
{
    for (auto& bg : BodyguardDb)
        if (IsBodyguardAlive(bg) && bg.EscortVehicle.Exists() &&
            bg.EscortVehicle.GetHandle() == veh)
            return true;
    return false;
}

void DeleteEmptySpawnedEscortVehicles()
{
    PurgeSpawnedEscortVehicles();          // adapté au nouveau type d'élément
    const int now = MISC::GET_GAME_TIMER();

    for (size_t i = 0; i < s_spawnedEscortVehicles.size(); )
    {
        auto& rec = s_spawnedEscortVehicles[i];
        if (!EntityVehicleExists(rec.veh)) { /* erase, continue — inchangé */ }

        ApplyEscortVehicleGodmode(rec.veh);

        // (a) occupé physiquement OU (b) encore affecté à un garde vivant
        //  -> jamais supprimé, compte à rebours remis à zéro.
        if (IsVehiclePhysicallyOccupied(rec.veh) || AnyAliveGuardAssignedTo(rec.veh))
        {
            rec.emptySinceMs = 0;
            ++i;
            continue;
        }

        // (c) vide ET non affecté : suppression seulement après le délai de grâce.
        if (rec.emptySinceMs == 0) { rec.emptySinceMs = now; ++i; continue; }
        if (now - rec.emptySinceMs < kEscortVehicleEmptyGraceMs) { ++i; continue; }

        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(rec.veh, true, true);
        GTAvehicle(rec.veh).Delete();
        s_spawnedEscortVehicles.erase(s_spawnedEscortVehicles.begin() + i);
    }
}
```

Adaptations mécaniques : `IsSpawnedEscortVehicle` (`std::find_if` sur `rec.veh`), `PurgeSpawnedEscortVehicles` (prédicat sur `rec.veh`), `push_back({escortVeh, 0})` ligne 553, et les deux `for (Vehicle veh : s_spawnedEscortVehicles)` deviennent `for (auto& rec : ...)`.

**Natives utilisées :** aucune nouvelle (`MISC::GET_GAME_TIMER` déjà utilisé dans le TU).

**Pourquoi c'est sûr :** structure interne au TU (rien d'exporté ne change de signature) ; le comportement « fuite zéro » est conservé — un véhicule réellement abandonné disparaît toujours, juste 12 s plus tard. La condition (b) est la vraie protection pendant le combat : les gardes gardent `EscortVehicle` affecté tant que `QueueReboardOrClear` n'a pas abandonné (> 6 tentatives ≈ 17 s), donc le véhicule survit à tout l'épisode de combat + ré-embarquement.

**Interaction avec l'étape 2 :** ordre d'implémentation important — l'étape 2 d'abord. Si on inversait, la grâce réduirait la fréquence du symptôme mais le trou de l'étape 2 resterait exploitable (véhicule détruit par explosion, dé-streamé…).

**Effet secondaire assumé :** après « Clear Escort Assignments », le véhicule spawné ne disparaît plus instantanément mais ~12 s après que le dernier garde en est descendu. À documenter dans `docs/Bodyguards.md`.

**Validation en jeu :** 1) Spawner un véhicule d'escorte (« Spawn Escort Vehicle If Full » on), affecter les gardes, déclencher une fusillade → les gardes descendent, se battent 20-30 s, remontent : le véhicule ne doit **jamais** disparaître. 2) « Clear Escort Assignments » → les gardes descendent, le véhicule disparaît ~12 s plus tard. 3) Tuer tous les gardes assis dedans → véhicule supprimé après la grâce (pas de fuite).

---

## Étape 4 — Relâcher `SET_PED_KEEP_TASK` après le combat (MOYEN — « ils deviennent autonomes »)

**Bug vérifié.** `BodyguardCombat.cpp` — `TaskBodyguardOnTarget` pose `PED::SET_PED_KEEP_TASK(bgPed, TRUE)` en véhicule (ligne 188) et à pied (lignes 195-196 après `TASK_COMBAT_PED`), et **rien ne repasse jamais à FALSE** ni ne redonne une tâche de suivi quand la cible est morte. Le garde reste en comportement combat/recherche au lieu de revenir en formation.

**Fichiers / symboles :**
- `BodyguardManagement.h` — `BodyguardEntity` : **ajouter en fin de classe** (invariant : ne jamais réordonner les membres existants) :
  ```cpp
  bool CombatTasked{ false };   // runtime uniquement — jamais persisté
  ```
- `BodyguardCombat.cpp` — `TaskBodyguardOnTarget`, `TaskAllBodyguardsOnTarget`, `TickCombatResponse` + nouvelle fonction locale `ReleaseFinishedCombatTasks`.

**Diff attendu (esquisse) :**

```cpp
// namespace anonyme de BodyguardCombat.cpp
static Ped s_squadTarget = 0;   // dernière cible ordonnée à l'escouade

// même logique que IsPedBusyFighting de BodyguardEscort.cpp (locale au TU escorte,
// on la duplique ici : 3 natives, pas de header commun à créer pour si peu)
static bool IsPedStillFighting(Ped ped)
{
    return PED::IS_PED_IN_COMBAT(ped, 0) ||
           PED::IS_PED_SHOOTING(ped) ||
           PED::IS_PED_IN_MELEE_COMBAT(ped);
}

// Dans TaskBodyguardOnTarget : bg.CombatTasked = true; juste après chaque
// SET_PED_KEEP_TASK(bgPed, TRUE) (branches véhicule ET à pied).
// Dans TaskAllBodyguardsOnTarget : s_squadTarget = targetPed;

static void ReleaseFinishedCombatTasks()
{
    static int s_nextCheck = 0;
    int now = MISC::GET_GAME_TIMER();
    if (now < s_nextCheck) return;
    s_nextCheck = now + 1000;

    const bool targetGone = s_squadTarget == 0 ||
        !ENTITY::DOES_ENTITY_EXIST(s_squadTarget) ||
        PED::IS_PED_DEAD_OR_DYING(s_squadTarget, true);
    if (!targetGone)
        return;
    s_squadTarget = 0;

    for (auto& bg : sub::BodyguardMenu::BodyguardDb)
    {
        if (!bg.CombatTasked) continue;
        if (!sub::BodyguardMenu::IsBodyguardAlive(bg)) { bg.CombatTasked = false; continue; }

        Ped ped = bg.Handle.GetHandle();
        if (IsPedStillFighting(ped))
            continue;   // a acquis une autre cible tout seul : on attend

        if (bg.EscortVehicle.Exists())
        {
            // Garde d'escorte : KEEP_TASK appartient au module escorte, ne pas
            // y toucher. Forcer juste le re-task de conduite au prochain TickEscort.
            bg.DriverTasked = false;
            bg.CombatTasked = false;
            continue;
        }

        bg.Handle.RequestControl();
        PED::SET_PED_KEEP_TASK(ped, FALSE);
        TASK::CLEAR_PED_TASKS(ped);   // doux (PAS _IMMEDIATELY) : l'IA de groupe
                                      // reprend la main à la frame suivante
        bg.CombatTasked = false;
    }
}

// Dans TickCombatResponse : appeler ReleaseFinishedCombatTasks() AVANT le
// early-return "mode == 0" (si l'utilisateur coupe le mode en plein combat,
// les gardes doivent quand même être relâchés), mais APRÈS le check DB vide.
```

**Natives utilisées :** `PED::SET_PED_KEEP_TASK(FALSE)`, `TASK::CLEAR_PED_TASKS`, `PED::IS_PED_IN_COMBAT`, `PED::IS_PED_SHOOTING`, `PED::IS_PED_IN_MELEE_COMBAT` (nouvelles dans ce TU, standard).

**Pièges :**
- **Escorte** : `AdoptExistingSeat`/`SeatBodyguard`/`QueueReboardOrClear`/`TickEscort` posent aussi `SET_PED_KEEP_TASK(TRUE)` pour maintenir la conduite. La branche `bg.EscortVehicle.Exists()` ci-dessus est indispensable — sans elle, on casserait le convoi après chaque fusillade.
- **`CLEAR_PED_TASKS` et non `_IMMEDIATELY`** : la version immédiate ferait « poper » le garde debout au milieu d'une animation, visuellement moche.
- Le champ `CombatTasked` ne doit **jamais** être écrit dans l'ini ni dans les squads sauvegardés (runtime only, comme `EscortVehicle`).
- `TickShootOnTarget`/`TickRetaliate`/`TickWantedThreats` gardent chacun leurs statics de cible : seule `s_squadTarget` (dernière cible tous modes confondus) pilote le relâchement — suffisant car `TaskAllBodyguardsOnTarget` est l'unique point d'entrée des ordres.

**Validation en jeu :** mode « Full Protection », viser un ped hostile → l'escouade attaque ; une fois la cible morte, dans les ~2-3 s **tous** les gardes rengainent et reviennent en formation derrière le joueur (sans avoir à rouvrir le menu). Répéter avec des gardes en véhicule d'escorte : après le combat ils ré-embarquent (comportement `Reboard After Combat` inchangé) et le convoi repart.

---

## Étape 5 — Seuil d'étoiles pour « Wanted Threats » (PETIT — anti « ils font leur vie »)

**Bug vérifié (aggravant, pas cause racine).** `BodyguardCombat.cpp:19` : mode par défaut 4 « Full Protection » ; `FindWantedThreat` (lignes 291-326) se déclenche dès `GET_PLAYER_WANTED_LEVEL > 0` (ligne 293) et cible **tout** ped assimilé police (`IsLawResponsePed`, lignes 97-137) dans un rayon de 90 m ; `TickWantedThreats` (328-352) relance l'assaut toutes les 700-2500 ms. Dès 1 étoile, toute l'escouade part en chasse en continu.

**Choix retenu :** ne pas changer le mode par défaut (les utilisateurs existants ont déjà `combat_response_mode` persisté ; changer le défaut serait sans effet pour eux). À la place : **seuil configurable d'étoiles**, défaut 2 — à 1 étoile les gardes n'agressent plus la police spontanément (retaliate continue de couvrir le cas où un policier blesse le joueur).

**Fichiers / symboles :**
- `BodyguardCombat.h` : `extern int g_wantedThreatMinStars;`
- `BodyguardCombat.cpp` : définition `int g_wantedThreatMinStars = 2;` (à côté de `g_combatResponseMode`, ligne 19) ; dans `FindWantedThreat` :
  ```cpp
  if (PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) < g_wantedThreatMinStars)
      return 0;
  ```
- `BodyguardSettings.cpp` (section « --- Behaviour --- », juste sous le texter « Combat Response », lignes 221-231) :
  ```cpp
  static const std::vector<std::string> starLabels = { "1", "2", "3", "4", "5" };
  int starIdx = sub::BodyguardMenu::g_wantedThreatMinStars - 1;
  if (starIdx < 0 || starIdx > 4) starIdx = 1;
  bool stInput = false, st_plus = false, st_minus = false;
  AddTexter("Wanted Response Min Stars", 0, { starLabels[starIdx] }, stInput, st_plus, st_minus);
  if (st_plus)  starIdx = (starIdx + 1) % 5;
  if (st_minus) starIdx = (starIdx == 0 ? 4 : starIdx - 1);
  sub::BodyguardMenu::g_wantedThreatMinStars = starIdx + 1;
  ```
- `BodyguardConfig.cpp` : lecture/écriture (voir tableau config plus bas), clamp 1..5, **aucune native** dans `ReadBodyguardConfig` (respecté : simple entier).

**Pourquoi c'est sûr :** un seul point de gate (`FindWantedThreat` retourne 0), aucun autre mode affecté ; valeur 1 = comportement actuel exact.

**Validation en jeu :** avec le défaut (2) : prendre 1 étoile → les gardes restent en formation (ils ne foncent pas sur les policiers tant que personne ne tire sur le joueur) ; passer à 2 étoiles → ils engagent. Mettre le réglage à 1 → retour au comportement actuel.

---

## Étape 6 — Libérer les gardes assis dans le véhicule personnel abandonné (MOYEN)

**Bug vérifié (comportement voulu mais piégeux).** `BodyguardEscort.cpp:630` : `if (bg.EscortSeat != -1) continue;` — un passager affecté (y compris dans le **véhicule du joueur** via `AdoptExistingSeat`/`FillPassengerSeats`, « Use My Seats First ») est ignoré par le tick tant qu'il est assis. Il est hors groupe (`RemoveFromPlayerGroupForEscort`). Si le joueur descend et part à pied, le garde reste assis **indéfiniment** ; seul « Clear Escort Assignments » le libère.

**Fichier / symbole :** `BodyguardEscort.cpp` — `TickEscort()` uniquement.

**Diff attendu (esquisse) :**

```cpp
// En tête de TickEscort, après le calcul de playerInVehicle :
static int s_playerOnFootSince = 0;
if (playerInVehicle)
    s_playerOnFootSince = 0;
else if (s_playerOnFootSince == 0)
    s_playerOnFootSince = MISC::GET_GAME_TIMER();
const bool playerLongOnFoot =
    !playerInVehicle && (now - s_playerOnFootSince) > 8000;   // 8 s à pied

// Remplacer la ligne 630 :
if (bg.EscortSeat != -1)
{
    // Passager du véhicule PERSONNEL du joueur (pas un véhicule spawné par le
    // mod) alors que le joueur est parti à pied et s'est éloigné : le libérer
    // pour qu'il redescende et re-suive (retour dans le groupe vanilla).
    if (playerLongOnFoot && !IsSpawnedEscortVehicle(veh))
    {
        Vector3 pp = ENTITY::GET_ENTITY_COORDS(playerPed, TRUE);
        Vector3 vp = ENTITY::GET_ENTITY_COORDS(veh, TRUE);
        float dx = pp.x - vp.x, dy = pp.y - vp.y, dz = pp.z - vp.z;
        if (dx * dx + dy * dy + dz * dz > 15.f * 15.f)   // > 15 m
        {
            TASK::TASK_LEAVE_VEHICLE(ped, veh, 0);
            PED::SET_PED_KEEP_TASK(ped, true);   // même pattern que ClearEscortState
            ClearEscortState(bg);
        }
    }
    continue;
}
```

Attention : `now` existe déjà (ligne 595) mais est déclaré **après** la boucle `EnsureEscortVehicleDriver` — déplacer sa déclaration plus haut ou réutiliser tel quel (il est avant la boucle par garde, OK).

**Natives utilisées :** `TASK::TASK_LEAVE_VEHICLE`, `ENTITY::GET_ENTITY_COORDS` (déjà dans le TU via d'autres fonctions), `MISC::GET_GAME_TIMER`.

**Pièges :**
- `ClearEscortState` seul ne fait **pas** sortir le garde d'un véhicule non-spawné (comportement voulu, commentaire lignes 661-664) — d'où le `TASK_LEAVE_VEHICLE` explicite juste avant.
- Ne toucher que les **passagers** (`EscortSeat != -1`). Un garde conducteur (`-1`) suit déjà le joueur via `TASK_VEHICLE_FOLLOW` (ligne 647) — comportement correct, ne pas le débarquer.
- Le délai (8 s) + la distance (15 m) évitent de vider la voiture quand le joueur fait juste un aller-retour au coffre ou entre dans un magasin voisin.
- Interaction étape 3 : véhicule non-spawné → jamais concerné par la suppression ; véhicules spawnés → non concernés par cette étape (`!IsSpawnedEscortVehicle`). Pas de recouvrement.

**Validation en jeu :** « Use My Seats First » on, « Assign Bodyguards To Escort » avec les gardes dans SA voiture ; rouler, descendre, s'éloigner à pied de > 15 m pendant ~10 s → les gardes descendent seuls et reviennent en formation. Contre-test : descendre et rester à côté de la voiture 30 s → ils restent assis. Contre-test 2 : gardes dans un véhicule d'escorte **spawné** + joueur à pied → le convoi continue de suivre en voiture (inchangé).

---

## Nouvelles clés de configuration (`[bodyguards]`, `BodyguardConfig.cpp`)

| Clé | Type | Défaut | Clamp | Étape |
|---|---|---|---|---|
| `combat_wanted_min_stars` | int | 2 | 1..5 | 5 |

Lecture (dans `ReadBodyguardConfig`, **sans native**, même pattern que `combat_response_mode` lignes 36-38) :

```cpp
g_wantedThreatMinStars = (int)ini.GetLongValue(kSection, "combat_wanted_min_stars", g_wantedThreatMinStars);
if (g_wantedThreatMinStars < 1 || g_wantedThreatMinStars > 5)
    g_wantedThreatMinStars = 2;
```

Écriture (dans `SaveBodyguardConfig`) : `ini.SetLongValue(kSection, "combat_wanted_min_stars", g_wantedThreatMinStars);`

Aucune autre étape n'introduit de clé (grâce, délais et seuils de distance restent des constantes compile-time ; les nouveaux champs `CombatTasked`/`emptySinceMs` sont runtime only — **jamais persistés**, conformément à l'invariant).

## Nouvelles entrées French.json

| Clé anglaise (code) | Valeur FR |
|---|---|
| `"Wanted Response Min Stars"` | `"Étoiles min. avant riposte police"` |

C'est le **seul** nouveau libellé de menu. Les messages « X ranimés, Y introuvables » sont des toasts `Game::Print` en FR codé en dur, comme tous les toasts existants du module (l'i18n des toasts est le chantier P1-1, hors périmètre). Valider le JSON après édition (compte de clés + parse).

## Risques et pièges transverses

- **Invariant Db/s_bodyguards** : aucune étape n'ajoute/ne retire d'entrée de `BodyguardDb` — seuls des champs sont lus/écrits. Pas de spawn hors `SpawnBodyguardPed`.
- **`BodyguardEntity`** : le champ `CombatTasked` s'ajoute **en fin de classe** (commentaire ligne 20 de `BodyguardManagement.h` : ne pas réordonner). Jamais sérialisé.
- **`submenu_enum.h`** : intact (aucun nouveau sous-menu).
- **Ordre d'implémentation = ordre des étapes** (1→6). Les étapes 1, 2, 5 sont indépendantes ; 3 dépend logiquement de 2 (le trou de :601 doit être bouché avant de réduire la fréquence du déclencheur) ; 4 doit connaître la branche escorte (ne pas relâcher le KEEP_TASK d'un garde d'escorte) ; 6 réutilise `IsSpawnedEscortVehicle` refactorée en 3.
- **Tick budget** : étape 3 ajoute une boucle Db par véhicule spawné toutes les 750 ms (négligeable, Db ≤ ~30) ; étape 4 est throttlée à 1 s ; étape 6 n'ajoute que 2 `GET_ENTITY_COORDS` par passager et par tick escorte.
- **Build** : MSBuild Release x64 (voir mémoire `build-setup`), 0 erreur / 0 avertissement attendu. Compiler après chaque étape, pas seulement à la fin.
- **docs/Bodyguards.md** à mettre à jour en fin de plan : §8 (tick : relâchement KEEP_TASK, grâce 12 s, libération véhicule abandonné), §7 (clé `combat_wanted_min_stars`), § dépannage (message « corps introuvables »).

## Check-list de test en jeu (à faire par l'utilisateur)

1. **Revive dying** : garde au sol non mort → « Revive Dead » le relève ; garde dé-streamé → message « Gardes ranimés : 0 (1 corps introuvables) ».
2. **Heal dying** (si option retenue) : « Heal All » relève aussi un garde au sol.
3. **Groupe restauré** : détruire le véhicule d'escorte pendant que les gardes se battent → ils reviennent en formation < 2 s après la fin du combat.
4. **Véhicule persistant** : fusillade complète avec convoi → le véhicule d'escorte ne disparaît jamais ; après « Clear Escort Assignments », il disparaît ~12 s après s'être vidé.
5. **Retour de combat** : cible morte → tous les gardes rengainent et re-suivent sans action menu ; en convoi, ils ré-embarquent et le convoi repart.
6. **1 étoile** : défaut (2) → pas d'agression spontanée de la police à 1 étoile ; à 2 étoiles → engagement ; réglage à 1 → comportement d'avant.
7. **Voiture abandonnée** : gardes assis dans SA voiture, partir à pied > 15 m pendant ~10 s → ils descendent et suivent ; rester à côté → ils restent assis.
8. **Round-trip config** : régler « Wanted Response Min Stars » à 3, quitter/relancer → la valeur est conservée (`menyooConfig.ini`, `[bodyguards] combat_wanted_min_stars=3`).
9. **Non-régression** : spawn/list/squads/wanted lock/formation persistée inchangés ; 12 avertissements C4805 connus non aggravés.

## Estimation de taille

| Étape | Taille | Fichiers |
|---|---|---|
| 1. Revive dying + message introuvables | Petit (~40 lignes) | BodyguardMenu.cpp |
| 2. ClearEscortState sur handle mort | Petit (~6 lignes) | BodyguardEscort.cpp |
| 3. Grâce suppression véhicule | Moyen (~70 lignes, refactor struct) | BodyguardEscort.cpp |
| 4. Relâchement KEEP_TASK post-combat | Moyen (~60 lignes + 1 champ) | BodyguardCombat.cpp/.h, BodyguardManagement.h |
| 5. Seuil étoiles Wanted Threats | Petit (~25 lignes) | BodyguardCombat.cpp/.h, BodyguardSettings.cpp, BodyguardConfig.cpp, French.json |
| 6. Libération véhicule personnel abandonné | Moyen (~30 lignes) | BodyguardEscort.cpp |

Total estimé : ~230 lignes réparties sur 7 fichiers + doc. Aucune étape ne bloque les autres en cas d'abandon partiel, sauf 3 qui suppose 2 déjà faite.
