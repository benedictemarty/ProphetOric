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

CSRC = src/main.c src/http.c src/cli.c src/serial.c src/serial_tx.c src/at_modem.c
ASRC = src/serial_asm.s src/tapehdr.s
OBJ  = $(patsubst src/%.c,$(BLD)/%.o,$(CSRC)) $(patsubst src/%.s,$(BLD)/%.o,$(ASRC))
TOBJ = $(patsubst src/%.c,$(BLD)/t_%.o,$(CSRC)) $(patsubst src/%.s,$(BLD)/%.o,$(ASRC))

CFLAGS  = -t $(TARGET) -O --add-source -Isrc
DEFS    = -DPROPHET_HOST='"$(HOST)"' -DPROPHET_PORT='"$(PORT)"'
TDEFS   = -DPROPHET_HOST='"$(TEST_HOST_ADDR)"' -DPROPHET_PORT='"$(TEST_PORT)"'

EMU     ?= $(HOME)/Oric1/oric1-emu
EMU_ROM ?= $(HOME)/Oric1/roms/basic11b.rom

.PHONY: all test test-host test-emu run clean
all: $(OUT)

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

$(OUT): $(OBJ) $(CFG)
	$(LD65) -C $(CFG) -o $@ $(OBJ) -m $(BLD)/prophet.map $(TARGET).lib
$(TESTOUT): $(TOBJ) $(CFG)
	$(LD65) -C $(CFG) -o $@ $(TOBJ) -m $(BLD)/prophet-test.map $(TARGET).lib

run: $(OUT)      ## Phosphoric + LOCI + modem PicoWiFi émulé (vraies sockets) vers prophet.3617.fr
	$(EMU) -r $(EMU_ROM) -t $(OUT) -f --loci --serial picowifi:ProphetOric --serial-buffer 4096

test: test-host test-emu

test-host: | $(BLD)  ## parseurs cli et HTTP sur l'hôte (gcc)
	gcc -Wall -Wextra -DTEST_HOST -Isrc -o $(BLD)/test_cli tests/host/test_cli.c src/cli.c && $(BLD)/test_cli
	gcc -Wall -Wextra -DTEST_HOST -DPROPHET_HOST='"h"' -DPROPHET_PORT='"1"' -Isrc -o $(BLD)/test_http tests/host/test_http.c src/http.c src/at_modem.c tests/host/fake_serial.c && $(BLD)/test_http

test-emu: $(TESTOUT)  ## Phosphoric headless → prophetd local (tests/run.sh)
	tests/run.sh $(TESTOUT)

clean:
	rm -rf $(BLD)
