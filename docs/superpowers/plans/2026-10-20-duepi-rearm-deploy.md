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

## Amendement demandé : aucune installation de complément HA

JB préfère que toute la fonctionnalité soit incluse dans ESPHome. Le complément précédemment préparé ne sera pas installé et aucun redémarrage global HA n'est autorisé ni nécessaire pour le chemin retenu. Le choix d'une source par liste personnalisée est remplacé par un champ texte natif contenant son `entity_id`, modifiable sans recompilation. L'intégration ESPHome HA standard reste nécessaire, avec son autorisation native d'exécuter des actions activée pour ce module.

### Task 3: Transmission native des mesures HA

Owner: agent firmware existant. Ownership exclusif : `components/duepi_room/**`, `components/packages/room_regulation.yaml`, `tests/native/**`, `tests/fixtures/test_room_integration.py`, nouveaux tests ciblés de template HA sous `tests/ha/`, `docs/room-regulation.md`, et exemples seulement si nécessaires. Aucun fichier HA custom_components, historique UART ou réel à modifier; aucune opération Git ou appareil.

- Réutiliser uniquement `homeassistant.action`, `data_template`, `variables` et un rappel `api.actions` du même module. Aucun script/automation/helper ou composant HA à installer, aucun token HA, aucune deuxième connexion ou API privée HA/ESPHome. Le script Jinja est inclus dans le package ESPHome et rendu par HA via le mécanisme officiel.
- Ajouter un champ ESPHome texte natif `Room temperature sensor`, ID `duepi_room_source`, restaurable, vide au départ. Il accepte un identifiant `sensor.*` normal ou vide. Une modification est effective sans compilation. La source vide ne demande rien. Après connexion HA et au changement de source, demander la mesure; ensuite toutes les60s. Les appels ne dépendent pas du mode Local/Compensation : les diagnostics doivent fonctionner en Local.
- La demande vise le service `esphome.<nom_du_module_remplaçant_les_tirets_par_des_underscores>_duepi_room_receive`, où le nom vient de l'API publique ESPHome. L'action `duepi_room_receive` reçoit un seul argument string `payload`. HA2026.10.2 nomme effectivement ce service ainsi. Ne jamais appeler une action de commande du poêle pour obtenir ces données.
- Construire côté HA le protocole v1 déjà figé : `v`, `generation`, `seq`, `valid`, `temp_c` si valide, `age_s`, `sample_id`, moins de255octets ASCII. ESPHome génère une génération32hex et des séquences uint32 strictement croissantes; changer de source/redémarrer renouvelle la génération, l'épuisement du compteur aussi. Le Jinja utilise les variables transmises, la valeur/les unités réelles et `last_reported` du State HA; conversion°F→°C, nombres finis seulement, entité absente/unknown/unavailable/restaurée/unité non prise en charge/dates impossibles→message invalide. Une température identique mais nouveau last_reported est un nouvel échantillon; une interrogation périodique ne rajeunit jamais last_reported. L'âge maximal reste décidé par le firmware, pas par le Jinja.
- Utiliser le parseur/contrôleur existant. Les retours d'une ancienne génération/source et les doublons/hors ordre ne renouvellent pas le bail. Un changement de source annule les offres non émises et réinitialise PI/données de source; il ne doit pas faire croire qu'un ancien retour est frais ni perdre le suivi d'un RF2 déjà émis. Attendre une réponse de la nouvelle source avant un premier repli; garder les protections boot/pont/manuel/confirmation existantes.
- Définir la priorité des transports : lorsqu'un source native non vide est configurée, le texte de relais historique ne peut pas réintroduire une autre génération. Si la source native est vide, préserver le chemin de relais déjà implémenté pour compatibilité. Ne pas faire tourner les deux sur la même entrée.
- Le bouton de réarmement et le retour du mode réel, déjà revus au commit aebed0d, restent inchangés. Les écritures automatiques restent RF2 uniquement, espacées d'au moins300s, et soumises à RC6000; aucune commande HEAT/OFF pour une lecture source.
- Tests déterministes de session/source/génération/séquence/présence/péremption et de rejet de retours retardés. Tests de rendu avec le vrai moteur HA2026.10.2 : C/F, source manquante/restaurée/invalide, sourceancienne restant ancienne, échantillons distincts, taille/types du JSON, nommage du service. Prouver l'assemblage du template et le contrat callback; ne pas considérer le rendu passif comme une preuve de transport sur l'appareil.
- Reprendre l'environnement HA de développement décrit dans le guide (fixtures officielles déjà présentes dans /tmp/duepi-ha-core-2026.10.2), sans installer quoi que ce soit sur HA réel. Tester seulement les nouveaux cas et les contrats natifs concernés. Valider les configurations ESPHome C/F et C3; le coordinateur réalise les compilations et le flash.
- Documenter le démarrage sans complément : package inclus, autorisation native HA pour ce module, champ source à remplir, cible salon, Compensation à réarmer. Indiquer explicitement le compromis du champ entity_id plutôt qu'une liste personnalisée. Aucune promesse de fonctionnement avant preuve réelle du retour API.
- Rapport complet `.superpowers/sdd/2026-10-20-duepi-rearm-deploy/task-3-report.md`, avec preuves, commandes, interfaces exactes et limites. Aucun sous-agent ni Git.

### Task 4: Revue et mise en service native

Owner: relecteur indépendant pour le seul lot natif, coordinateur pour toutes les écritures réelles. Ne pas répéter la revue du bouton déjà close.

- Vérifier le contrat du trajet natif et les protections, puis compiler l'ESP-01 réel et l'exemple C3 affecté. Tester les autres limites C/F via les cas de configuration existants. Revenir au propriétaire du code pour toute correction.
- Isoler le seul VTherm poêle par désactivation de son entrée HA, après lecture du chemin unload installé qui ne commande pas Off; sauvegarde existante. Pas de restart global HA ni de complément.
- Mettre à jour l'option native allow_service_calls du seul module quand prêt, flasher, reconnecter et vérifier les nouvelles entités. Configurer le champ source, confirmer température ET âge réel, sans activer Compensation sur une source périmée. Vérifier l'absence de commande native causée par le transport seul.
- Réaliser la carte demandée avec message Manuel et bouton, conserver les radiateurs et le reste du dashboard. Distinguer preuve du transport API, preuve RC6000, puissance réelleRD4000 et essai thermique.
- Mettre à jour les rapports et suivis, push seulement origin. Si les données de la sonde restent trop anciennes, laisser Local et rapporter ce point réel; ne pas modifier la sonde ou son seuil de fraîcheur sans demande.
