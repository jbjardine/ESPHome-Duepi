# Duepi — réarmement explicite et installation Capinghem

Demande approuvée de JB : rendre visible le mode manuel et permettre de réactiver facilement la compensation, puis flasher son ESPHome réel. Amendement borné à la fonctionnalité de pièce déjà implémentée.

## Global constraints

- Même checkout et branche `codjardine/remote-room-regulation`; Git centralisé par le coordinateur; aucun push upstream, aucune nouvelle branche/worktree/PR. Préserver les modifications des autres. Aucun sous-agent ne fait de Git ni de sous-délégation.
- Un agent de codage; coordinateur responsable du déploiement réel et des API HA Capinghem. Aucun agent de code ne contacte HA, le poêle, Proxmox ou un appareil.
- Aucun changement constructeur, puissance, ventilateur ou radiateur. Aucune commande Marche/Arrêt pour tester le bouton. Le réarmement choisit Compensation sans PI et n'allume pas le poêle.
- Ne pas déduire les unités UART d'un seul nombre. Conversion source/limites °F existante conservée; protocole E670 actuellement Celsius. L'idée d'un seuil reste un indice à expliquer, pas une autodétection ajoutée.
- Tests ciblés et compilation ESP-01 réelle avant flash; confirmation d'installation, retour HA et lectures RC6000/RD4000 après flash. Cadence de sonde contrôlée avant activation; pas de promesse thermique.

### Task 1: Contrôle de réarmement, explication et banc natif

Owner: agent de codage. Ownership exclusif : `components/packages/room_regulation.yaml`, `tests/native/**`, `tests/fixtures/test_room_integration.py`, `custom_components/duepi_room/{strings.json,translations/en.json,translations/fr.json}`, `docs/room-regulation.md`. Aucun autre fichier sans coordination.

- Ajouter au package facultatif un bouton ESPHome template nommé `Reactivate room compensation`, ID `duepi_room_rearm`, icône `mdi:restart`. Son unique action est `select.set` sur `duepi_room_mode`, option exacte `Compensation`. Réutiliser le chemin public existant pour préserver publication/persistance et remise à zéro PI. Ne pas ajouter de nouveau contrôleur, d'état concurrent ou d'allumage.
- Le sélecteur existant est l'état armé/manuel. Documenter et fournir un exemple Lovelace natif de message visible quand il est Local : « Mode manuel — compensation désactivée. Cliquez sur Réactiver la compensation pour reprendre la régulation avec la sonde de la pièce. Ce bouton n'allume pas le poêle. » Le coordinateur réalisera la carte sur la page réelle avec les vrais entity_id après installation. L'exemple peut être dans le guide, sans nouvelle dépendance UI.
- Ajouter dans les descriptions des assistants HA (strings et traductions en/fr) une explication brève : choix de sonde, réglage du mode sur l'appareil ESPHome, Arrêt/consigne manuelle entraînent Local, bouton pour reprendre. Ne pas changer le protocole ni les entités/flux existants.
- Corriger la réserve ODR du banc : actuellement `ScopedSourceGuard` est remplacé seulement dans la TU du test, tandis que le SDK `script.cpp` est compilé séparément sans ce remplacement. Toutes les TU voyant Script doivent avoir une définition identique. Garder les vrais Script/RestartScript/Automation du SDK 2026.9.1; aucun contournement produit ni modification de SDK installé. Limiter la correction aux tests.
- Reproduire les preuves ciblées : bouton n'émet que la sélection, mode Compensation et pas PI/Marche/Arrêt; états manuels existants et dernière commande conservés; tests natifs avec SDK cohérent. Faire les tests de contrat et config ESPHome. Le coordinateur compile les images et déploie.
- Runtime ESPHome 2026.9.1 dans `.venv-esphome` (Python 3.13) si absent; ArduinoJson 7.4.3 déjà dans `/Users/jbjardine/.cache/duepi-room-native/ArduinoJson-7.4.3/src`. Remplacer les valeurs par défaut du runner visant l'ancien workspace supprimé par le venv de développement documenté et le chemin ArduinoJson explicitement sélectionnable. Pas de dépendance runtime ajoutée.
- Livrer un rapport dans le workspace SDD avec commandes, résultats, fichiers touchés et limites. Aucun commit, flash ni accès réseau à une installation réelle.

### Task 2: Revue bornée puis déploiement réel

Owner: relecteur indépendant en lecture seule puis coordinateur. Revue uniquement de l'amendement, pas une nouvelle revue de toute la branche. Tous les correctifs produit retournent à l'agent.

- Sauvegarder configuration ESPHome réelle, configuration du seul VTherm poêle et carte HA affectée. Vérifier l'isolation VTherm sans ordre d'arrêt depuis le code effectivement installé avant de l'effectuer.
- Compiler la configuration réelle avec secrets conservés hors Git et les sources locales exactes; vérifier la place OTA contre l'image installée quand possible. Préserver la configuration précédente comme retour arrière.
- Flasher le module Capinghem explicitement autorisé; vérifier connexion API, anciennes et nouvelles entités, bouton et sélecteur. Installer/configurer le complément HA si nécessaire, sans toucher les radiateurs. Toute opération HA est annoncée précisément et utilise les API publiques; pas de modification directe de `.storage`.
- Afficher le message Manuel et le bouton Réactiver dans la carte poêle existante, ou à défaut une carte minimale dédiée au poêle. Vérifier le contenu sauvegardé et, si disponible, le rendu.
- Ne pas activer deux régulateurs simultanément. Si la sonde actuelle est périmée, conserver le mode Local clairement indiqué et rapporter cet obstacle au lieu de forcer le chauffage. Activer Compensation sans PI uniquement avec données fraîches et l'ancien VTherm isolé, dans le périmètre de la mise en service demandée.
- Documenter l'état réel final, les preuves et le point de reprise dans le suivi du projet et le rapport privé. Push uniquement origin après vérifications; aucune PR upstream.
