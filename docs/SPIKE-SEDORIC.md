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

## Suite proposée (décision PO)

- **S2** : pilote série « lire en émettant » pour l'ACIA sans tampon, catalogue reçu
  sans LOCI ; puis téléchargement d'un petit `.tap` en tranches de ~4 Ko dans un tampon
  RAM, écrit par `SAVE` (pas de réception pendant l'écriture disque).
- **S3** : gros fichiers (écriture en flux XWDESC/XSVSEC), disque plein, coupures.
- **S4** : `.dsk` (exclure, ou copie secteur par secteur sur le lecteur B).
- Rappel : sans LOCI, **aucun transport réel vérifié** (carte ACIA + modem série).
