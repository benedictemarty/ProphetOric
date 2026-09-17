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
    if [ -n "$keys" ]; then
        "$EMU" -r "$ROM" -t "$TAP" -f --loci --serial picowifi:Test --serial-buffer 4096 --headless --realtime \
            --cycles $((at + 100000)) --type-keys "$keys" --screenshot-text-at "$at:$OUT/$name.txt" >"$OUT/$name.log" 2>&1
    else
        "$EMU" -r "$ROM" -t "$TAP" -f --loci --serial picowifi:Test --serial-buffer 4096 --headless --realtime \
            --cycles $((at + 100000)) --screenshot-text-at "$at:$OUT/$name.txt" >"$OUT/$name.log" 2>&1
    fi
    if [ "$mode" = ref ]; then cp "$OUT/$name.txt" "$REF/$name.txt"; echo "REF  $name"; cat "$OUT/$name.txt"
    elif cmp -s "$OUT/$name.txt" "$REF/$name.txt"; then echo "PASS $name"
    else echo "FAIL $name"; diff "$REF/$name.txt" "$OUT/$name.txt" | head -20; fail=1; fi
}
scenario cats   ""                  40000000    # catégories (/cat?platform=oric) : oric-games (1) — dune protégé caché
scenario list   "20000000:\n"       80000000    # liste oric-games : Zorg seul
scenario fiche  "20000000:\n\p4\n"  140000000   # fiche Zorg : auteur, description repliée
echo "----"; [ $fail -eq 0 ] && echo "OK" || echo "ECHEC"
exit $fail
