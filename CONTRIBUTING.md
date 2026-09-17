# Contribuer à ProphetOric

Lire `README.md`, `ROADMAP.md`, `CHANGELOG.md` et `docs/CADRAGE.md` avant toute
modification.

## Règles du projet
- Méthode agile : chaque modification = code + tests + `CHANGELOG.md` + docs,
  puis commit signé `bmarty <bmarty@mailo.com>` (auteur unique ; les hooks de
  `.githooks/` refusent tout trailer `Co-Authored-By`). Activer les hooks :
  `git config core.hooksPath .githooks`.
- `make test` doit passer avant tout commit : tests hôte (gcc) + scénarios
  Phosphoric headless (`--realtime`) comparés aux références `tests/ref/`
  (`tests/run.sh <tap> ref` après inspection pour régénérer ; `ONLY=<nom>`
  pour un seul scénario ; `LONG=1` pour la disquette de 1 Mo).
- Protocole : format texte `cli` du serveur Prophet (celui de `prophet.neo`),
  `?platform=oric`, en-tête `X-Prophet-Password` ; ne rien inventer, les
  réponses de référence viennent du vrai serveur.
- Matériel cible = ce que Phosphoric émule fidèlement : ACIA 6551 à `$0380`
  (jamais `$03A0`), API MIA `$03AF` du LOCI, 6502 NMOS. Le projet n'a été
  validé qu'en émulation : marquer « non vérifié » ce qui ne l'a pas été sur
  matériel.
- Sources reprises d'OricTel (`serial*`, `at_modem*`, `tapehdr.s`, cfg) et de
  Neo6502ProphetGui (`cli.c`) : provenance en tête de fichier, licence
  EUPL‑1.2.
- Jamais `git add -A` sans `git status` ; ajouts ciblés.
