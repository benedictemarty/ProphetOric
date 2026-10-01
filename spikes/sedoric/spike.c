/* spike.c — S1 « stockage Sedoric sans LOCI » : un programme cc65 lancé sous SEDORIC
 * V4.0 écrit 256 octets connus dans SPIKE.BIN sur la disquette, par la commande
 * SAVE de SEDORIC appelée via le vecteur « ! » ($0467, sed.s). Contrôle hors Oric :
 * spikes/sedoric/run.sh relit le fichier par SEDORIC lui-même. */
#include <conio.h>
#include <string.h>

unsigned char __fastcall__ sed_present(void);
void __fastcall__ sed_cmd(const char *line);

static unsigned char data[256];
static char line[48];

static void hex4(char *d, unsigned int v)
{
    static const char hx[] = "0123456789ABCDEF";
    d[0] = hx[v >> 12]; d[1] = hx[(v >> 8) & 15]; d[2] = hx[(v >> 4) & 15]; d[3] = hx[v & 15]; d[4] = 0;
}

int main(void)
{
    unsigned int i;
    for (i = 0; i < 256; ++i) data[i] = (unsigned char)(i ^ 0x5A);
    if (!sed_present()) { cputs("SPIKE: pas de SEDORIC V4.0\r\n"); return 1; }
    strcpy(line, "SAVE\"SPIKE.BIN\",A#"); hex4(line + strlen(line), (unsigned int)data);
    strcat(line, ",E#"); hex4(line + strlen(line), (unsigned int)data + 255);
    cputs(line); cputs("\r\n");
    sed_cmd(line);
    cputs("SPIKE: OK (retour de SEDORIC)\r\n");
    return 0;
}
