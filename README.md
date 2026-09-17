# ProphetOric — client Prophet pour Oric (LOCI + modem Wi‑Fi)

Client du dépôt de programmes **Prophet** (`prophet.3617.fr`, serveur
[Neo6502Prophet](../Neo6502Prophet)) pour **Oric 1 / Atmos** équipé du
**LOCI** (cartouche RP2040 de sodiumlb) et du modem **PicoWiFiModemUSB** :
catalogue Oric (zone réservée, mot de passe), fiche, téléchargement du
`.tap`/`.dsk` sur le stockage du LOCI, lancement.

**État : sprint 4** (`build/prophet.tap`, ~17 Ko) : catégories / recherche →
liste paginée → fiche → `g` télécharge sur le LOCI → `l` monte la cassette
(`CLOAD""`) ou démarre la disquette (`MIA_BOOT`) ; `PROPHET.CFG` et écran de
configuration (`c`) ; ACIA à **9600 bauds**, vérifié avec l'anneau de 32 octets
du firmware LOCI ; via le modem PicoWiFi émulé de Phosphoric (vraies sockets). Validation **en émulation uniquement**
(Phosphoric, `~/Oric1`) : le PO n'a pas le matériel.

```
make            # build/prophet.tap (prophet.3617.fr:8998)
make run        # Phosphoric SDL + LOCI + modem PicoWiFi émulé → prophet.3617.fr
make test       # tests hôte (cli, http sur faux modem) + 9 scénarios Phosphoric headless → prophetd local (ONLY=nom ; LONG=1 : disquette 1 Mo, 18 min)
```
Touches : j/k (flèches) choisir, entrée ouvrir, b retour, n/p page, s chercher, g télécharger (fiche), l lancer (cassette montée + CLOAD"" / disquette + MIA_BOOT), c configuration, q quitter.

| | |
|---|---|
| Transport | ACIA 6551 à `$0380` (LOCI) → modem Hayes PicoWiFiModemUSB (`ATD-hôte:port` : `-` = sans telnet, sinon CR→CR NUL ; `ATD-#hôte:443` = TLS terminé par le modem, firmware ≥ 0.2.0, **non vérifié sur matériel**) → HTTP/1.1 `GET` |
| Stockage | API MIA du LOCI (`$03AF` : OPEN/WRITE/READDIR…), clé USB / SD / flash |
| Protocole | `cli` de Prophet (`ResponseFormat: cli`), `?platform=oric`, `X-Prophet-Password`, `Range` |
| Chaîne | cc65 (`-t atmos`, comme OricTel) ; tests hôte gcc + Phosphoric headless |
| Réutilisé | OricTel (`~/orictel`) : driver ACIA `serial_asm.s`, file d'émission, `at_modem.c` |

Voir `docs/CADRAGE.md` (architecture, risques, plan de tests) et `ROADMAP.md`.
