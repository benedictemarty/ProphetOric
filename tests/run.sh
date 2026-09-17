#!/usr/bin/env bash
# Scénarios ProphetOric sous Phosphoric (headless, temps réel) : LOCI + modem PicoWiFi
# émulé (vraies sockets) → prophetd local (tests/repo, port 18994). Captures texte
# comparées aux références (tests/ref/*.txt). `tests/run.sh <tap> ref` régénère.
set -u
cd "$(dirname "$0")/.." || exit 1
TAP=${1:-build/prophet-test.tap}; mode=${2:-check}
EMU=${EMU:-$HOME/Oric1/oric1-emu}; ROM=${EMU_ROM:-$HOME/Oric1/roms/basic11b.rom}
PROPHETD=${PROPHETD:-$HOME/Neo6502Prophet/bin/prophetd}
OUT=tests/out; REF=tests/ref; mkdir -p "$OUT" "$REF"; fail=0
[ -x "$PROPHETD" ] || (cd "$HOME/Neo6502Prophet" && make -s build)
printf 'datafolder: %s/tests/repo\nport: 18994\nbind: 127.0.0.1\nmax_ipp: 50\n' "$PWD" > "$OUT/prophet.yml"
PROPHET_PASSWORD=sesame-test "$PROPHETD" -config "$OUT/prophet.yml" >"$OUT/prophetd.log" 2>&1 & PD=$!
trap 'kill $PD 2>/dev/null' EXIT
for i in $(seq 1 30); do curl -s -o /dev/null http://127.0.0.1:18994/cat && break; sleep 0.1; done

scenario() {   # nom | frappes (--type-keys, après le chargement) | cycles de capture
    local name=$1 keys=$2 at=$3
    [ -n "${ONLY:-}" ] && [ "$ONLY" != "$name" ] && return 0
    rm -rf "$OUT/flash_$name"; mkdir -p "$OUT/flash_$name"          # flash root du LOCI (0:) propre à chaque scénario
    [ -f "$OUT/cfg_$name" ] && cp "$OUT/cfg_$name" "$OUT/flash_$name/PROPHET.CFG"
    if [ -n "$keys" ]; then
        "$EMU" -r "$ROM" -t "$TAP" -f --loci --loci-flash "$OUT/flash_$name" --serial picowifi:Test --serial-buffer 4096 --headless --realtime \
            --cycles $((at + 100000)) --type-keys "$keys" --screenshot-text-at "$at:$OUT/$name.txt" >"$OUT/$name.log" 2>&1
    else
        "$EMU" -r "$ROM" -t "$TAP" -f --loci --loci-flash "$OUT/flash_$name" --serial picowifi:Test --serial-buffer 4096 --headless --realtime \
            --cycles $((at + 100000)) --screenshot-text-at "$at:$OUT/$name.txt" >"$OUT/$name.log" 2>&1
    fi
    if [ "$mode" = ref ]; then cp "$OUT/$name.txt" "$REF/$name.txt"; echo "REF  $name"; cat "$OUT/$name.txt"
    elif cmp -s "$OUT/$name.txt" "$REF/$name.txt"; then echo "PASS $name"
    else echo "FAIL $name"; diff "$REF/$name.txt" "$OUT/$name.txt" | head -20; fail=1; fi
}
scenario cats   ""                  12000000    # catégories (/cat?platform=oric) : oric-games (1) — dune protégé caché
scenario list   "12000000:\n"       20000000    # liste oric-games : Zorg seul
scenario fiche  "12000000:\n\p5\n"  26000000   # fiche Zorg : auteur, description repliée
# téléchargement (g) : zorg.tap écrit sur le flash du LOCI, identique à l'original
scenario dl     "12000000:\n\p5\n\p5g" 40000000   # \pN : un seul chiffre
if cmp -s "$OUT/flash_dl/zorg.tap" tests/repo/oric-games/zorg/zorg.tap; then echo "PASS dl_file (zorg.tap identique sur le LOCI)"
else echo "FAIL dl_file"; ls -l "$OUT/flash_dl"; fail=1; fi
# PROPHET.CFG avec le mot de passe : la liste montre aussi Dune Explorer (zone réservée)
printf '127.0.0.1\n18994\n\nsesame-test\n' > "$OUT/cfg_secret"
scenario secret "12000000:\n"       20000000
# écran de configuration : c, hôte, port, dossier JEUX, mot de passe → PROPHET.CFG (4 lignes) et dossier utilisé
scenario config "12000000:c\n\nJEUX\nsesame-test\nb\p5\n\p5\n\p5g" 50000000
# (le clavier émulé tape en minuscules : dossier « jeux » ; avec le mot de passe, Dune Explorer est 1er de la liste)
if [ "$(cat "$OUT/flash_config/PROPHET.CFG" 2>/dev/null)" = "$(printf '127.0.0.1\n18994\njeux\nsesame-test')" ] \
   && cmp -s "$OUT/flash_config/jeux/dune.tap" tests/repo/oric-games/dune/dune.tap; then echo "PASS config_file (PROPHET.CFG ecrit, jeux/dune.tap = paquet protege)"
else echo "FAIL config_file"; cat "$OUT/flash_config/PROPHET.CFG" 2>/dev/null; ls -R "$OUT/flash_config" | head; fail=1; fi
echo "----"; [ $fail -eq 0 ] && echo "OK" || echo "ECHEC"
exit $fail
