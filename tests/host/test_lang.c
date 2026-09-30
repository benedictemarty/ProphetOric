/* test_lang.c — tests hôte des textes (src/strings.def) : chaque texte existe dans les 3
 * langues, tient dans sa longueur maximale, n'utilise que l'ASCII imprimable (jeu de l'Oric)
 * et le français compilé (lang.c) correspond à la colonne fr ; fichier .LNG généré relu. */
#include <stdio.h>
#include <string.h>
#include "../../src/lang.h"
#include "../../src/version.h"
#include "../../src/himem.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct s { const char *name; unsigned int max; const char *t[3]; };
#define S(id, max, fr, en, es) { #id, max, { fr, en, es } },
static const struct s tab[] = {
#include "../../src/strings.def"
};
#undef S

static int ascii_ok(const char *t)
{
    for (; *t; ++t) if ((*t < 32 || *t > 126) && *t != '\n') return 0;
    return 1;
}

int main(int argc, char **argv)
{
    unsigned int i, l;
    CHECK(sizeof tab / sizeof tab[0] == S_COUNT && S_COUNT < 256);
    for (i = 0; i < S_COUNT; ++i) for (l = 0; l < 3; ++l) {
        const char *t = tab[i].t[l];
        if (!t[0] || strlen(t) > tab[i].max || !ascii_ok(t)) { printf("FAIL %s [%s] : \"%s\" (%u > %u ?)\n", tab[i].name, l == 0 ? "fr" : l == 1 ? "en" : "es", t, (unsigned)strlen(t), tab[i].max); fails++; }
    }
    CHECK(!strcmp(T(S_CONNECTING), "connexion...") && !strcmp(T(S_E_WRITE), "ecriture impossible"));
    for (l = 1; l < (unsigned int)argc; ++l) {                          /* fichiers .LNG générés : en-tête, compte, ordre */
        static char buf[8192]; FILE *f = fopen(argv[l], "rb"); size_t n; unsigned int k, c = 0;
        CHECK(f != 0); if (!f) continue;
        n = fread(buf, 1, sizeof buf, f); fclose(f);
        CHECK(n < LNG_MAX && n > 0 && buf[n - 1] == 0 && !strcmp(buf, "PLNG " VERSION));
        for (k = (unsigned int)strlen(buf) + 1; k < n; k += (unsigned int)strlen(buf + k) + 1) {
            CHECK(!strcmp(buf + k, tab[c].t[strstr(argv[l], "ES") ? 2 : 1])); ++c;
        }
        CHECK(c == S_COUNT);
        printf("%s : %u textes, %u octets\n", argv[l], c, (unsigned)n);
    }
    printf("%s : %d échec(s)\n", __FILE__, fails);
    return fails != 0;
}
