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

want() { [ -z "${ONLY:-}" ] || [ "$ONLY" = "$1" ]; }   # ONLY=<nom> : un seul scénario (et ses contrôles)
EXTRA=""       # options Phosphoric supplémentaires pour un scénario (ex. --serial-baud 1200)
scenario() {   # nom | frappes (--type-keys, après le chargement) | cycles de capture
    local name=$1 keys=$2 at=$3
    want "$name" || return 0
    rm -rf "$OUT/flash_$name"; mkdir -p "$OUT/flash_$name"          # flash root du LOCI (0:) propre à chaque scénario
    [ -f "$OUT/cfg_$name" ] && cp "$OUT/cfg_$name" "$OUT/flash_$name/PROPHET.CFG"
    [ -n "${SEED:-}" ] && cp $SEED "$OUT/flash_$name/"              # fichiers pré-semés dans le flash (ex. microdis.rom)
    if [ -n "$keys" ]; then
        "$EMU" -r "$ROM" -t "$TAP" -f --loci --loci-usb none --loci-flash "$OUT/flash_$name" --serial picowifi:Test ${EXTRA:---serial-buffer 4096} --headless --realtime \
            --cycles $((at + 100000)) --type-keys "$keys" --screenshot-text-at "$at:$OUT/$name.txt" >"$OUT/$name.log" 2>&1
    else
        "$EMU" -r "$ROM" -t "$TAP" -f --loci --loci-usb none --loci-flash "$OUT/flash_$name" --serial picowifi:Test ${EXTRA:---serial-buffer 4096} --headless --realtime \
            --cycles $((at + 100000)) --screenshot-text-at "$at:$OUT/$name.txt" >"$OUT/$name.log" 2>&1
    fi
    if [ -n "${NOREF:-}" ]; then return 0                                    # capture non déterministe (roue) : contrôle à part
    elif [ "$mode" = ref ]; then cp "$OUT/$name.txt" "$REF/$name.txt"; echo "REF  $name"; cat "$OUT/$name.txt"
    elif cmp -s "$OUT/$name.txt" "$REF/$name.txt"; then echo "PASS $name"
    else echo "FAIL $name"; diff "$REF/$name.txt" "$OUT/$name.txt" | head -20; fail=1; fi
}
scenario cats   ""                  12000000    # écran principal : onglets (tous, oric-games, beta), grille de cartes — dune protégé caché
scenario list   "12000000:v"        20000000    # vue liste compacte (v) : Zorg, Beta Test [dev], Hello Tape
scenario fiche  "12000000:\n"       24000000   # fiche Zorg (1re carte) : auteur, description repliée, fichiers
# téléchargement (g) : zorg.tap écrit sur le flash du LOCI, identique à l'original
scenario dl     "12000000:\n\p5g" 60000000   # zorg.tap = 20 Ko à motif (9600 bauds ≈ 21 s)   # \pN : un seul chiffre
if ! want dl; then :; elif cmp -s "$OUT/flash_dl/zorg.tap" tests/repo/oric-games/zorg/zorg.tap; then echo "PASS dl_file (zorg.tap identique sur le LOCI)"
else echo "FAIL dl_file"; ls -l "$OUT/flash_dl"; fail=1; fi
# marqueur « déjà téléchargé » : empreinte du serveur (/crc32/zorg) et fichier à lancer
if ! want dl; then :; elif [ "$(cat "$OUT/flash_dl/.prophet/zorg.crc" 2>/dev/null)" = "$(printf 'C:%s\nT:zorg.tap\nD:' "$(curl -s -H 'ResponseFormat: cli' http://127.0.0.1:18994/crc32/zorg | tr -d '\r\n')")" ]; then echo "PASS dl_mark (.prophet/zorg.crc = empreinte du serveur, zorg.tap)"
else echo "FAIL dl_mark"; cat -A "$OUT/flash_dl/.prophet/zorg.crc" 2>/dev/null; fail=1; fi
# fiche d'un paquet déjà téléchargé : dl puis retour et réouverture → « deja telecharge (identique) », l direct
scenario installed "12000000:\n\p5g\p9\p9\p9\p9b\p5\n" 80000000
# catégorie en-developpement : carte « EN DEV » (l = 2e carte) puis fiche avec bandeau
scenario fiche_dev "12000000:l\p1\n" 22000000
# aide (?) : rappel des touches
scenario help "12000000:?" 14000000
# onglets : . → oric-games (Zorg, Hello seulement)
scenario tabs "12000000:." 17000000
# rendu de la grille (attributs série, double hauteur, carte choisie, bandeau EN DEV) : image PPM
if want grid_ppm; then
    rm -rf "$OUT/flash_grid"; mkdir -p "$OUT/flash_grid"
    "$EMU" -r "$ROM" -t "$TAP" -f --loci --loci-usb none --loci-flash "$OUT/flash_grid" --serial picowifi:Test --serial-buffer 4096 --headless --realtime \
        --cycles 16100000 --type-keys "12000000:l" --screenshot-at "16000000:$OUT/grid.ppm" >"$OUT/grid.log" 2>&1
    if [ "$mode" = ref ]; then cp "$OUT/grid.ppm" "$REF/grid.ppm"; echo "REF  grid_ppm"
    elif cmp -s "$OUT/grid.ppm" "$REF/grid.ppm"; then echo "PASS grid_ppm (grille de cartes : couleurs, double hauteur, EN DEV)"
    else echo "FAIL grid_ppm (tests/out/grid.ppm differe de tests/ref/grid.ppm)"; fail=1; fi
fi
# composants minimums (/requires/hello : loci>=0.3.1, picowifi) : avertissement sur la fiche
scenario fiche_req "12000000:shello\n\p5\n" 32000000
# indicateur d'activité : capture pendant le téléchargement (≈ 10 s après g) : roue -\|/ et compteur Ko en bas à droite
NOREF=1 scenario spin   "12000000:\n\p5g" 28000000
if ! want spin; then :; elif tail -1 "$OUT/spin.txt" | grep -qE "^telechargement zorg.tap +[0-9]+ K[-\\|/]$"; then echo "PASS spin_wheel (indicateur -\\|/ et Ko pendant le telechargement)"
else echo "FAIL spin_wheel"; tail -1 "$OUT/spin.txt" | cat -A; fail=1; fi
# recherche (s) : "zorg" → liste de résultats (1 : Zorg ; Dune est réservé)
scenario search "12000000:szorg\n"   22000000
# lancement (l) : .tap monté sur le LOCI, retour au BASIC, CLOAD"" charge et lance le programme autorun
scenario launch "12000000:shello\n\p5\n\p5g\p5l\p2CLOAD\"\"\n" 45000000   # via la recherche : Hello Tape (BASIC autorun)
if ! want launch; then :; elif grep -q "HELLO FROM PROPHET TAP" "$OUT/launch.txt"; then echo "PASS launch_run (programme charge par CLOAD depuis la cassette montee)"
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
    # le flash du vrai LOCI contient microdis.rom ; Phosphoric ne le résout pas depuis roms/ (basic11b.rom, si)
    SEED="$HOME/Oric1/roms/microdis.rom" EXTRA="--serial-buffer 32" scenario dsk "15000000:sdisk\n\p9\n\p9g$(printf '\\p9%.0s' $(seq 1 130))l" 1220000000   # 1 Mo à 9600 bauds ≈ 18 min, anneau 32
    if ! want dsk; then :; elif grep -q "SEDORIC DOS" "$OUT/dsk.txt"; then echo "PASS dsk_boot (hello.dsk telecharge, monte en A, MIA_BOOT : Sedoric demarre)"
    else echo "FAIL dsk_boot"; grep -v "^$" "$OUT/dsk.txt" | head -6; fail=1; fi
    rm -rf tests/repo/oric-utils
else echo "SKIP dsk_boot (LONG=1 : 1 Mo a 9600 bauds = 18 min ; SEDO40u.DSK + tap2sedoric requis)"; fi
# PROPHET.CFG avec le mot de passe : la liste montre aussi Dune Explorer (zone réservée)
printf '127.0.0.1\n18994\n\nsesame-test\n' > "$OUT/cfg_secret"
scenario secret ""                  16000000
# écran de configuration : c, hôte, port, dossier JEUX, mot de passe → PROPHET.CFG (4 lignes) et dossier utilisé
scenario config "12000000:c\n\n\nnjeux\n\n sesame-test\nb\p5\n\p5g" 60000000   # hote, HTTP, port, explorateur : n(ouveau) jeux, entrer, espace ; mot de passe
# (le clavier émulé tape en minuscules : dossier « jeux » ; avec le mot de passe, Dune Explorer est 1er de la liste)
if ! want config; then :; elif [ "$(cat "$OUT/flash_config/PROPHET.CFG" 2>/dev/null)" = "$(printf '127.0.0.1\n18994\njeux\nsesame-test')" ] \
   && cmp -s "$OUT/flash_config/jeux/dune.tap" tests/repo/oric-games/dune/dune.tap; then echo "PASS config_file (PROPHET.CFG ecrit, jeux/dune.tap = paquet protege)"
else echo "FAIL config_file"; cat "$OUT/flash_config/PROPHET.CFG" 2>/dev/null; ls -R "$OUT/flash_config" | head; fail=1; fi
# volumes : une clé USB émulée (--loci-usb) → l'explorateur commence par les volumes ; « 1: » choisi,
# dossier jeux créé dessus, téléchargement dans N:jeux (N = numéro attribué par l'émulateur), config (flash 0:) = "N:jeux"
rm -rf "$OUT/usb1"; mkdir -p "$OUT/usb1"
EXTRA="--serial-buffer 4096 --loci-usb $OUT/usb1" scenario volumes "12000000:c\n\n\nj\nnjeux\n\n sesame-test\nb\p5\n\p5g" 60000000
if ! want volumes; then :; elif sed -n 3p "$OUT/flash_volumes/PROPHET.CFG" 2>/dev/null | grep -qE "^[1-4]:jeux$" && cmp -s "$OUT/usb1/jeux/dune.tap" tests/repo/oric-games/dune/dune.tap; then echo "PASS volumes (volume USB choisi dans l'explorateur, fichier sur la cle USB, config sur le flash)"
else echo "FAIL volumes"; cat "$OUT/flash_volumes/PROPHET.CFG" 2>/dev/null; ls -R "$OUT/usb1" | head -5; grep -v "^$" "$OUT/volumes.txt" | tail -3; fail=1; fi
# reprise : un relais coupe la première réponse de fichier après 5 000 octets (le modem émet NO CARRIER) → le client
# l'écarte, se repositionne (LSEEK) et reprend par Range: bytes=5000- ; zorg.tap (20 Ko) doit être identique
kill $PD 2>/dev/null; sleep 0.3
sed 's/^port: 18994/port: 18993/' "$OUT/prophet.yml" > "$OUT/prophet93.yml"
PROPHET_PASSWORD=sesame-test "$PROPHETD" -config "$OUT/prophet93.yml" >>"$OUT/prophetd.log" 2>&1 & PD=$!
python3 tests/cut_proxy.py 18994 18993 5000 >"$OUT/cut_proxy.log" 2>&1 & CP=$!
for i in $(seq 1 30); do curl -s -o /dev/null http://127.0.0.1:18994/cat && break; sleep 0.1; done
scenario resume "12000000:\n\p5g" 110000000
if ! want resume; then :; elif cmp -s "$OUT/flash_resume/zorg.tap" tests/repo/oric-games/zorg/zorg.tap && grep -q "termine : 1 fichier" "$OUT/resume.txt"; then echo "PASS resume (coupure a 5 000 octets, NO CARRIER ecarte, reprise Range + LSEEK, fichier identique)"
else echo "FAIL resume"; tail -1 "$OUT/resume.txt"; ls -l "$OUT/flash_resume"; fail=1; fi
kill $CP 2>/dev/null; sleep 0.3
# empreinte : le relais inverse l'octet 5 000 de la première réponse → CRC-32 différent, détecté
python3 tests/cut_proxy.py 18994 18993 5000 flip >"$OUT/flip_proxy.log" 2>&1 & CP=$!
for i in $(seq 1 30); do curl -s -o /dev/null http://127.0.0.1:18994/cat && break; sleep 0.1; done
scenario crcbad "12000000:\n\p5g" 70000000
if ! want crcbad; then :; elif grep -q "EMPREINTE DIFFERENTE" "$OUT/crcbad.txt" && [ ! -e "$OUT/flash_crcbad/.prophet/zorg.crc" ]; then echo "PASS crcbad (octet altere detecte par CRC-32, pas de marqueur)"
else echo "FAIL crcbad"; tail -3 "$OUT/crcbad.txt"; fail=1; fi
kill $CP 2>/dev/null; kill $PD 2>/dev/null; sleep 0.3
PROPHET_PASSWORD=sesame-test "$PROPHETD" -config "$OUT/prophet.yml" >>"$OUT/prophetd.log" 2>&1 & PD=$!
for i in $(seq 1 30); do curl -s -o /dev/null http://127.0.0.1:18994/cat && break; sleep 0.1; done
# débit réaliste : 1200 bauds, anneau de 32 octets comme le firmware LOCI devant le 6551 → fiche + téléchargement intacts
EXTRA="--serial-buffer 32" scenario baud "12000000:\n\p9\p9g" 120000000   # anneau de 32 octets (firmware LOCI) à 9600 bauds
if ! want baud; then :; elif cmp -s "$OUT/flash_baud/zorg.tap" tests/repo/oric-games/zorg/zorg.tap && grep -q "termine : 1 fichier" "$OUT/baud.txt"; then echo "PASS baud_ring32 (20 Ko intacts a 9600 bauds avec un anneau de 32 octets)"
else echo "FAIL baud_ring32"; tail -1 "$OUT/baud.txt"; fail=1; fi
# sans LOCI ni modem (Oric nu, émulateur sans --loci) : écran d'explication au lieu d'un arrêt muet
if want noacia; then
    "$EMU" -r "$ROM" -t "$TAP" -f --headless --realtime --cycles 12100000 --screenshot-text-at "12000000:$OUT/noacia.txt" >"$OUT/noacia.log" 2>&1
    if [ "$mode" = ref ]; then cp "$OUT/noacia.txt" "$REF/noacia.txt"; echo "REF  noacia"; cat "$OUT/noacia.txt"
    elif cmp -s "$OUT/noacia.txt" "$REF/noacia.txt"; then echo "PASS noacia (sans interface serie : materiel requis explique)"
    else echo "FAIL noacia"; diff "$REF/noacia.txt" "$OUT/noacia.txt" | head -20; fail=1; fi
fi
# ACIA en $0380 mais sans l'API MIA du LOCI : avertissement, puis le catalogue quand même
if want noloci; then
    "$EMU" -r "$ROM" -t "$TAP" -f --serial picowifi:Test --acia-addr 0380 --headless --realtime --cycles 12100000 --screenshot-text-at "12000000:$OUT/noloci.txt" >"$OUT/noloci.log" 2>&1
    if [ "$mode" = ref ]; then cp "$OUT/noloci.txt" "$REF/noloci.txt"; echo "REF  noloci"; cat "$OUT/noloci.txt"
    elif cmp -s "$OUT/noloci.txt" "$REF/noloci.txt"; then echo "PASS noloci (ACIA sans LOCI : stockage impossible explique)"
    else echo "FAIL noloci"; diff "$REF/noloci.txt" "$OUT/noloci.txt" | head -20; fail=1; fi
fi
echo "----"; [ $fail -eq 0 ] && echo "OK" || echo "ECHEC"
exit $fail
