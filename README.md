# ProphetOric — client Prophet pour Oric (LOCI + modem Wi‑Fi)

Client du dépôt de programmes **Prophet** (`prophet.3617.fr`, serveur
[Neo6502Prophet](../Neo6502Prophet)) pour **Oric 1 / Atmos** équipé du
**LOCI** (cartouche RP2040 de sodiumlb) et du modem **PicoWiFiModemUSB** :
catalogue Oric (zone réservée, mot de passe), fiche, téléchargement du
`.tap`/`.dsk` sur le stockage du LOCI, lancement.

**État : sprint 0 (cadrage), aucun code.** Validation **en émulation
uniquement** (Phosphoric, `~/Oric1`) : le PO n'a pas le matériel.

| | |
|---|---|
| Transport | ACIA 6551 à `$0380` (LOCI) → modem Hayes (`ATD hôte:port` → TCP brut) → HTTP/1.1 `GET` en clair, port 8998 |
| Stockage | API MIA du LOCI (`$03AF` : OPEN/WRITE/READDIR…), clé USB / SD / flash |
| Protocole | `cli` de Prophet (`ResponseFormat: cli`), `?platform=oric`, `X-Prophet-Password`, `Range` |
| Chaîne | cc65 (`-t atmos`, comme OricTel) ; tests hôte gcc + Phosphoric headless |
| Réutilisé | OricTel (`~/orictel`) : driver ACIA `serial_asm.s`, file d'émission, `at_modem.c` |

Voir `docs/CADRAGE.md` (architecture, risques, plan de tests) et `ROADMAP.md`.
