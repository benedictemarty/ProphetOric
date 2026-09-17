# Changelog — ProphetOric

## [0.6.0] — 2026-09-17 — Configuration : type de connexion, explorateur de dossiers
### Validé
- PO, `make run` contre prophet.3617.fr (Phosphoric SDL) : « fonctionne ».
### Ajouté (demande PO)
- Écran de configuration refait : hôte ; **type de connexion** (« HTTP 8998 » /
  « TLS 443 (modem) », bascule espace/j/k — un port personnalisé est
  conservé) ; port ; **explorateur** du stockage du LOCI pour le dossier
  cible : niveau **volumes** (« 0: Internal storage », clés USB « N: MSC … »,
  le modem CDC exclu) quand il y en a plusieurs, puis les sous-dossiers
  (`OPENDIR`/`READDIR`/`CLOSEDIR`, tri alphabétique), `entrée` entrer,
  `b` parent, `espace` choisir, **`n` nouveau dossier** (`MKDIR`), `échap` ;
  mot de passe. Résultat enregistré tel quel (`jeux`, `2:`, `2:jeux/oric`) ;
  téléchargement et `mkdir` respectent le préfixe de volume.
- `loci.c` : `loci_opendir/readdir/closedir` (dirent de 72 octets : fd, nom
  64, attributs, taille).
- Tests : `config` (explorateur : `n` jeux, entrer, espace) et **`volumes`**
  (clé USB émulée `--loci-usb` : volume choisi, fichier écrit sur la clé,
  `PROPHET.CFG` sur le flash avec `N:jeux`).
### Corrigé
- Le compteur Ko de l'indicateur faisait une division 32 bits par Ko (≈ 10 000
  cycles en cc65) : l'anneau de 32 octets débordait à 9600 bauds (fichier
  corrompu à l'octet 4131, test `baud_ring32`). Compteur 16 bits par tranche.

## [0.5.1] — 2026-09-17 — Indicateur de téléchargement
### Ajouté
- Pendant un téléchargement : roue `-\|/` en bas à droite (un caractère écrit
  directement dans l'écran TEXT après chaque bloc de 128 octets) et compteur
  « nn K » tous les 1 024 octets (demande PO). Test `spin_wheel` (capture en
  cours de transfert, hors comparaison de référence).

## [0.5.0] — 2026-09-17 — Sprint 5 : tranches Range, reprise, disquette vérifiée
### Ajouté
- `make run` : crée `flash/` (flash du LOCI émulé) avec `microdis.rom`, exclut
  les clés USB du PC ; message « fichier refuse par le LOCI (dossier ?) » quand
  l'ouverture échoue (vu par le PO : `--loci-flash` sur un dossier inexistant).
- **Téléchargement par tranches `Range: bytes=a-b` de 32 Ko** (réponse 206,
  taille totale lue dans `Content-Range`), une connexion par tranche ;
  **reprise en ajout** (`O_APPEND`) sur coupure, 3 essais par tranche ; un
  serveur sans Range (200) est pris d'un trait. Test `resume` : un relais
  (`tests/cut_proxy.py`) coupe la première réponse après 5 000 octets → le
  fichier de 20 Ko arrive identique.
- Scénario long `dsk_boot` **vérifié** : 1 Mo en 36 tranches, disquette
  montée en A, `MIA_BOOT` → Sedoric démarre (le flash du LOCI doit contenir
  `microdis.rom`, pré-semé dans le test comme sur le vrai LOCI ; Phosphoric ne
  la résout pas depuis `roms/`, contrairement à `basic11b.rom`).
- `tests/run.sh` : `ONLY=<nom>` couvre aussi les contrôles annexes ; `SEED=`
  pour pré-semer le flash.
### Constaté (émulateur)
- Le backend `picowifi` de Phosphoric n'a **pas de contrôle de flux** : au-delà
  de son anneau de 64 Ko les octets sont jetés en silence (un `.dsk` de 1 Mo
  s'arrêtait à 69 Ko). Les tranches de 32 Ko contournent ; signalement
  `docs/MESSAGE-phosphoric-picowifi-flux.md` (copie dans `~/Oric1/`).

## [0.4.0] — 2026-09-17 — Sprint 4 : disquettes, 9600 bauds, recherche
### Ajouté
- **Lancer une disquette** (`l` après un `.dsk`) : montage en lecteur A (op
  `MOUNT` 0) puis `MIA_BOOT` (Microdisc + BASIC 1.1) — le LOCI bascule les
  ROM et resette l'Oric, qui démarre sur la disquette téléchargée. Test
  `dsk_boot` (`LONG=1` : disquette Sedoric de 1 Mo générée à la volée par
  `tap2sedoric` depuis `~/Oric1/disks/SEDO40u.DSK`, hors git ; 18 min).
- **Recherche** (`s` sur les catégories) : `/search/<clé>` (titre, auteur,
  description), résultats paginés comme une catégorie. Test `search`.
- **9600 bauds** (Control `$1E`, OricTel : `$18` = 1200) : 8× plus vite ;
  19200 perd le dialogue AT avec un anneau de 32 octets. 1 Mo ≈ 18 min.
  Non vérifié sur LOCI + PicoWiFi réels.
### Corrigé
- **Réception à 9600 bauds avec l'anneau de 32 octets du LOCI** : (1) les
  en-têtes étaient analysés après coup (≈ 100 ms de `strncmp` en cc65)
  pendant que le corps continuait d'arriver → 32 premiers octets perdus ;
  analyse désormais **au fil des octets** (statut + `Content-Length`) ;
  (2) la boucle du corps faisait de l'arithmétique 32 bits par octet
  (> 1 000 cycles) → boucle serrée par blocs de 128 (8 bits), longueur
  restante décomptée par bloc. Vérifié : 20 Ko à motif identiques à 9600
  bauds avec `--serial-buffer 32` (test `baud_ring32`, fichier
  `tests/repo/oric-games/zorg/zorg.tap` remplacé par un motif de 20 Ko ; le
  `.tap` BASIC autorun est un paquet à part, `oric-games/hello`, trouvé par la
  recherche dans le test de lancement). Même correction dans `http_get`
  (la description de la fiche perdait des caractères).
- Listes demandées en `sort=date&ord=desc` (comme ProphetGui).
### Constaté (émulateur)
- Phosphoric rythme l'ACIA d'après le registre Control (1200 bauds dès que
  `serial_init` écrit `$18` ; « instant transfer » seulement avec Control
  `$00`) : les mesures précédentes étaient donc à 1200 bauds.

## [0.3.0] — 2026-09-17 — Sprint 3 : lancement, débit réaliste
### Ajouté
- **Lancer** (`l` après un téléchargement) : le `.tap` reçu est monté comme
  cassette sur le LOCI (op `MOUNT`, lecteur 4) et le client rend la main au
  BASIC avec le message « Tapez CLOAD"" » — même geste que le menu du LOCI ;
  pas d'auto-frappe (la ROM 1.1 n'a pas de tampon clavier). Test `launch` +
  `launch_run` : `zorg.tap` (BASIC autorun fabriqué par `bas2tap` de
  Phosphoric) chargé par `CLOAD""` et exécuté (« HELLO FROM PROPHET TAP »).
- Fichiers `.zip` ignorés au téléchargement (inutilisables sur Oric ; 461
  titres du catalogue — décision serveur en attente), compteur affiché.
- Test `baud` + `baud_1200` : `--serial-baud 1200 --serial-buffer 32` (anneau
  du firmware LOCI devant le 6551) → fiche et téléchargement intacts.
- Tests isolés des clés USB du PC : `--loci-usb none` (Phosphoric attache
  sinon `/media/$USER` comme volumes LOCI — un test ne doit jamais y écrire).
### Corrigé
- **Plantage après l'écran de configuration** : une ligne d'état de 40
  caractères exactement faisait passer `CURS_Y` à 28 (cc65 atmos `cputc`
  avance la ligne après la 40e colonne) ; `cclear` lisait alors la table des
  adresses écran hors bornes et écrivait 40 espaces dans le code (`$07BB`,
  trouvé par `--trace` + `--dump-ram-at`). `line()` n'écrit plus jamais 40
  `cputc` suivis d'autre chose et replace le curseur.
### Non fait
- `.dsk` : montage Microdisc (lecteurs 0-3) non branché ; 9600/19200 bauds :
  non testés (le PicoWiFi réel : débit non vérifié).

## [0.2.0] — 2026-09-17 — Sprint 2 : téléchargement sur le LOCI, configuration
### Ajouté
- `src/loci.c/.h` : API MIA du LOCI en C (op `$03AF`, xstack `$03AC`, stub
  `$03B0`, retour A/X) — `open/read/write/close/mkdir`, détection par la
  signature `$03B3/5/7`. Protocole relevé dans `~/bbsoric/client/loci.s`
  (validé sur LOCI réel) et `~/Oric1/src/io/loci_fs.c`.
- Téléchargement (`g` sur la fiche) : `/files/<id>` puis chaque
  `/files/<id>/<n>` **en flux** (`http_get_stream`, blocs de 128 octets
  écrits au fil de la réception, sans tout garder en RAM) dans le dossier
  cible du LOCI (`mkdir` si besoin) ; contrôle par `Content-Length`.
- `PROPHET.CFG` sur le LOCI (hôte / port / dossier / mot de passe), lu au
  démarrage ; écran **configuration** (`c`) avec saisie des 4 champs
  (entrée = suivant, échap = annuler), enregistrement.
- Tests : scénarios `dl` (+ `dl_file` : `zorg.tap` identique sur le flash
  root du LOCI), `secret` (mot de passe dans `PROPHET.CFG` → paquet réservé
  visible), `config` (+ `config_file` : fichier écrit, `jeux/dune.tap`
  téléchargé) ; `ONLY=<nom>` pour un seul scénario.
### Modifié
- Modem : `ATZ` une seule fois ; plus d'attente `ATI` « CONNECTED TO WIFI »
  (l'émulation répond « WiFi: x UP » → 15 s perdues par requête) ; jusqu'à 3
  `ATD` avec 3 s d'attente ; fin de réponse = `NO CARRIER` (le serveur ferme)
  et `+++`/`ATH` seulement en secours. Une requête ≈ 2-3 s ; passe de tests
  complète ≈ 3 min au lieu de 12.
- Débit : Phosphoric transfère l'ACIA **instantanément** (« baud rate 0
  (external clock) », `--serial-baud N` pour un rythme réaliste) — le 1200
  bauds d'OricTel n'est donc pas mesurable ici ; à traiter avec `--serial-baud`
  au sprint 3.
- TAP : 15,7 Ko (CODE 14 Ko, BSS 4,9 Ko).

## [0.1.0] — 2026-09-17 — Sprint 1 : catalogue texte
### Ajouté
- Client fonctionnel en émulation : `build/prophet.tap` (cc65 `-t atmos`,
  10,7 Ko, autorun) affiche les catégories Oric, la liste paginée d'une
  catégorie et la fiche d'un programme (titre, auteur, description repliée à
  40 colonnes, nombre de fichiers) depuis `prophet.3617.fr:8998`.
- `src/http.c` : GET HTTP/1.1 sur modem Hayes PicoWiFiModemUSB (ATZ, ATI →
  IP prête, `ATD-hôte:port` sans telnet, requête, en-têtes, `Content-Length`,
  `+++`/ATH) ; en-têtes `ResponseFormat: cli`, `Connection: close`,
  `X-Prophet-Password` (si défini), `Range` ; `ATD-#` pour un port 443 (TLS
  terminé par le modem — émulé, non vérifié sur matériel).
- Importés tels quels d'OricTel (EUPL‑1.2, provenance en tête de fichier) :
  `serial.h/.c`, `serial_asm.s` (ACIA 6551 `$0380`, polling), `serial_tx.c`,
  `at_modem.c/.h`, `tapehdr.s` (nom `PROPHET`), `cfg/prophetoric.cfg` ;
  de ProphetGui : `cli.c/.h` et `tests/host/test_cli.c`.
- Tests : `make test` = tests hôte gcc (`test_cli`, `test_http` sur faux modem
  Hayes réactif) + `tests/run.sh` : prophetd local (`tests/repo`, paquet
  protégé `dune` caché sans mot de passe, `PROPHET_PASSWORD` posé) et 3
  scénarios Phosphoric `--headless --realtime --loci --serial picowifi`
  (captures texte comparées à `tests/ref/`).
### Constaté
- Mode telnet par défaut du modem : CR → CR NUL dans la requête → 400 du
  serveur ; corrigé par `ATD-` (modificateur du firmware, relevé dans
  `~/Oric1/src/io/serial_picowifi.c`).
- Débit : `serial_init` d'OricTel programme 1200 bauds ; un scénario complet
  (3 requêtes) prend ~2 min de temps émulé — à revoir au sprint 2.

## [0.0.1] — 2026-09-17 — Sprint 0 : cadrage
### Ajouté
- Dépôt créé (identité bmarty, hooks `commit-msg`/`pre-commit` + `secscan`
  repris de Neo6502ProphetGui), règles du projet, README, ROADMAP, `docs/CADRAGE.md`.
- Décisions PO : client Oric validé (« oui »), pas de matériel LOCI (« non »),
  validation en émulation Phosphoric (« émulation »).
