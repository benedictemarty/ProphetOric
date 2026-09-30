/* test_crc32.c — tests hôte (gcc) des tables CRC-32 (crc32.c) : valeurs de référence
 * zlib, et fichier de test zorg.tap = 7eba6c8f (réponse de /crc32/zorg du prophetd). */
#include <stdio.h>
#include <string.h>
#include "../../src/crc32.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const char *crc_of(const void *p, unsigned int n)
{
    static char h[9];
    crc32_start(); crc32_buf(p, n); crc32_hex(h);
    return h;
}

int main(void)
{
    static unsigned char big[70000];
    FILE *f; unsigned int n;
    CHECK(!strcmp(crc_of("", 0), "00000000"));
    CHECK(!strcmp(crc_of("123456789", 9), "cbf43926"));                  /* valeur de contrôle CRC-32 */
    CHECK(!strcmp(crc_of("The quick brown fox jumps over the lazy dog", 43), "414fa339"));
    CHECK(crc32_t0[1] == 0x96 && crc32_t1[1] == 0x30 && crc32_t2[1] == 0x07 && crc32_t3[1] == 0x77);   /* T[1] = 77073096 */
    f = fopen("tests/repo/oric-games/zorg/zorg.tap", "rb");
    CHECK(f != 0);
    if (f) { n = (unsigned int)fread(big, 1, sizeof big, f); fclose(f); CHECK(!strcmp(crc_of(big, n), "7eba6c8f")); }
    {   /* par morceaux de 255 (comme loci_read_crc) = d'un coup */
        unsigned int i; char a[9];
        for (i = 0; i < sizeof big; ++i) big[i] = (unsigned char)(i * 7 + (i >> 8));
        strcpy(a, crc_of(big, sizeof big));
        crc32_start();
        for (i = 0; i < sizeof big; i += 255) crc32_buf(big + i, sizeof big - i < 255 ? sizeof big - i : 255);
        { char b[9]; crc32_hex(b); CHECK(!strcmp(a, b)); }
    }
    printf("%s : %d échec(s)\n", __FILE__, fails);
    return fails != 0;
}
