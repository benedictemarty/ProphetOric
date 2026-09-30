/* mklng.c — génère un fichier de langue ProphetOric (voir src/lang.h) depuis src/strings.def.
 * Usage : mklng en|es > EN.LNG */
#include <stdio.h>
#include <string.h>
#include "../src/version.h"

struct s { const char *fr, *en, *es; };
#define S(id, max, fr, en, es) { fr, en, es },
static const struct s tab[] = {
#include "../src/strings.def"
};

int main(int argc, char **argv)
{
    unsigned int i; int en;
    if (argc != 2 || (strcmp(argv[1], "en") && strcmp(argv[1], "es"))) { fprintf(stderr, "usage : mklng en|es\n"); return 2; }
    en = !strcmp(argv[1], "en");
    fputs("PLNG " VERSION, stdout); fputc(0, stdout);
    for (i = 0; i < sizeof tab / sizeof tab[0]; ++i) { fputs(en ? tab[i].en : tab[i].es, stdout); fputc(0, stdout); }
    return 0;
}
