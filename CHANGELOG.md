# Changelog — ProphetOric

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
