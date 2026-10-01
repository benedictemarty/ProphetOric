#!/usr/bin/env bash
# run.sh — spike S1 : SPIKE (cc65) lancé sous SEDORIC V4.0 écrit SPIKE.BIN par SAVE ($0467).
# 1) disque SEDO40u « nu » + spike.tap injecté (AUTO) et lancé par l'INIST ;
# 2) Phosphoric headless --disk-writeback ; 3) catalogue (sedoric-info) ;
# 4) relecture par SEDORIC lui-même : INIST LOAD"SPIKE.BIN", dump RAM, comparaison.
set -u
cd "$(dirname "$0")"
O=${OUT:-out}; mkdir -p "$O"; E=$HOME/Oric1
# copie figée de l'émulateur : une autre session peut reconstruire ~/Oric1/oric1-emu pendant l'essai
EMU=${EMU:-$O/oric1-emu}; [ -x "$EMU" ] || cp $E/oric1-emu "$EMU" || { echo "FAIL : oric1-emu absent"; exit 1; }
ROM="-r $E/roms/basic11b.rom --disk-rom $E/roms/microdis.rom"
python3 $E/tools/sedoric_mkbare.py $E/disks/SEDO40u.DSK "$O/base.dsk" >/dev/null
$E/tap2sedoric spike.tap -o "$O/disk.dsk" -b "$O/base.dsk" -n SPIKE.COM -e 050D -i 'LOAD"SPIKE.COM"' >"$O/inject.log" 2>&1 || { echo "FAIL inject"; cat "$O/inject.log"; exit 1; }
$EMU $ROM -d "$O/disk.dsk" --disk-writeback --headless --cycles 60100000 --screenshot-text-at "60000000:$O/run.txt" >"$O/run.log" 2>&1
grep -v "^ *$" "$O/run.txt" | tail -4
$E/sedoric-info "$O/disk.dsk" > "$O/info.txt" 2>&1; grep -i spike "$O/info.txt"
ADDR=$(grep -o 'A#[0-9A-F]*' "$O/run.txt" | head -1 | cut -c3-)
[ -n "$ADDR" ] || { echo "FAIL : adresse du tampon non affichee"; exit 1; }
# relecture : disque nu + SPIKE.BIN copié ? non : on relance sur le disque écrit, INIST remplacée
python3 $E/tools/sedoric_mkbare.py "$O/disk.dsk" "$O/check.dsk" 'LOAD"SPIKE.BIN"' >/dev/null
$EMU $ROM -d "$O/check.dsk" --headless --cycles 50100000 --dump-ram-at "50000000:$O/check.bin" >"$O/check.log" 2>&1
python3 - "$O/check.bin" "$ADDR" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read(); a = int(sys.argv[2], 16)
want = bytes((i ^ 0x5A) for i in range(256))
print("PASS sedoric_save (SPIKE.BIN relu par SEDORIC : 256 octets identiques en $%04X)" % a if d[a:a+256] == want
      else "FAIL sedoric_save (relu en $%04X : %s...)" % (a, d[a:a+8].hex()))
sys.exit(0 if d[a:a+256] == want else 1)
PY

# --- S1b : ProphetOric entier sous SEDORIC, sans LOCI : ACIA en $031C (à côté du Microdisc),
# modem picowifi émulé (9600 bauds réels, sans tampon : fidèle au 6551) → prophetd local (port
# 18995, tests/repo) ; attendu : écran « LOCI absent », puis, après une touche, le catalogue —
# en 0.9.1 : « pas de modem (ATZ) », bloquant connu (voir docs/SPIKE-SEDORIC.md). Lancement : LOAD par l'INIST puis CALL#50D tapé au
# prompt — ni le lancement AUTO du LOAD ni « :CALL#50D » dans l'INIST ne démarrent ce fichier de
# 31 Ko (alors que l'AUTO marche pour SPIKE.COM) : cause NON CHERCHÉE (point ouvert).
P=../..
SRC="$P/src/main.c $P/src/http.c $P/src/cli.c $P/src/serial.c $P/src/serial_tx.c $P/src/at_modem.c $P/src/loci.c $P/src/download.c $P/src/config.c $P/src/crc32.c $P/src/lang.c $P/src/serial_asm.s $P/src/loci_asm.s $P/src/crc32_asm.s $P/src/tapehdr.s"
cl65 -t atmos -O -I$P/src -C $P/cfg/prophetoric.cfg -DPROPHET_HOST='"127.0.0.1"' -DPROPHET_PORT='"18995"' -DPROPHET_ACIA=0x031C \
     -o "$O/prophet031c.tap" $SRC -m "$O/prophet031c.map" >"$O/build031c.log" 2>&1 || { echo "FAIL build 031C"; cat "$O/build031c.log"; exit 1; }
printf 'datafolder: %s/tests/repo\nport: 18995\nbind: 127.0.0.1\nmax_ipp: 50\n' "$(cd $P && pwd)" > "$O/prophet.yml"
for p in $(pgrep -f "prophetd -config $O/prophet.yml"); do kill $p; done
$HOME/Neo6502Prophet/bin/prophetd -config "$O/prophet.yml" >"$O/prophetd.log" 2>&1 & PD=$!
trap 'kill $PD 2>/dev/null' EXIT
for i in $(seq 1 30); do curl -s -o /dev/null http://127.0.0.1:18995/cat && break; sleep 0.1; done
$E/tap2sedoric "$O/prophet031c.tap" -o "$O/disk2.dsk" -b "$O/base.dsk" -n PROPHET.COM -e 050D -i 'LOAD"PROPHET.COM"' >"$O/inject2.log" 2>&1 || { echo "FAIL inject 2"; cat "$O/inject2.log"; exit 1; }
$EMU $ROM -d "$O/disk2.dsk" --serial picowifi:Spike --serial-baud 9600 --serial-trace "$O/s1b_trace.txt" --headless --realtime --cycles 85100000 \
     --type-keys "40000000:CALL#50D\n" --type-keys "60000000: " --screenshot-text-at "56000000:$O/s1b_noloci.txt" --screenshot-text-at "85000000:$O/s1b_cat.txt" >"$O/s1b.log" 2>&1
echo "--- 56 M cycles"; grep -v "^ *$" "$O/s1b_noloci.txt" | head -3
echo "--- 85 M cycles"; grep -v "^ *$" "$O/s1b_cat.txt" | head -6; tail -1 "$O/s1b_cat.txt"
if grep -q "LOCI absent" "$O/s1b_noloci.txt"; then echo "PASS sedoric_prophet_start (ProphetOric demarre sous SEDORIC, ACIA \$031C detecte sans LOCI)"
else echo "FAIL sedoric_prophet_start"; exit 1; fi
# Bloquant connu (resultat du spike, pas un echec du test) : sans l'anneau de 32 octets du LOCI,
# l'echo de "ATZ" par le modem ecrase l'unique octet de reception du 6551 pendant l'emission.
if tail -1 "$O/s1b_cat.txt" | grep -q "pas de modem (ATZ)" && grep -q "OVERRUN" "$O/s1b_trace.txt"; then
    echo "CONNU sedoric_modem (echo AT : OVERRUN du 6551 sans tampon -> S2 : lire en emettant)"
elif grep -q "Zorg" "$O/s1b_cat.txt"; then echo "PASS sedoric_catalogue (catalogue recu sans LOCI)"
else echo "FAIL sedoric_modem (ni catalogue ni bloquant connu)"; tail -1 "$O/s1b_cat.txt"; exit 1; fi
