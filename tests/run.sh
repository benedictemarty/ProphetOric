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
for p in $(pgrep -f "prophetd -config $OUT/prophet.yml"); do kill "$p"; done; sleep 0.3   # reliquat d'une passe interrompue
PROPHET_PASSWORD=sesame-test "$PROPHETD" -config "$OUT/prophet.yml" >"$OUT/prophetd.log" 2>&1 & PD=$!
trap 'kill $PD 2>/dev/null' EXIT
for i in $(seq 1 30); do curl -s -o /dev/null http://127.0.0.1:18994/cat && break; sleep 0.1; done

EXTRA=""       # options Phosphoric supplémentaires pour un scénario (ex. --serial-baud 1200)
scenario() {   # nom | frappes (--type-keys, après le chargement) | cycles de capture
    local name=$1 keys=$2 at=$3
    [ -n "${ONLY:-}" ] && [ "$ONLY" != "$name" ] && return 0
    rm -rf "$OUT/flash_$name"; mkdir -p "$OUT/flash_$name"          # flash root du LOCI (0:) propre à chaque scénario
    [ -f "$OUT/cfg_$name" ] && cp "$OUT/cfg_$name" "$OUT/flash_$name/PROPHET.CFG"
    if [ -n "$keys" ]; then
        "$EMU" -r "$ROM" -t "$TAP" -f --loci --loci-usb none --loci-flash "$OUT/flash_$name" --serial picowifi:Test ${EXTRA:---serial-buffer 4096} --headless --realtime \
            --cycles $((at + 100000)) --type-keys "$keys" --screenshot-text-at "$at:$OUT/$name.txt" >"$OUT/$name.log" 2>&1
    else
        "$EMU" -r "$ROM" -t "$TAP" -f --loci --loci-usb none --loci-flash "$OUT/flash_$name" --serial picowifi:Test ${EXTRA:---serial-buffer 4096} --headless --realtime \
            --cycles $((at + 100000)) --screenshot-text-at "$at:$OUT/$name.txt" >"$OUT/$name.log" 2>&1
    fi
    if [ "$mode" = ref ]; then cp "$OUT/$name.txt" "$REF/$name.txt"; echo "REF  $name"; cat "$OUT/$name.txt"
    elif cmp -s "$OUT/$name.txt" "$REF/$name.txt"; then echo "PASS $name"
    else echo "FAIL $name"; diff "$REF/$name.txt" "$OUT/$name.txt" | head -20; fail=1; fi
}
scenario cats   ""                  12000000    # catégories (/cat?platform=oric) : oric-games (2) — dune protégé caché
scenario list   "12000000:\n"       20000000    # liste oric-games : Zorg seul
scenario fiche  "12000000:\n\p5\n"  26000000   # fiche Zorg : auteur, description repliée
# téléchargement (g) : zorg.tap écrit sur le flash du LOCI, identique à l'original
scenario dl     "12000000:\n\p5\n\p5g" 60000000   # zorg.tap = 20 Ko à motif (9600 bauds ≈ 21 s)   # \pN : un seul chiffre
if cmp -s "$OUT/flash_dl/zorg.tap" tests/repo/oric-games/zorg/zorg.tap; then echo "PASS dl_file (zorg.tap identique sur le LOCI)"
else echo "FAIL dl_file"; ls -l "$OUT/flash_dl"; fail=1; fi
# recherche (s) : "zorg" → liste de résultats (1 : Zorg ; Dune est réservé)
scenario search "12000000:szorg\n"   22000000
# lancement (l) : .tap monté sur le LOCI, retour au BASIC, CLOAD"" charge et lance le programme autorun
scenario launch "12000000:shello\n\p5\n\p5g\p5l\p2CLOAD\"\"\n" 45000000   # via la recherche : Hello Tape (BASIC autorun)
if grep -q "HELLO FROM PROPHET TAP" "$OUT/launch.txt"; then echo "PASS launch_run (programme charge par CLOAD depuis la cassette montee)"
else echo "FAIL launch_run"; grep -v "^$" "$OUT/launch.txt" | head -8; fail=1; fi
# disquette : paquet .dsk (Sedoric généré à la volée depuis ~/Oric1/disks/SEDO40u.DSK, hors git) →
# g télécharge, l monte en A et MIA_BOOT → Sedoric démarre depuis la disquette téléchargée
BASE=$HOME/Oric1/disks/SEDO40u.DSK
if [ -n "${LONG:-}" ] && [ -f "$BASE" ] && [ -x "$HOME/Oric1/tap2sedoric" ]; then
    mkdir -p tests/repo/oric-utils/hellodsk
    printf 'title: Hello Disk\nauthor: Test\nplatform: oric\n' > tests/repo/oric-utils/hellodsk/desc.yaml
    "$HOME/Oric1/tap2sedoric" tests/repo/oric-games/hello/hello.tap -o tests/repo/oric-utils/hellodsk/hello.dsk -b "$BASE" -n HELLO.BAS -a >/dev/null 2>&1
    curl -s -o /dev/null http://127.0.0.1:18994/refresh; kill $PD 2>/dev/null; sleep 0.3      # rescan : relance du prophetd de test
    PROPHET_PASSWORD=sesame-test "$PROPHETD" -config "$OUT/prophet.yml" >>"$OUT/prophetd.log" 2>&1 & PD=$!
    for i in $(seq 1 30); do curl -s -o /dev/null http://127.0.0.1:18994/cat && break; sleep 0.1; done
    EXTRA="--serial-buffer 32" scenario dsk "15000000:j\p1\n\p9\n\p9g$(printf '\\p9%.0s' $(seq 1 130))l" 1220000000   # 1 Mo à 9600 bauds ≈ 18 min, anneau 32
    if grep -q "SEDORIC DOS" "$OUT/dsk.txt"; then echo "PASS dsk_boot (hello.dsk telecharge, monte en A, MIA_BOOT : Sedoric demarre)"
    else echo "FAIL dsk_boot"; grep -v "^$" "$OUT/dsk.txt" | head -6; fail=1; fi
    rm -rf tests/repo/oric-utils
else echo "SKIP dsk_boot (LONG=1 : 1 Mo a 9600 bauds = 18 min ; SEDO40u.DSK + tap2sedoric requis)"; fi
# PROPHET.CFG avec le mot de passe : la liste montre aussi Dune Explorer (zone réservée)
printf '127.0.0.1\n18994\n\nsesame-test\n' > "$OUT/cfg_secret"
scenario secret "12000000:\n"       20000000
# écran de configuration : c, hôte, port, dossier JEUX, mot de passe → PROPHET.CFG (4 lignes) et dossier utilisé
scenario config "12000000:c\n\nJEUX\nsesame-test\nb\p5\n\p5\n\p5g" 50000000
# (le clavier émulé tape en minuscules : dossier « jeux » ; avec le mot de passe, Dune Explorer est 1er de la liste)
if [ "$(cat "$OUT/flash_config/PROPHET.CFG" 2>/dev/null)" = "$(printf '127.0.0.1\n18994\njeux\nsesame-test')" ] \
   && cmp -s "$OUT/flash_config/jeux/dune.tap" tests/repo/oric-games/dune/dune.tap; then echo "PASS config_file (PROPHET.CFG ecrit, jeux/dune.tap = paquet protege)"
else echo "FAIL config_file"; cat "$OUT/flash_config/PROPHET.CFG" 2>/dev/null; ls -R "$OUT/flash_config" | head; fail=1; fi
# débit réaliste : 1200 bauds, anneau de 32 octets comme le firmware LOCI devant le 6551 → fiche + téléchargement intacts
EXTRA="--serial-buffer 32" scenario baud "12000000:\n\p9\p9\n\p9\p9g" 120000000   # anneau de 32 octets (firmware LOCI) à 9600 bauds
if cmp -s "$OUT/flash_baud/zorg.tap" tests/repo/oric-games/zorg/zorg.tap && grep -q "termine : 1 fichier" "$OUT/baud.txt"; then echo "PASS baud_ring32 (20 Ko intacts a 9600 bauds avec un anneau de 32 octets)"
else echo "FAIL baud_ring32"; tail -1 "$OUT/baud.txt"; fail=1; fi
echo "----"; [ $fail -eq 0 ] && echo "OK" || echo "ECHEC"
exit $fail
