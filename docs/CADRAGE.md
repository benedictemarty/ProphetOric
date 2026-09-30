# Cadrage ProphetOric (sprint 0) — 2026-09-17

Ce document ne contient que des faits relevés dans `~/Oric1` (Phosphoric),
`~/orictel` (OricTel), `~/bbsoric` et `~/Neo6502Prophet`. Ce qui n'a pas été
vérifié est marqué **non vérifié**.

## 1. Cible matérielle (émulée)
- **Oric 1 / Atmos**, 6502 NMOS, 48 Ko, écran TEXT 40×28 à `$BB80`
  (HIRES possible plus tard). ROM BASIC 1.1 (`roms/basic11b.rom`).
- **LOCI** (sodiumlb, firmware de référence v0.3.1) émulé par Phosphoric
  `--loci` (`~/Oric1/docs/loci.md`) :
  - **ACIA 6551 à `$0380-$0383`** = modem Wi‑Fi PicoWiFiModemUSB (`--serial
    picowifi`). **Ne jamais adresser `$03A0`** (registres MIA : écrire dedans
    gèle le clavier — bug relevé dans `~/bbsoric/MESSAGE-terminal-team-LOCI.md`).
  - **API MIA** : op `$03AF`, xstack `$03AC` (512 octets), errno `$03AD/E`,
    stub `$03B0`, BUSY `$03B2` bit 7. 36/36 ops émulées : fichiers
    `OPEN/CLOSE/READ/WRITE_XSTACK/XRAM/LSEEK/UNLINK/RENAME`, répertoires
    `OPENDIR/CLOSEDIR/READDIR/MKDIR/GETCWD`, montage `MOUNT/UMOUNT`, TAP
    `SEEK/TELL/READ_HEADER`. Descripteurs fichiers 3‑18, répertoires 64+ ;
    errno FatFS = `32 + FRESULT`.
  - Stockage : flash `0:` (`--loci-flash DIR`), clés USB `1:`‑`4:`
    (`--loci-usb DIR`), image SD (`--loci-sdimg`).
- Sans LOCI (clone NG, Oric1NG) : pas de transport → hors périmètre.

## 2. Transport réseau
- Modem **Hayes** : `AT`, `ATZ`, `ATE0`, `ATH`, `ATD hôte:port` → `CONNECT` /
  `NO CARRIER`, puis TCP brut, `+++` pour revenir en commande (dialecte de
  Phosphoric `--serial modem`, `~/Oric1/docs/orictel-modem-hayes.md`).
  Différences éventuelles avec le vrai PicoWiFiModemUSB (`--serial picowifi`) :
  **à relever** dans `~/orictel/src/at_modem.c` (matcher ancré sur les lignes
  CR/LF, pas de sous-chaîne — leçon OricTel).
- ACIA 6551 réel : **1 octet de RX, pas de FIFO** → tout rendu écran pendant
  une rafale perd des octets (bug OricTel remonté par Dbug). Règle : recevoir
  d'abord dans un tampon, afficher ensuite ; `serial_asm.s` en polling pur.
- Débit : non vérifié (OricTel fonctionne à 9600/19200 sur le 6551 ; à mesurer
  sur les `.dsk` de 200‑300 Ko : plusieurs minutes probables).
- **TLS : aucun** sur ce modem (non vérifié) → HTTP 8998 en clair, mot de
  passe en clair (le serveur le sait : SECURITY.md de Neo6502Prophet).

## 3. Protocole Prophet (côté serveur : rien à ajouter)
- `GET /cat`, `/list/<cat>?platform=oric&ipp=N&page=P`, `/search/<clé>`,
  `/app/<id>`, `/files/<id>/<n>` (`Range` → 206), `/sha256/<id>/<n>` ;
  en‑têtes `ResponseFormat: cli`, `Connection: close`,
  `X-Prophet-Password: …` (zone `oric-*`, sinon seul Astéroric est visible).
- Format `cli` (relevé dans `~/Neo6502Prophet/docs/ANALYSE-AMONT.md`,
  parseur de référence `~/Neo6502ProphetGui/src/cli.c` + fixtures
  `tools/gen_fixture.py`) : `\x83id\x82 : titre\n\r`, `total,pages,page:`,
  `n:"nom","chemin/",…`. Le serveur tolère un espace avant CRLF.
- Catalogue Oric : 1 569 paquets, catégories `oric-games` (805),
  `oric-typeins` (371), `oric-utils` (229), `oric-demos` (139), `oric-misc`
  (25) ; formats `tap` 1 329, `dsk` 486, `zip` 461 (**zip : non ouvrable sur
  Oric**, à exclure ou à décompresser côté serveur — décision à prendre),
  `rom` 43.

## 4. Architecture du client (proposée)
```
src/serial_asm.s   driver ACIA 6551 $0380 (import OricTel, polling, tampon RX)
src/serial_tx.c    file d'émission non bloquante (import OricTel)
src/at_modem.c     AT/ATD/+++ ancré CR/LF (import OricTel)
src/http.c         GET + en-têtes, lecture jusqu'à \r\n\r\n, Content-Length/Range
src/cli.c          parseur cli (port du cli.c de ProphetGui, testé sur l'hôte)
src/loci.c         MIA : open/write_xstack/close/mkdir/readdir, montage TAP
src/ui.c           TEXT 40×28 : catégories, liste paginée, fiche, téléchargement
src/config.c       PROPHET.CFG sur le LOCI (hôte / port / dossier / mot de passe)
```
- Mémoire : programme sous `$9800` ; la zone HIRES `$A000-$B3FF`, libre en mode TEXT,
  porte le fichier de langue, la réponse HTTP (2 Ko) et la liste `/files` (`src/himem.h`, 0.9.1).
- SHA‑256 : le 6502 NMOS sans `phx`/`stz`… — l'asm 65C02 de ProphetGui n'est
  pas portable tel quel. **Décision 0.7.0** : CRC‑32 (`/crc32/<id>`, comme
  ProphetGui depuis 0.10), boucle NMOS `src/crc32_asm.s` (~40 cycles/octet)
  qui lit directement la XSTACK du LOCI ; calcul **après** réception (relecture
  du fichier), jamais pendant (anneau de 32 octets à 9600 bauds).
- Coupure en cours de corps : le modem émet `\r\nNO CARRIER (hh:mm:ss)\r\n`
  sur la ligne série ; `http_get_stream` l'écarte et la reprise se
  repositionne par `LSEEK` (op `$1A`, whence 2 = SEEK_SET — convention relevée
  dans Phosphoric, **non vérifiée sur matériel**).

## 5. Plan de tests (émulation seulement, D2)
- Hôte (gcc) : parseur `cli` sur fixtures réelles, machine d'états AT (comme
  `~/orictel` TEST_HOST), HTTP (en‑têtes, Range, tolérance).
- Phosphoric headless : `oric1-emu -r roms/basic11b.rom --loci --loci-usb DIR
  --serial modem --headless --realtime --type-keys C:TEXT --screenshot-text-at
  C:FILE` ; `--serial modem` ouvre de vraies sockets TCP → **prophetd local**
  (`tests/repo` avec un paquet Oric protégé, `PROPHET_PASSWORD`), comme
  `tests/run.sh` de ProphetGui. Vérifier le fichier écrit dans `DIR`
  (`cmp` côté hôte, CRC‑32 côté client).
- Références texte (`--screenshot-text-at`) plutôt que PPM tant que l'UI est
  en TEXT.

## 6. Questions ouvertes (PO)
1. `zip` : exclure côté client, ou le serveur les ouvre (`/files/<id>/<n>` →
   contenu) ? Ne rien faire tant que non décidé.
2. Lancement : `.tap` via montage TAP du LOCI puis `CLOAD` (à vérifier dans
   Phosphoric) ; `.dsk` via Microdisc du LOCI (`$0310`, `$0319`='L').
3. Chaîne : cc65 `-t atmos` (OricTel, connue) ou llvm‑mos‑oric (`~/llvm-mos-oric`) ?
   Proposition : cc65 pour réutiliser OricTel tel quel.
