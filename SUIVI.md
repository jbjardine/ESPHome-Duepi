# Suivi ESPHome-Duepi

## En cours

- Résultat demandé : compensation ESPHome avec sonde HA, PI facultative, reprise explicite par bouton, puis installation autorisée sur le poêle de JB. Dernier choix : aucun complément HA à installer, aucun restart global.
- Branche `codjardine/remote-room-regulation`; push uniquement vers `origin` sur le fork personnel. Aucun travail dans DUEPI_EVO ni push upstream.
- Base native `c1c0f57`, fraîcheur `cee5c52`, reprise/échos RC6000 `0c485c6`. 44 tests HA du dépôt, 42 natifs, 14 contrats et 6 C/F passent. Images réelles ESP-01, legacy sans extension et exemple C3 compilées avec ESPHome 2026.9.1. Le contrôle matériel a aussi couvert le repli et la reprise; les tests isolés ne sont pas présentés comme une preuve du raccord SDK.
- Fonctionnement : champ texte persistant `Room temperature sensor`, permission native HA actions pour ce seul module, demande toutes les60 s et callback sur la même connexion. Aucun script, automatisation, helper, jeton ou complément HA. Copier l'entity_id plutôt que choisir une liste personnalisée; modifier ce champ si la sonde est renommée.
- Bouton `Reactivate room compensation` : choisit Compensation sans allumage. Local reste le choix manuel visible. La réserve ODR initiale du banc est corrigée; SDK installé inchangé. UART Celsius conservé, sources/limites/affichage Fahrenheit convertis; pas d'autodétection d'après une valeur isolée68.
- Préparation terrain Capinghem : sauvegardes privées et configuration réelle compilée. FlashOTA réussi et API vérifiée (build 20/10 14:08:29),22 anciennes entités conservées sur 33. Seul VTherm poêle désactivé sans Off; données/options conservées et8 autres entrées identiques. Champ source configuré, fichier ESPHome serveur validé, carte Manuel/Réactiver relue viaAPI. Aucun complément/redémarrage HA/paramètre constructeur/radiateur modifié.
- Fraîcheur corrigée par `cee5c52` : attribut source `duepi_sample_timestamp` prioritaire, sans rajeunissement par un polling HA, une reconnexion ou un message d'humidité. Mapping Z2M par sonde, température exclue du cache et placeholders null ignorés. Sans attribut explicite, ancien comportement conservé. Le maximum Duepi reste 15 min et le silence du relais 180 s.
- Sonde salon : maximum température passé de 3600 à 300 s, min=10 s et variation=1 °C inchangés. Rapports 15:01:07, 15:06:06 puis 15:11:04; deux rapports identiques à 22,0 °C à 15:21:02 et 15:26:01 renouvellent correctement la fraîcheur. Les messages humidité/null intermédiaires ne la renouvellent pas. Aucun réglage constructeur du poêle modifié.
- Livraison finale : firmware réel build 15:19:03, OTA réussie et API vérifiée. Test sur source HA temporairement absente : repli natif 21 → 20 °C à 15:21:51, mode Compensation maintenu. Sonde restaurée à 15:22:24; correction normale 20 → 21 °C confirmée RC6000 à 15:26:51, après cinq minutes. Toujours Compensation/regulating à 15:27:21, cible salon 20 °C, PI 0, P1 affichée, pont off, radiateurs Off/16 °C et VTherm poêle désactivé. La perte sur ancienneté 900 s est vérifiée à horloge injectée; aucune pile retirée. Précision thermique et rendu visuel restent des validations distinctes.
- Reprise : [guide](docs/room-regulation.md), [validation native](docs/superpowers/validation/2026-10-20-duepi-native.md) et [artefacts](docs/superpowers/validation/2026-10-20-duepi-native-artifacts.json). Préparation initiale historique dans2026-10-20-duepi-room.md. Rapports/configuration privés dans `/Users/jbjardine/.local/share/home-assistant-edits/capinghem/poele-room-2026-10-20/`.

## Ensuite, déjà demandé

- Essai thermique chezJB avant toute proposition upstream : source fraîche, Compensation sans PI, comparaison pièce/RD1000/calcul/RC6000/RD4000 sur plusieurs relevés; PI dans un second temps. Pas de second régulateur en parallèle. Ne jamais flasher les images de fixtures à secrets factices.

## Idées en réserve

- Précision à 20 °C et intérêt réel de la PI à mesurer physiquement après validation de cadence. L'ajustement du maximum de reporting de cette sonde fait partie du correctif de fraîcheur demandé le 20/10; aucune modification des autres sondes autorisée par ce chantier.
- Affichage à revoir séparément : le climate Room target n'annonce pas la prise en charge de current_temperature dans ses traits, malgré une source reçue et son âge correct. Le calcul utilise la valeur reçue; aucun correctif de cet affichage engagé dans le chantier fraîcheur.
