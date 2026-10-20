# Suivi ESPHome-Duepi

## En cours

- Résultat demandé : sonde de pièce choisie dans HA, compensation native ESPHome et PI facultative ; plan approuvé le 20 octobre 2026.
- Branche : `codjardine/remote-room-regulation`, depuis `upstream/main`; destination des push : `origin` uniquement.
- Critère de fin : code testé, revu et compilé (legacy, ESP-01 1 Mo avec place OTA, ESP32-C3), documentation et protocole terrain. Aucun flash ni changement HA pendant ce travail.
- Reprise : plan `docs/superpowers/plans/2026-10-20-duepi-room.md`, spécification et protocole dans `docs/superpowers/specs/`.
- État : base mise à jour ; préparation des lots délégués A et B.

## Ensuite, déjà demandé

- Préparer l'essai chez JB avant toute proposition à l'upstream. Installation/flash à traiter après présentation des artefacts et des vérifications.

## Idées en réserve

- Précision à 20 °C et intérêt thermique de la PI à mesurer sur le poêle ; les tests logiciels ne constituent pas cette preuve.
