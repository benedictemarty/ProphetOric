#!/usr/bin/env bash
# secscan.sh — contrôle de sécurité avant commit (Neo6502ProphetGui)
#
# Usage :
#   tools/secscan.sh --staged          # fichiers indexés (utilisé par le hook)
#   tools/secscan.sh FICHIER...        # fichiers explicites (tests, audit)
#
# Vérifie : 1) noms de fichiers interdits (clés, .env…)
#           2) motifs de secrets dans le contenu
#           3) taille maximale (MAX_KB, défaut 2048 Ko)
# Code retour 0 = propre, 1 = au moins un problème, 2 = mauvais usage.
set -u

MAX_KB=${MAX_KB:-2048}
status=0

FORBIDDEN_NAMES='(^|/)(\.env(\..*)?|id_rsa[^/]*|id_ed25519[^/]*|.*\.(pem|key|p12|pfx|secret|token))$'

# Motifs de secrets (ERE). Chaque ligne = un motif.
PATTERNS=(
  'AKIA[0-9A-Z]{16}'                                   # AWS access key
  'gh[pousr]_[A-Za-z0-9]{36,}'                          # GitHub token
  'github_pat_[A-Za-z0-9_]{22,}'                        # GitHub fine-grained PAT
  'sk-[A-Za-z0-9_-]{20,}'                               # clé API (préfixe sk-)
  'xox[baprs]-[A-Za-z0-9-]{10,}'                        # Slack
  '-----BEGIN [A-Z ]*PRIVATE KEY-----'                  # clé privée PEM
  '(password|passwd|pwd|secret|api[_-]?key|token)[[:space:]]*[=:][[:space:]]*["'"'"'][^"'"'"']{6,}["'"'"']'  # affectation en dur
)

if [ $# -eq 0 ]; then
  echo "usage: $0 --staged | FICHIER..." >&2
  exit 2
fi

if [ "$1" = "--staged" ]; then
  mapfile -t files < <(git diff --cached --name-only --diff-filter=ACMR)
  reader() { git show ":$1"; }
else
  files=("$@")
  reader() { cat -- "$1"; }
fi

for f in "${files[@]}"; do
  # 1) nom de fichier interdit
  if printf '%s\n' "$f" | grep -Eq "$FORBIDDEN_NAMES"; then
    echo "SECSCAN: fichier interdit (secret/clé) : $f"
    status=1
    continue
  fi
  # 2) taille
  size=$(reader "$f" | wc -c)
  if [ "$size" -gt $((MAX_KB * 1024)) ]; then
    echo "SECSCAN: fichier trop volumineux ($((size / 1024)) Ko > ${MAX_KB} Ko) : $f"
    status=1
  fi
  # 3) contenu (fichiers texte seulement)
  if reader "$f" | grep -Iq . ; then
    for p in "${PATTERNS[@]}"; do
      hits=$(reader "$f" | grep -EnI -e "$p" | grep -v 'secscan:allow' | head -5)
      if [ -n "$hits" ]; then
        echo "SECSCAN: secret potentiel dans $f (motif: $p)"
        echo "$hits" | sed 's/^/    /'
        status=1
      fi
    done
  fi
done

[ $status -eq 0 ] && echo "SECSCAN: OK (${#files[@]} fichier(s))"
exit $status
