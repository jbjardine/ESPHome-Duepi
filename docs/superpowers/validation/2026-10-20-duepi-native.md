# Duepi Room — réarmement et transport HA natif

Code produit : `c1c0f5735df474d74e5bafae631394beefc1501e`. Branche personnelle `codjardine/remote-room-regulation`, push vers le fork via `origin` uniquement.

Ce rapport remplace, pour l'installation courante, les instructions de complément HA et la réserve du banc de la [validation initiale](2026-10-20-duepi-room.md), qui reste historique. Il couvre l'amendement de réarmement `aebed0d` puis le transport natif. La revue ciblée `aebed0d..c1c0f57` est approuvée sans constat important; la mise en service réelle est encore en préparation; aucun succès OTA ou thermique n'est revendiqué à ce stade.

## Fonctionnement retenu

Le package expose le champ texte persistant `Room temperature sensor`. HA rend le template transmis avec l'action native ESPHome, lit la valeur, son unité et `last_reported`, puis renvoie un paquet v1 au même appareil. Il faut autoriser les actions HA pour ce seul appareil. Aucun complément, script, automatisation, helper, jeton ou restart global HA n'est nécessaire.

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

- Ruling: accepter le délai initial d'au plus60s lorsque HA arrive après un autre client API ou avant son abonnement aux appels. Le retry périodique continue; la régulation active reste bloquée sans source utilisable. Coût: le premier affichage peut attendre un cycle. Aucun correctif supplémentaire du transport n'est requis pour ce cas borné.

## Mise en service réelle

Revue ciblée approuvée; installation en attente. Vérifier après OTA le retour API complet, l'ancienneté croissante sans nouvelle publication, les anciennes entités, RC6000 et RD4000. Conserver Local si la source dépasse15min; ne pas forcer une mesure ou augmenter le seuil. L'essai thermique et l'intérêt de la PI restent distincts de la compilation et du transport.
