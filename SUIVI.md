# Suivi ESPHome-Duepi

## En cours

- Résultat demandé : compensation ESPHome avec sonde HA, PI facultative, reprise explicite par bouton, puis installation autorisée sur le poêle de JB. Dernier choix : aucun complément HA à installer, aucun restart global.
- Branche `codjardine/remote-room-regulation`; push uniquement vers `origin` sur le fork personnel. Aucun travail dans DUEPI_EVO ni push upstream.
- Code natif `c1c0f57` après réarmement/affichage `aebed0d`. 42 tests natifs,14 contrats,6 C/F,12 templates HA passent; quatre compilations finales2026.9.1 réussies : ESP-01 réel privé, fixturesESP-01 C/F et C3. Legacy inchangé réutilisé. Revue finale native approuvée; aucun défaut important, seule latence initiale bornée à60 s signalée.
- Fonctionnement : champ texte persistant `Room temperature sensor`, permission native HA actions pour ce seul module, demande toutes les60 s et callback sur la même connexion. Aucun script, automatisation, helper, jeton ou complément HA. Copier l'entity_id plutôt que choisir une liste personnalisée; modifier ce champ si la sonde est renommée.
- Bouton `Reactivate room compensation` : choisit Compensation sans allumage. Local reste le choix manuel visible. La réserve ODR initiale du banc est corrigée; SDK installé inchangé. UART Celsius conservé, sources/limites/affichage Fahrenheit convertis; pas d'autodétection d'après une valeur isolée68.
- Préparation terrain Capinghem : sauvegardes privées et configuration réelle compilée. FlashOTA réussi et API vérifiée (build 20/10 14:08:29),22 anciennes entités conservées sur 33. Seul VTherm poêle désactivé sans Off; données/options conservées et8 autres entrées identiques. Champ source configuré, fichier ESPHome serveur validé, carte Manuel/Réactiver relue viaAPI. Aucun complément/redémarrage HA/paramètre constructeur/radiateur modifié.
- Sonde salon : publications12:56:48 puis13:56:33 Paris le 20/10, écart proche d'une heure. Avec le maximum 15 min, conserver Local quand la mesure est périmée; ne pas forcer la chauffe ou allonger le seuil.
- Critère logiciel et mise en service satisfaits : revue close, OTA réelle et retour API, ancienneté1470→1905 s sans rajeunissement, anciennes entités conservées et bloc Manuel/Réactiver installé. ModeLocal/cible 20 °C, natif 19 °C/P1 etPI 0. Mesure salon21,4 °C périmée (>15 min): Compensation non activée. Rendu visuel non vérifié après reprise du navigateur; API/configuration vérifiées. Documentation et branche synchronisées en fin de livraison. Précision thermique distincte.
- Reprise : [guide](docs/room-regulation.md), [validation native](docs/superpowers/validation/2026-10-20-duepi-native.md) et [artefacts](docs/superpowers/validation/2026-10-20-duepi-native-artifacts.json). Préparation initiale historique dans2026-10-20-duepi-room.md. Rapports/configuration privés dans `/Users/jbjardine/.local/share/home-assistant-edits/capinghem/poele-room-2026-10-20/`.

## Ensuite, déjà demandé

- Essai thermique chezJB avant toute proposition upstream : source fraîche, Compensation sans PI, comparaison pièce/RD1000/calcul/RC6000/RD4000 sur plusieurs relevés; PI dans un second temps. Pas de second régulateur en parallèle. Ne jamais flasher les images de fixtures à secrets factices.

## Idées en réserve

- Précision à20 °C et intérêt réel de la PI à mesurer physiquement. Résoudre la cadence de source avant de juger la régulation; un ajustement de la sonde n'est pas autorisé implicitement par le présent flash.
