/* lang.c — voir lang.h */
#include <string.h>
#include "lang.h"
#include "version.h"
#ifndef TEST_HOST
#include "loci.h"
#include "download.h"
#endif

#define S(id, max, fr, en, es) fr,
static const char *const fr_tab[S_COUNT] = {
#include "strings.def"
};
#undef S

char lang_code[3] = "fr";
static const char *lng[S_COUNT];
static unsigned char lng_ok;

const char *T(unsigned char id) { return lng_ok ? lng[id] : fr_tab[id]; }

#ifdef TEST_HOST
unsigned char lang_set(const char *code) { lng_ok = 0; strcpy(lang_code, "fr"); return code[0] == 'f'; }
#else
#define LNG_BUF ((char *)0xA000)
#define LNG_MAX 0x1400                      /* $A000-$B3FF : sous les jeux de caractères $B400 */

unsigned char lang_set(const char *code)
{
    char name[8], path[44]; int fd, r; unsigned int n = 0, k; unsigned char i = 0;
    lng_ok = 0; strcpy(lang_code, "fr");
    if (code[0] == 'f' || !code[0] || !code[1]) return code[0] == 'f';
    name[0] = (char)(code[0] - 32); name[1] = (char)(code[1] - 32); strcpy(name + 2, ".LNG");
    if (!loci_present()) return 0;
    fd = loci_open(name, LOCI_O_RDONLY);                       /* à côté de PROPHET.CFG, sinon dans le */
    if (fd < 0 && dl_dir[0]) {                                  /* dossier de téléchargement (paquet Prophet) */
        strcpy(path, dl_dir); if (path[strlen(path) - 1] != ':') strcat(path, "/"); strcat(path, name);
        fd = loci_open(path, LOCI_O_RDONLY);
    }
    if (fd < 0) return 0;
    while (n < LNG_MAX && (r = loci_read((unsigned char)fd, (unsigned char *)LNG_BUF + n, (unsigned char)(LNG_MAX - n > 255 ? 255 : LNG_MAX - n))) > 0) n += (unsigned int)r;
    loci_close((unsigned char)fd);
    if (n >= LNG_MAX || n < 6 || LNG_BUF[n - 1] || strcmp(LNG_BUF, "PLNG " VERSION)) return 0;
    for (k = (unsigned int)strlen(LNG_BUF) + 1; k < n && i < S_COUNT; ++i) { lng[i] = LNG_BUF + k; k += (unsigned int)strlen(LNG_BUF + k) + 1; }
    if (i != S_COUNT || k != n) return 0;
    lng_ok = 1; lang_code[0] = code[0]; lang_code[1] = code[1]; lang_code[2] = 0;
    return 1;
}
#endif
