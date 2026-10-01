# ProphetOric — cc65 (-t atmos), chaîne reprise d'OricTel.
CC65    = cc65
CA65    = ca65
LD65    = ld65
TARGET  = atmos
CFG     = cfg/prophetoric.cfg
BLD     = build
OUT     = $(BLD)/prophet.tap
TESTOUT = $(BLD)/prophet-test.tap        # hôte/port de test (prophetd local)

HOST ?= prophet.3617.fr
PORT ?= 8998
TEST_HOST_ADDR ?= 127.0.0.1
TEST_PORT ?= 18994

CSRC = src/main.c src/http.c src/cli.c src/serial.c src/serial_tx.c src/at_modem.c src/loci.c src/download.c src/config.c src/crc32.c src/lang.c
ASRC = src/serial_asm.s src/loci_asm.s src/crc32_asm.s src/tapehdr.s
OBJ  = $(patsubst src/%.c,$(BLD)/%.o,$(CSRC)) $(patsubst src/%.s,$(BLD)/%.o,$(ASRC))
TOBJ = $(patsubst src/%.c,$(BLD)/t_%.o,$(CSRC)) $(patsubst src/%.s,$(BLD)/%.o,$(ASRC))

CFLAGS  = -t $(TARGET) -O --add-source -Isrc
DEFS    = -DPROPHET_HOST='"$(HOST)"' -DPROPHET_PORT='"$(PORT)"'
TDEFS   = -DPROPHET_HOST='"$(TEST_HOST_ADDR)"' -DPROPHET_PORT='"$(TEST_PORT)"'

EMU     ?= $(HOME)/Oric1/oric1-emu
EMU_ROM ?= $(HOME)/Oric1/roms/basic11b.rom

.PHONY: all test test-host test-emu run clean spike-sedoric
LNG = $(BLD)/EN.LNG $(BLD)/ES.LNG   # langues (src/strings.def), à copier sur le LOCI à côté de PROPHET.CFG
all: $(OUT) $(LNG)

$(BLD):
	mkdir -p $(BLD)

$(BLD)/%.s: src/%.c | $(BLD)
	$(CC65) $(CFLAGS) $(DEFS) -o $@ $<
$(BLD)/t_%.s: src/%.c | $(BLD)
	$(CC65) $(CFLAGS) $(TDEFS) -o $@ $<
$(BLD)/%.o: $(BLD)/%.s
	$(CA65) -t $(TARGET) -o $@ $<
$(BLD)/%.o: src/%.s | $(BLD)
	$(CA65) -t $(TARGET) -o $@ $<

# en-têtes et textes : tout recompiler s'ils changent — aussi les .s (build/t_*.s pouvaient rester
# d'une construction précédente et être réutilisés périmés : User-Agent 0.9.0 dans le .tap de test 0.9.1)
$(OBJ) $(TOBJ) $(patsubst src/%.c,$(BLD)/%.s,$(CSRC)) $(patsubst src/%.c,$(BLD)/t_%.s,$(CSRC)): $(wildcard src/*.h) src/strings.def

$(BLD)/mklng: tools/mklng.c src/strings.def src/version.h | $(BLD)
	gcc -Wall -Wextra -o $@ tools/mklng.c
$(BLD)/EN.LNG: $(BLD)/mklng
	$(BLD)/mklng en > $@
$(BLD)/ES.LNG: $(BLD)/mklng
	$(BLD)/mklng es > $@

$(OUT): $(OBJ) $(CFG)
	$(LD65) -C $(CFG) -o $@ $(OBJ) -m $(BLD)/prophet.map $(TARGET).lib
$(TESTOUT): $(TOBJ) $(CFG)
	$(LD65) -C $(CFG) -o $@ $(TOBJ) -m $(BLD)/prophet-test.map $(TARGET).lib

# flash du LOCI émulé (0:) : PROPHET.CFG, fichiers téléchargés, microdis.rom pour le boot disquette
FLASH ?= flash
run: $(OUT) $(LNG)      ## Phosphoric (fenêtre SDL) + LOCI + modem PicoWiFi émulé (vraies sockets) vers prophet.3617.fr
	mkdir -p $(FLASH); [ -f $(FLASH)/microdis.rom ] || cp $(HOME)/Oric1/roms/microdis.rom $(FLASH)/
	cp $(LNG) $(FLASH)/
	$(EMU) -r $(EMU_ROM) -t $(OUT) -f --loci --loci-usb none --loci-flash $(FLASH) --serial picowifi:ProphetOric --serial-buffer 32

test: test-host test-emu

test-host: $(LNG) | $(BLD)  ## parseurs cli, HTTP, CRC-32 et textes (3 langues) sur l'hôte (gcc)
	gcc -Wall -Wextra -DTEST_HOST -Isrc -o $(BLD)/test_cli tests/host/test_cli.c src/cli.c && $(BLD)/test_cli
	gcc -Wall -Wextra -DTEST_HOST -Isrc -o $(BLD)/test_crc32 tests/host/test_crc32.c src/crc32.c && $(BLD)/test_crc32
	gcc -Wall -Wextra -DTEST_HOST -DPROPHET_HOST='"h"' -DPROPHET_PORT='"1"' -Isrc -o $(BLD)/test_http tests/host/test_http.c src/http.c src/at_modem.c tests/host/fake_serial.c src/lang.c && $(BLD)/test_http
	gcc -Wall -Wextra -DTEST_HOST -Isrc -o $(BLD)/test_lang tests/host/test_lang.c src/lang.c && $(BLD)/test_lang $(LNG)

test-emu: $(TESTOUT) $(LNG)  ## Phosphoric headless → prophetd local (tests/run.sh)
	tests/run.sh $(TESTOUT)

spike-sedoric:  ## spike S1 : SAVE Sedoric depuis cc65 + ProphetOric sous Sedoric sans LOCI (spikes/sedoric)
	spikes/sedoric/run.sh

clean:
	rm -rf $(BLD)
