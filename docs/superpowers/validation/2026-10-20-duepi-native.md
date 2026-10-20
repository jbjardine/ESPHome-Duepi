# Duepi Room — réarmement et transport HA natif

Code initial : `c1c0f5735df474d74e5bafae631394beefc1501e`. Corrections de fraîcheur et reprise : `cee5c52` puis `0c485c6`. Branche personnelle `codjardine/remote-room-regulation`, push vers le fork via `origin` uniquement.

Ce rapport remplace, pour l'installation courante, les instructions de complément HA et la réserve du banc de la [validation initiale](2026-10-20-duepi-room.md), qui reste historique. Il couvre l'amendement de réarmement `aebed0d` puis le transport natif. La revue ciblée `aebed0d..c1c0f57` est approuvée sans constat important; la mise en service réelle et le retour API sont maintenant vérifiés. La régulation thermique reste à tester avec une source fraîche.

## Fonctionnement retenu

Le package expose le champ texte persistant `Room temperature sensor`. HA rend le template transmis avec l'action native ESPHome, lit la valeur, son unité et la date `duepi_sample_timestamp` si présente (sinon `last_reported`), puis renvoie un paquet v1 au même appareil. Il faut autoriser les actions HA pour ce seul appareil. Aucun complément, script, automatisation, helper, jeton ou restart global HA n'est nécessaire.

Le contrôleur ESPHome conserve cible, compensation, PI facultative, âges et gardes UART. Le bouton `Reactivate room compensation` choisit Compensation, sans allumage. Le sélecteur Local est l'état manuel; une carte peut afficher ce choix et le bouton de reprise. Les réponses RD9000 connues synchronisent l'affichage Heat/Off sans envoyer d'ordre.

## Vérifications ciblées

- 42/42 tests natifs contrôleur/parseur/arbitrage, dont sessions natives, retours retardés et maintien du suivi RF2 émis.
- 14/14 contrats d'intégration; 6/6 cas de limites C/F et correction Δ°C.
- 12/12 rendus du vrai moteur Home Assistant 2026.10.2; mesure et date réelles, doublon, source absente/restaurée/invalide, C/F et débordement après conversion.
- Quatre compilations finales ESPHome 2026.9.1 : configuration privée du véritable ESP-01, fixtures ESP-01 C/F et LOLIN C3 Mini. Le legacy inchangé réutilise sa compilation initiale; il n'a pas été recompilé.
- La cohérence ODR du banc est corrigée : le même header de compatibilité est forcé sur toutes les unités de traduction. Le SDK installé n'est pas modifié. Cela ferme la réserve formelle initiale; les timings UART matériels restent une preuve distincte.

Commandes exécutées :

```sh
sh tests/native/run_tests.sh
python3 tests/fixtures/test_room_integration.py
ESPHOME_BIN=.venv-esphome/bin/esphome python3 tests/fixtures/test_room_native_units.py
PYTHONPATH=/tmp/duepi-ha-core-2026.10.2 PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 .venv-ha/bin/python -m pytest -q tests/ha/test_native_payload_template.py
.venv-esphome/bin/esphome compile <configuration-privee>/pellet-duepi-local.yaml
.venv-esphome/bin/esphome compile tests/fixtures/room_esp01_1m.yaml
.venv-esphome/bin/esphome compile tests/fixtures/room_esp01_1m_fahrenheit.yaml
.venv-esphome/bin/esphome compile tests/fixtures/room_lolin_c3_mini.yaml
```

Les premières preuves rouges étaient le trajet API et la session absents. Le cas `1e308 °F` a ensuite montré une conversion non finie déclarée valide; une garde après conversion le corrige. La première compilation ESP-01 a révélé une collision avec la macro Arduino `HEX`; le renommage local ferme ce défaut. Aucun élargissement de firmware ou dépendance n'a été ajouté pour contourner ces erreurs.

Les [empreintes et tailles des images](2026-10-20-duepi-native-artifacts.json) distinguent les fixtures à faux secrets et l'image privée réelle. Les binaires privés restent hors Git. Les marges OTA des fixtures supposent l'image installée connue : 73 728 octets pour extension→extension, 110 592 pour le legacy initial→extension. La taille exacte de l'ancienne image du module réel n'est pas établie; le contrôle OTA du module reste nécessaire.

## Décisions et conséquences

- Ruling: un champ `entity_id` natif remplace la liste personnalisée de sondes. Bénéfice : aucun complément ni restart HA. Coût : copier correctement l'identifiant, le corriger si la sonde est renommée, et autoriser l'appareil à appeler des actions HA.
- Ruling: l'affichage Heat/Off suit les seules phases RD9000 reconnues, sans commande. Une erreur de correspondance pourrait affecter l'affichage; la garde de régulation reste séparée et n'autorise que `0x0201`. Une phase inconnue conserve le dernier affichage.
- Les sources HA et limites Fahrenheit sont converties vers le calcul Celsius. Une valeur isolée telle que68 ne suffit pas à identifier les unités réellement échangées sur UART; aucune autodétection UART n'est ajoutée.

- Ruling: accepter le délai initial d'au plus60 s lorsque HA arrive après un autre client API ou avant son abonnement aux appels. Le retry périodique continue; la régulation active reste bloquée sans source utilisable. Coût: le premier affichage peut attendre un cycle. Aucun correctif supplémentaire du transport n'est requis pour ce cas borné.

## Mise en service réelle

Revue ciblée approuvée. L'image privée a été flashée sur le module réel par OTA chiffrée (26,51 s de transfert). Son API rapporte exactement le timestamp de compilation attendu. Les22 anciennes entités et3anciens services sont conservés;11 entités et le rappel natif sont ajoutés. Le fichier ESPHome sauvegardé sur le serveur valide aussi ses sources publiées2026.9.1.

Le seul VTherm dédié au poêle a été désactivé sans ordre Off; ses données/options sont préservées et les huit autres entrées sont identiques. La permission d'action HA du seul module est activée. Aucun complément HA, redémarrage global, paramètre constructeur ou radiateur modifié.

Le champ source est configuré. L'ESP a accepté l'âge réel de la mesure:1470 s puis1905 s sur des relevés espacés d'environ7 min; la source reste datée de sa vraie publication. Son mode resteLocal, cible pièce 20 °C et correctionPI 0. Le poêle conserve sa consigne native19 °C et sa puissance appliquéeRD4000P1. La sonde locale historique a son filtre existant; le contrôleur utilise toujours RD1000 brut.

Le bloc Poêle de la tablette contient cible salon, mode, champ source, message Manuel, avertissement de mesure ancienne et bouton Réactiver. Un contrôle API confirme que ce seul bloc a changé. Le template d'avertissement passe le moteur HA strict. Le rendu visuel n'est pas revendiqué: la capture serveur est désactivée et la tentative navigateur a été interrompue par une reprise utilisateur; aucune interaction après cette reprise.

**Limite terrain:** aucune activation de Compensation ni pression du bouton avec cette mesure trop ancienne. La sonde a montré un intervalle proche d'une heure, incompatible avec le maximum 15 min pendant une partie du temps. Ne pas allonger silencieusement le seuil. L'aller-retour API/âge est prouvé, mais l'application d'une nouvelle correctionRF2/RC6000 et la précision thermique de ce contrôleur restent à vérifier avec des données fraîches, puis la PI dans un second temps. Le seuil, le thermostat radiateurs et les paramètres constructeur sont conservés.


## Correction de fraîcheur et reprise vérifiées le 20 octobre

Le diagnostic a distingué les rapports réels de sonde et les écritures d'état HA. MQTT ne réécrit pas systématiquement une température identique; `last_reported` seul ne prouvait donc pas la cadence de la sonde. Le package utilise maintenant une date d'échantillon explicite lorsqu'elle est fournie, sans la remplacer par une date HA plus récente si elle est invalide. Un mapping par appareil Zigbee2MQTT exclut la température du cache et ignore le placeholder null des publications d'humidité. Aucun complément, helper ou restart HA ajouté.

Le maximum de reporting de la seule sonde utilisée est passé de 3600 à 300 s. Deux cycles réels de cinq minutes ont été observés, puis **22,0 °C identiques à 15:21:02 et 15:26:01** ont reçu deux dates distinctes. L'âge publié par ESPHome suit ces dates et continue d'augmenter entre les rapports. Le seuil Duepi reste 900 s, la présence du relais 180 s et l'intégration PI ne progresse pas sur un simple polling HA.

La première reprise a révélé deux raccords manquants : une décision RF2 pouvait être consommée par un poll sans émission, et le refresh différé du thermostat natif traitait un retour RC6000 comme une commande manuelle. Les entrées concernées passent par l'update existant; le refresh du thermostat est effectué sous le garde de synchronisation. La priorité des vraies commandes manuelles reste dans le contrôleur. Ces défauts ont été révélés par le contrôle matériel, malgré les tests isolés verts; aucun test du contrôleur seul n'est présenté comme une preuve de la réaction du SDK.

Validation finale : **44 tests HA du dépôt**, **42 natifs**, **14 contrats**, **6 C/F**. Les six avertissements de fixtures/dépendances HA sont non bloquants, sans échec. Compilation ESP-01 réelle, legacy sans extension et C3 réussie; les fixtures à faux secrets ne sont pas flashées. Image réelle finale 560384 octets, RAM 59,0 %, flash 54,3 %, API build 15:19:03 après OTA. Aucune nouvelle compilation de la fixture ESP-01 Fahrenheit n'est revendiquée pour ce correctif; ses six contrôles de configuration passent.

Test réel d'une source HA absente, sans créer d'entité : le repli fait passer RC6000 de **21 à 20 °C à 15:21:51**, avec Compensation maintenue. Sonde salon rétablie à 15:22:24; après le délai de cinq minutes, reprise normale à **21 °C confirmée par RC6000 à 15:26:51**, état regulating et toujours Compensation à 15:27:21. Cible pièce 20 °C, PI 0. Ce test d'indisponibilité est distinct d'un retrait physique de pile; l'expiration à 900 s malgré les heartbeats HA est couverte par l'horloge injectée.

La précision thermique à 20 °C reste à mesurer. Un défaut d'affichage séparé du current_temperature de Room target est seulement noté en réserve, sans chantier supplémentaire. Les preuves détaillées et empreintes de firmware restent dans le rapport privé du suivi de déploiement.

## Conservation de l'intégrale pendant les nettoyages courts, 20 octobre au soir

Les phases connues `0x0401` et `0x0200` conservent maintenant l'intégrale pour un
nettoyage de moins de 120 secondes. Les changements entre ces deux phases ne
renouvellent pas ce délai. Aucun calcul d'intégration ni RF2 automatique pendant
le nettoyage. Au retour de `0x0201`, de nouvelles observations RD1000 et RC6000
sont requises; la base de temps de la PI repart à cette reprise, sans compter
le temps du nettoyage ni celui d'attente des lectures. Les cas d'arrêt, alarme,
phase inconnue, données perdues/périmées, contrôle manuel et nettoyage prolongé
continuent à effacer l'intégrale. Aucun gain ni mode Marche/Arrêt automatique
n'est ajouté ou changé.

Preuve rouge : les cinq premiers nouveaux tests échouaient sur le code précédent.
Après implémentation et ajout du cas de reprise sans lectures fraîches :
`sh tests/native/run_tests.sh` **48/48** et
`python3 tests/fixtures/test_room_integration.py` **14/14** passent.
Ces contrôles n'exécutent pas la suite HA Core. Les images C3 et Fahrenheit
n'ont pas été recompilées pour ce correctif limité au contrôleur.

La configuration privée réelle a été compilée avec ESPHome **2026.9.1** puis
flashée par OTA. Image **560752 octets**, transfert compressé **389957 octets**;
SHA256 `c777591e1c559ca75d6b16ac5423535da244012a85fb8d437f9560f90bfe37c5`.
RAM **48320/81920**, flash **556597/1023984**. L'API du même module confirme
le build **21:49:25 +0200**. Le contrôle d'un nettoyage physique après ce flash
reste distinct des tests déterministes du contrôleur.

Après le retry natif initial, la vraie sonde est de nouveau reçue; le mode
Compensation + PI présent avant le flash est rétabli à **21:52:09**, cible pièce
20 °C, état regulating, native/calcul 22 °C, mesure salon 21,4 °C âgée de 165 s.
La PI repart normalement à zéro au redémarrage, avec correction proportionnelle
-0,14 °C. Aucun ordre Marche/Arrêt envoyé pour installer ce correctif. Pont off
et radiateurs Off/16 °C. Un nettoyage physique après ce flash n'a pas encore
été observé; le maintien thermique à 20 °C n'est pas une conclusion de ce test.
