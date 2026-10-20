# Suivi ESPHome-Duepi

## En cours

- Résultat demandé : compensation ESPHome avec sonde HA, PI facultative, reprise explicite par bouton, puis installation autorisée sur le poêle de JB. Dernier choix : aucun complément HA à installer, aucun restart global.
- Branche `codjardine/remote-room-regulation`; push uniquement vers `origin` sur le fork personnel. Aucun travail dans DUEPI_EVO ni push upstream.
- Code natif `c1c0f57` après réarmement/affichage `aebed0d`. 42 tests natifs,14 contrats,6 C/F,12 templates HA passent; quatre compilations finales2026.9.1 réussies : ESP-01 réel privé, fixturesESP-01 C/F et C3. Legacy inchangé réutilisé. Revue finale native approuvée; aucun défaut important, seule latence initiale bornée à60s signalée.
- Fonctionnement : champ texte persistant `Room temperature sensor`, permission native HA actions pour ce seul module, demande toutes les60s et callback sur la même connexion. Aucun script, automatisation, helper, jeton ou complément HA. Copier l'entity_id plutôt que choisir une liste personnalisée; modifier ce champ si la sonde est renommée.
- Bouton `Reactivate room compensation` : choisit Compensation sans allumage. Local reste le choix manuel visible. La réserve ODR initiale du banc est corrigée; SDK installé inchangé. UART Celsius conservé, sources/limites/affichage Fahrenheit convertis; pas d'autodétection d'après une valeur isolée68.
- Préparation terrain Capinghem : sauvegardes privées et configuration réelle compilée. Flash/écritures HA pas encore exécutés. Isoler seulement l'entrée VTherm dédiée au poêle, sans ordreOff; préserver les radiateurs et paramètres constructeur. Les huit autres entrées VTherm seront comparées avant/après.
- Sonde salon : publications12:56:48 puis13:56:33 Paris le20/10, écart proche d'une heure. Avec le maximum15min, conserver Local quand la mesure est périmée; ne pas forcer la chauffe ou allonger le seuil.
- Critère de fin courant : revue close, firmware réel flashé, retour API et données/âge prouvés, anciennes entités conservées, carte Manuel/Réactiver installée, documentation et branche personnelle synchronisées. Précision thermique distincte.
- Reprise : [guide](docs/room-regulation.md), [validation native](docs/superpowers/validation/2026-10-20-duepi-native.md) et [artefacts](docs/superpowers/validation/2026-10-20-duepi-native-artifacts.json). Préparation initiale historique dans2026-10-20-duepi-room.md. Rapports/configuration privés dans `/Users/jbjardine/.local/share/home-assistant-edits/capinghem/poele-room-2026-10-20/`.

## Ensuite, déjà demandé

- Essai thermique chezJB avant toute proposition upstream : source fraîche, Compensation sansPI, comparaison pièce/RD1000/calcul/RC6000/RD4000 sur plusieurs relevés; PI dans un second temps. Pas de second régulateur en parallèle. Ne jamais flasher les images de fixtures à secrets factices.

## Idées en réserve

- Précision à20°C et intérêt réel de laPI à mesurer physiquement. Résoudre la cadence de source avant de juger la régulation; un ajustement de la sonde n'est pas autorisé implicitement par le présent flash.
