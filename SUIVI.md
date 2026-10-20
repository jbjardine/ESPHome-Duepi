# Suivi ESPHome-Duepi

## En cours

- Résultat demandé : sonde de pièce choisie dans HA, compensation native ESPHome et PI facultative ; plan approuvé le 20 octobre 2026.
- Branche : `codjardine/remote-room-regulation`, depuis `upstream/main`; destination des push : `origin` uniquement.
- Critère de fin : code testé, revu et compilé (legacy, ESP-01 1 Mo avec place OTA, ESP32-C3), documentation et protocole terrain. Aucun flash ni changement HA pendant ce travail.
- Reprise : guide `docs/room-regulation.md`; rapport et empreintes des artefacts dans `docs/superpowers/validation/2026-10-20-duepi-room.md`. Plan, spécification et protocole conservés sous `docs/superpowers/`.
- État : préparation terminée. Code produit `e361eda`, 38 tests natifs, 13 contrôles YAML/unités, 23 tests HA 2026.10.2, Ruff et quatre compilations complètes ESPHome 2026.9.1 passent. Revue indépendante close pour les défauts importants/critiques; une réserve ODR du banc natif est documentée dans le rapport.
- ESP-01 1 Mo : images optionnelles C/F de 549 664 octets; marge OTA compressée statique 90 112 octets pour une mise à jour entre ces fixtures. La taille du firmware réellement installé n'a pas été mesurée. Binaires à secrets factices, à ne pas flasher tels quels.
- Fahrenheit : sources HA, limites et affichage pris en charge; convention UART Celsius conservée. Un protocole réellement Fahrenheit reste à établir par capture. Arrêt explicite du thermostat de pièce sélectionne Local; réactivation externe explicite nécessaire.
- Installation réelle inchangée : aucun flash/upload, ordre au poêle, changement HA/radiateurs ou paramètre constructeur pendant l'implémentation.

## Ensuite, déjà demandé

- Essai chez JB avant toute proposition à l'upstream : vérifier l'isolation sans Arrêt du seul VTherm du poêle, mesurer la cadence de sa sonde, puis installer/flasher seulement après autorisation de cette phase. Commencer en Compensation sans PI; comparer RD1000, pièce, calcul, RC6000 et puissance réelle RD4000. Ne pas utiliser les binaires de fixtures factices.

## Idées en réserve

- Précision à 20 °C et intérêt thermique de la PI à mesurer sur le poêle ; les tests logiciels ne constituent pas cette preuve.
- Uniformiser la garde neutre entre unités de traduction du harness natif avant de lui attribuer une preuve formelle complète de l'arbitrage SDK; aucune incidence du macro de test sur le firmware compilé.
