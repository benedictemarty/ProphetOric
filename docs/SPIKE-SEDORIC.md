# Spike S1 — stockage Sedoric sans LOCI (2026-10-01)

Demande PO (2026-09-30) : « sans LOCI, enregistrer sur une disquette Sedoric ».
Étude préalable : faisable **en émulation**, 3 à 4 sprints ; **bloquant matériel** :
le PicoWiFiModemUSB se branche sur le LOCI — sans LOCI il faut une carte ACIA RS‑232
et un modem Wi‑Fi série (non vérifié). Ce spike lève les deux premiers risques
techniques. Tout est en émulation (Phosphoric), rien sur matériel.

`make spike-sedoric` (≈ 3 min, `spikes/sedoric/run.sh`, copie figée de l'émulateur
dans `spikes/sedoric/out/` : une autre session peut reconstruire `~/Oric1/oric1-emu`).

## Résultats

| Essai | Résultat |
|---|---|
| **sedoric_save** : programme cc65 lancé sous SEDORIC V4.0 (`SEDO40u.DSK` rendu « nu »), `SAVE"SPIKE.BIN",A#…,E#…` exécuté par le vecteur « ! » `$0467` | **PASS** — le programme reprend la main ; SPIKE.BIN relu **par SEDORIC lui‑même** : 256 octets identiques |
| **sedoric_prophet_start** : ProphetOric 0.9.1 compilé avec `-DPROPHET_ACIA=0x031C`, injecté sur la disquette, lancé sous SEDORIC, modem picowifi émulé **sans LOCI** | **PASS** — l'ACIA est détecté en `$031C` à côté du Microdisc, écran « LOCI absent » |
| **sedoric_modem** : dialogue AT puis catalogue | **bloquant** — « pas de modem (ATZ) » |

## Ce qui a été appris

- **Page zéro** : cc65 range ses variables en `$E2-$FB`, là où BASIC/SEDORIC ont
  CHRGET (TXTPTR = `$E9/$EA`). `sed.s` les sauve, remet la copie de CHRGET relevée au
  prompt SEDORIC V4.0 (`E6 E9 D0 02 E6 EA AD 36 00 C9 20 F0 F3 4C 00 04 …` : SEDORIC
  détourne CHRGET vers la page 4), appelle `$0467`, restaure. **Propre à cette version**.
- **Présence de SEDORIC V4.0** : `$0467` = `A9 AE A0 D3` (appel de l'interpréteur `$D3AE`).
- Ligne de commande terminée par `$00`, jamais CR (`~/Oric1/docs/SEDORIC.md`).
- **Lancement** : l'amorce BASIC du `.tap` cc65 (`CALL #50D`) donne « BREAK ON BYTE
  #050A » sous SEDORIC → injection en code machine (`tap2sedoric -e 050D`). Le
  lancement AUTO marche pour le petit SPIKE.COM mais **pas** pour ProphetOric (31 Ko),
  ni `:CALL#50D` dans l'INIST ; `CALL#50D` tapé au prompt fonctionne. Cause **non cherchée**.
- **Bloquant modem** (trace `--serial-trace`) : ProphetOric envoie `ATZ\r` ; le modem
  renvoie l'écho de chaque caractère **pendant** l'émission ; le 6551 n'a qu'un octet de
  réception → `OVERRUN (RDR not read)` dès le 2e caractère. Avec le LOCI, l'anneau de 32
  octets du firmware absorbe l'écho. Sans LOCI (vrai 6551 comme dans Phosphoric à
  `--serial-baud 9600` sans `--serial-buffer`) il faut que le pilote série **lise en
  émettant** (tampon logiciel) — c'est le premier travail de S2.
- `sedoric-info` place SPIKE.BIN en piste 74 d'une image de 42 pistes physiques ; la
  relecture marche (cf. note VTOC « D/80/17 » de `~/Oric1/docs/SEDORIC.md`) : non élucidé.

## S2, première partie (2026-10-01, branche `s2-sedoric`)

Pilote série pour l'ACIA **sans tampon** (ProphetOric compilé `-DPROPHET_ACIA=0x031C
-DSERIAL_NO_FIFO`), mesuré à la trace série de Phosphoric (`--serial-baud 9600`, sans
`--serial-buffer`, donc fidèle au 6551) :

| Correctif | Effet |
|---|---|
| anneau de réception logiciel de 64 octets (`serial.c`), rempli pendant l'émission (`serial_tx_flush`, file pleine) et en fin de `flush` | écho de `ATZ` / `ATD…` capté |
| `serial_wait(ms)` : attente qui surveille la réception par pas de 0,1 ms, au lieu de sommeils de 1 à 2 ms (`at_wait_response`, `rx_byte`) | `OK`, `CONNECT` reçus (un octet arrive toutes les 1,04 ms à 9600 bauds) |
| `serial_recv` relève l'ACIA pendant qu'il vide l'anneau | plus de perte après l'écho de `ATD` |
| interruptions coupées pendant l'échange HTTP (`SERIAL_NO_FIFO` seulement) | l'IRQ 100 Hz de la ROM / SEDORIC ne vole plus de temps-octet |
| `parse_dec` par paquets de 4 chiffres en 16 bits (+ relevés) | la multiplication 32 bits par chiffre dépassait le temps-octet |

Résultat intermédiaire : catalogue reçu, mais **un octet écrasé à deux points fixes** de
chaque réponse (après la ligne d'état, jonction en-têtes / corps) : la fiche échouait
(`Title:` amputé). Cause : un traitement C de fin de ligne plus long que le temps-octet,
IRQ coupées — non réductible proprement par des relevés.

**Réception sous interruption** (`src/serial_irq.s`, build `-DSERIAL_NO_FIFO` seulement) :
pendant chaque échange HTTP, l'IRQ de réception de l'ACIA est autorisée (commande `$01`)
et une routine installée sur le vecteur RAM `$0244/$0245` (SEDORIC y met `JMP $0488`)
range chaque octet dans l'anneau ; le Timer 1 du VIA (100 Hz) est seulement acquitté (son
traitement ROM, clavier/curseur, est sauté pendant l'échange) ; toute autre IRQ est chaînée
vers le gestionnaire d'origine. En fin d'échange : commande `$03`, vecteur rétabli.

**Résultat : 0 OVERRUN** sur 7 requêtes (`/cat`, `/list/en-developpement`, `/list/all`,
`/app/zorg`, `/files/zorg`, `/crc32/zorg`, `/requires/zorg`) : **catalogue et fiche complets
sous SEDORIC sans LOCI**, en émulation (Phosphoric, 9600 bauds réels, 6551 sans tampon).
La version LOCI n'utilise ni `serial_irq.s` ni les relevés de l'ACIA (`serial_rx_grab`, réservé à
`-DSERIAL_NO_FIFO`) : une première version les faisait passer par l'anneau logiciel et saturait
l'anneau de 32 octets du LOCI au téléchargement (pic 32/32) ; corrigé, pic 18/32 (19-21 avant S2).

## Suite proposée (décision PO)

- **S2, suite** : téléchargement d'un petit `.tap` en tranches de ~4 Ko dans un tampon
  RAM, écrit par `SAVE` (pas de réception pendant l'écriture disque).
- **S3** : gros fichiers (écriture en flux XWDESC/XSVSEC), disque plein, coupures.
- **S4** : `.dsk` (exclure, ou copie secteur par secteur sur le lecteur B).
- Rappel : sans LOCI, **aucun transport réel vérifié** (carte ACIA + modem série).
