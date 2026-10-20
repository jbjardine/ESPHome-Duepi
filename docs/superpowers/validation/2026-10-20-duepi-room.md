# Vérification de la régulation de pièce Duepi

> Historique de la préparation initiale. Le [rapport natif](2026-10-20-duepi-native.md) décrit le réarmement, le transport sans complément HA et la correction ultérieure de la réserve du banc.

État de préparation sur `codjardine/remote-room-regulation`, depuis `upstream/main` à `a7f8915`. Aucun flash, service Home Assistant ou ordre au poêle n'a été exécuté pendant cette implémentation. Les push de cette branche visent exclusivement le fork `jbjardine/ESPHome-Duepi`.

Rapport final du 20 octobre 2026. Le code produit vérifié est au commit
`e361eda481fbc2169383418dda57a5aeee84b323`; les commits suivants de cette
livraison ne modifient que la documentation de validation et le suivi.

| Vérification ciblée | Résultat | Portée |
| --- | ---: | --- |
| Contrôleur, parseur et arbitrage SDK natif | 38/38 | Réserve du harness explicitée ci-dessous |
| Contrats YAML et conversions des limites | 13/13 | 7 contrats + 6 cas Celsius/Fahrenheit |
| Complément Home Assistant | 23/23 | HA 2026.10.2 |
| Ruff check et format --check | Réussis | Les 7 fichiers Python du complément et des tests |
| Compilations MCU complètes | 4/4 | ESPHome 2026.9.1, secrets factices |

Le coordinateur a exécuté les trois groupes de tests sur le code final. Il ne
s'agit pas des suites complètes de Home Assistant ou d'ESPHome. La création
des environnements et les commandes portables figurent dans
[le guide](../../room-regulation.md#development-checks).

## Contrôleur et parseur

Implémentation `7b66b5b`, correctifs `979bf58`, raccordement `f8c9e35` puis `e361eda` : **38 tests natifs passent**, avec la réserve formelle du banc décrite plus bas. Couverture ciblée : protocole et types JSON, compensation, arrondi et bornes, PI facultative, échantillons distincts, saturation, données périmées, démarrage, prise de contrôle manuelle, pont, intervalle d'écriture et confirmation RC6000.

Les nouveaux tests de revue ont montré trois échecs avant correction. Ils passent après correction : aucun repli sans paquet HA accepté après démarrage ; un paquet mal formé ne débloque pas ce garde ; une émission RF2 réelle reste suivie même lorsque le bus est déjà marqué occupé. Le délai de confirmation part de l'émission réelle. La relecture indépendante ciblée a clos les deux constats sans nouveau problème important.

Commande exécutée depuis la racine, avec ESPHome **2026.9.1** et ArduinoJson **7.4.3** :

```sh
ESPHOME_PYTHON="$ESPHOME_TEST_VENV/bin/python" \
ARDUINOJSON_INCLUDE="$ARDUINOJSON_SRC/src" sh tests/native/run_tests.sh
```

`esphome config` et `esphome compile --only-generate` ont aussi réussi pour le composant facultatif avant son raccordement UART. La macro `USE_DUEPI_ROOM` est générée uniquement lorsque ce composant est configuré.

## Complément Home Assistant

Implémentation `c21bec9`, correctifs de revue `2541271` : **23 tests HA ciblés passent**, sur Home Assistant **2026.10.2**, Python **3.14.2**, pytest **9.0.3** et pytest-asyncio **1.4.0**. Aucune suite complète de Home Assistant n'a été lancée.

Les fixtures proviennent du tag officiel Home Assistant Core `2026.10.2`, commit `8d7fd8b68677c2585be36ec86043308f403875b8`. Le plugin tiers de fixtures plus ancien n'est pas utilisé : il importe une classe supprimée dans cette version de HA. Les dépendances des tests restent limitées au développement ; le manifeste du complément ne déclare aucune dépendance d'exécution supplémentaire.

Le test couvre sélection et options, unicité par module, identité du relais et renommage, conversion Fahrenheit, indisponibilité et restauration, rapports identiques et heartbeats, âge transmis sans rajeunissement, envois sérialisés, reconnexion et nettoyage au déchargement. Les traductions anglaise et française ont été vérifiées via le vrai chargeur de traductions HA, après un test rouge montrant que `strings.json` seul était insuffisant.

```sh
PYTHONPATH="$HA_CORE_SRC" PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 \
  "$HA_TEST_VENV/bin/python" -m pytest -q tests/ha
ruff check custom_components tests/ha
ruff format --check custom_components tests/ha
"$HA_TEST_VENV/bin/python" -m pip check
```

Ces contrôles passent. Les six avertissements viennent des fixtures ou dépendances amont : réécriture pytest-asyncio déjà importé, dépréciation aiohttp et `asyncio.iscoroutinefunction` dans backoff. Aucun contournement produit n'a été ajouté pour les masquer. Après suppression du marqueur temporaire à la racine `custom_components`, deux tests ciblés de flux et traductions passent encore avec le namespace Python normal.

## Raccordement et compilations

Les observations de régulation viennent des réponses réelles RD1000, RD9000,
RC6000 et des alarmes RDA. L'extension est absente du build legacy. La revue
indépendante a examiné une fois l'ensemble de la branche puis uniquement les
correctifs. Les trois constats du contrôle final sont clos; aucun problème
important ou critique ne reste ouvert.

Les cas suivants échouaient avant les corrections et passent après :

- Un paquet HA invalide avec l'identifiant numérique du dernier rapport est
  accepté conformément au protocole, permettant le repli initial.
- Après MyDPremote, le premier RC6000 frais est comparé à la cible d'avant
  connexion, même sans observation pendant le pont. Une modification manuelle
  sélectionne Local.
- Les scripts partagés de cible et de mode redémarrent sur la dernière
  commande : automatique → manuel A → manuel B et Marche en attente → Arrêt
  conservent le dernier choix.

**Réserve mineure conservée :** le harness natif remplace `ScopedSourceGuard`
par une garde neutre dans `tests/native/test_duepi_room.cpp`, mais compile
séparément le vrai `script.cpp` du SDK sans ce remplacement. Les unités de
traduction voient donc deux définitions différentes du template `Script`,
contrairement à l'ODR C++. Les tests s'exécutent avec succès, mais la preuve
formelle de l'arbitrage SDK est limitée par ce défaut du banc. Le remplacement
reste dans le test; aucun macro ni double de ce type n'entre dans le firmware.
Les empreintes de `script.h` et `script.cpp` installés correspondent au RECORD
du paquet 2026.9.1. Une garde cohérente entre unités du harness reste à faire
avant de le considérer comme preuve rigoureuse du comportement SDK. Les
attentes simulées ne prouvent pas non plus le timing UART matériel.

Les **13 contrôles de fixtures** ont été exécutés ainsi :

```sh
env ESPHOME_BIN="$ESPHOME_TEST_VENV/bin/esphome" \
  "$ESPHOME_TEST_VENV/bin/python" -m unittest discover \
  -s tests/fixtures -p 'test_room_*.py' -v
```

Une première invocation avait sélectionné `ESPHOME_PYTHON` au lieu de
`ESPHOME_BIN`, entraînant six échecs de recherche de l'exécutable. L'invocation
corrigée donne 13/13 sans changement produit.

Les quatre compilations complètes utilisent ESPHome **2026.9.1**.
ESP8266 utilise Arduino **4.2.1**; le LOLIN C3 Mini utilise ESP-IDF **5.5.5**.

| Fixture | RAM utilisée / disponible | Flash application utilisée / disponible | Image OTA, octets |
| --- | ---: | ---: | ---: |
| Legacy ESP-01 1 Mo | 45 760 / 81 920 | 516 661 / 1 023 984 | 520 816 |
| Extension ESP-01, limites °C | 47 736 / 81 920 | 545 505 / 1 023 984 | 549 664 |
| Extension ESP-01, limites °F | 47 732 / 81 920 | 545 509 / 1 023 984 | 549 664 |
| Extension LOLIN C3 Mini | 113 192 / 321 296 | 1 028 662 / 1 835 008 | 1 029 024 |

Les avertissements restants concernent la dépréciation existante de
`get_preference_hash` et le format `%X` existant dans `globals.yaml` sur C3.
Les collisions de macros Arduino avec des noms d'enum et un ancien appel
ClimateTraits, trouvés par les compilations, ont été corrigés avant ces builds.

Sur ESP-01, le linker donne `_FS_start=0x402FB000`, donc une fin de zone OTA à
**1 028 096 octets**. Le backend ESPHome arrondit l'image installée et l'image
entrante compressée aux secteurs de 4 096 octets. Avec gzip niveau 9 :

- Images entrantes optionnelles C/F : 383 648 / 383 660 octets, chacune arrondie
  à 385 024 octets.
- Legacy → extension : 524 288 + 385 024 = 909 312 octets; marge 118 784.
- Extension → extension : 552 960 + 385 024 = 937 984 octets; marge **90 112
  octets (88 Kio)**.

Ce calcul statique suppose que le firmware installé correspond à la fixture.
La taille réelle du firmware actuellement sur le module n'a pas été mesurée.
Ce n'est ni une tentative ni une réussite OTA. Les tailles et SHA-256 des
fichiers réellement produits sont conservés dans
[le manifeste des artefacts](2026-10-20-duepi-room-artifacts.json). Les binaires
locaux sont sous `tests/fixtures/.esphome/`, ignorés par Git, avec des
identifiants factices : **ne pas les flasher tels quels**.

## Décision d'interface

Un arrêt explicite depuis le thermostat de pièce sélectionne immédiatement **Local** et remet l'intégrale à zéro. Cela empêche des corrections automatiques pendant que le dernier état reçu indique encore la combustion. Une commande explicite Marche préserve le mode sélectionné, mais ne réactive pas la compensation depuis Local. Conséquence : après cet arrêt explicite, l'utilisateur doit sélectionner à nouveau Compensation pour reprendre la régulation externe.

Le support Fahrenheit couvre les limites configurées, les sources HA et
l'affichage HA. Les limites C/F sont normalisées vers le calcul et le chemin
UART Celsius existants. Les cas 50–95 °F, 51–94 °F et 50–104 °F passent;
54–55 °F est refusé faute de degré Celsius entier dans cet intervalle. La
correction PI est une différence `Δ°C`, sans conversion d'une température
absolue. Conséquence : un poêle échangeant réellement des valeurs Fahrenheit
sur son UART nécessite des captures et une adaptation; son affichage °F seul
ne prouve pas cette convention. Aucune autodétection n'est revendiquée.

La réserve ODR du banc natif est différée à l'issue de la revue finale.
Conséquence : les résultats de ce banc ne doivent pas être présentés comme
une preuve formelle complète de l'arbitrage SDK. Le produit compilé n'inclut
pas le remplacement de garde du test.

## Fichiers pour la phase d'installation

Le firmware facultatif est fourni par `components/duepi_room/` et
`components/packages/room_regulation.yaml`. Les exemples sont dans
`examples/room-regulation-esp8266.yaml` et
`examples/room-regulation-lolin-c3-mini.yaml`. Base, extension et composant
externe doivent tous viser la même branche du fork.

Le complément à installer ultérieurement est le dossier
`custom_components/duepi_room/`. Lors de cette phase seulement, le copier dans
le répertoire `custom_components` de la configuration HA, redémarrer HA, puis
ajouter **Duepi Room Companion** depuis les intégrations. Son assistant permet
de choisir le module compatible et la sonde. Les options changent la sonde
sans recompilation. Le complément ne crée aucun thermostat supplémentaire.
Le firmware compatible doit d'abord exposer son relais.

Le protocole supervisé figure dans
[le guide](../../room-regulation.md#supervised-field-protocol). La prochaine
phase exige une compilation avec les secrets locaux existants et un flash
autorisé séparément. Avant activation, sauvegarder puis vérifier la méthode
d'isolation du seul VTherm du poêle pour la version installée, sans supposer
qu'un déchargement ou une désactivation n'enverra pas d'Arrêt. Commencer en
Compensation, PI inactive. Comparer plusieurs relevés de pièce, RD1000,
calcul, RC6000 et puissance appliquée RD4000, en respectant l'intervalle de
cinq minutes entre corrections. Évaluer la PI dans une seconde étape.

## Limites de la preuve

Ces tests vérifient les contrats logiciels. Ils ne prouvent ni la précision thermique autour de 20 °C, ni l'intérêt réel de la PI sur un poêle, ni un transfert OTA effectué. L'âge `last_reported` de HA est celui d'un rapport, pas une garantie de date d'acquisition physique. La cadence de la sonde de JB doit être mesurée avant l'essai thermique : une absence de rapport pendant plus de quinze minutes déclenchera normalement le repli avec les réglages par défaut.
