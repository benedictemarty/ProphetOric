/*
 * main.c — ProphetOric, client texte du dépôt Prophet pour Oric + LOCI.
 * Sprint 1 : catégories → liste paginée → fiche. Écran TEXT 40×28 (conio cc65).
 * Touches : j/k (ou flèches) choisir, entrée = ouvrir, b = retour,
 * n/p = page suivante/précédente, q = quitter (retour BASIC).
 */
#include <conio.h>
#include <string.h>
#include "http.h"
#include "cli.h"
#include "serial.h"

#define VERSION "0.1.0"
#define IPP 16                       /* programmes par page (16 lignes de liste) */
#define ROWS 28
#define COLS 40

static char body[3072];              /* réponse courante (parsée en place) */
static struct cli_cat cats[8];
static unsigned char ncats;
static struct cli_listing listing;
static struct cli_info info;
static char cat_name[24];
static unsigned int page;

static void line(unsigned char y, const char *s)
{
    unsigned char n = (unsigned char)strlen(s);
    gotoxy(0, y);
    if (n > COLS) n = COLS;
    while (n--) cputc(*s++);
    cclear(COLS - wherex());
}

static void title(const char *t)
{
    clrscr();
    line(0, "ProphetOric " VERSION);
    gotoxy(COLS - strlen(t) - 1 > 14 ? COLS - strlen(t) - 1 : 14, 0);
    cputs(t);
}

static void status(const char *s) { line(ROWS - 1, s); }

/* affiche un texte avec retour à la ligne aux mots, depuis la ligne y ; renvoie la ligne suivante */
static unsigned char wrap(unsigned char y, const char *s)
{
    char buf[COLS + 1];
    while (*s && y < ROWS - 1) {
        unsigned char n = 0, cut = 0;
        while (s[n] && s[n] != '\n' && n < COLS) { if (s[n] == ' ') cut = n; ++n; }
        if (n == COLS && s[n] && s[n] != ' ' && cut) n = cut;
        memcpy(buf, s, n); buf[n] = 0;
        line(y++, buf);
        s += n;
        while (*s == ' ' || *s == '\n') ++s;
    }
    return y;
}

static unsigned char key(void)
{
    unsigned char c = cgetc();
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c == CH_CURS_DOWN) c = 'j';
    if (c == CH_CURS_UP) c = 'k';
    if (c == CH_CURS_LEFT) c = 'b';
    if (c == CH_CURS_RIGHT) c = 'n';
    if (c == CH_ENTER || c == '\r') c = '\n';
    return c;
}

/* GET → body ; 0 et message d'erreur à l'écran si échec */
static unsigned char fetch(const char *path)
{
    unsigned int len;
    status("connexion...");
    if (!http_get(path, 0, body, sizeof body, &len)) { status(http_error ? http_error : "erreur reseau"); return 0; }
    if (http_status != 200) {
        char m[24]; unsigned int v = http_status; unsigned char k;
        strcpy(m, http_status == 404 ? "introuvable " : http_status == 429 ? "trop d'essais " : "erreur serveur ");
        k = (unsigned char)strlen(m); m[k++] = '0' + v / 100; m[k++] = '0' + (v / 10) % 10; m[k++] = '0' + v % 10; m[k] = 0;
        status(m); return 0;
    }
    return 1;
}

static void draw_cats(unsigned char sel)
{
    unsigned char i;
    char l[COLS + 1];
    title("categories");
    for (i = 0; i < ncats; ++i) {
        unsigned char k = 0;
        l[k++] = i == sel ? '>' : ' '; l[k++] = ' ';
        strcpy(l + k, cats[i].name); k = (unsigned char)strlen(l);
        l[k++] = ' '; l[k++] = '(';
        { unsigned int v = cats[i].count; char d[6]; unsigned char m = 0;
          do { d[m++] = '0' + v % 10; v /= 10; } while (v);
          while (m) l[k++] = d[--m]; }
        l[k++] = ')'; l[k] = 0;
        line(2 + i, l);
    }
    status("j/k choisir  entree ouvrir  q quitter");
}

static void draw_list(unsigned char sel)
{
    unsigned char i;
    char l[COLS + 1];
    title(cat_name);
    for (i = 0; i < listing.count; ++i) {
        l[0] = i == sel ? '>' : ' '; l[1] = ' ';
        strncpy(l + 2, listing.item[i].title, COLS - 2); l[COLS] = 0;
        line(2 + i, l);
    }
    { char s[COLS + 1]; unsigned int v; char d[6]; unsigned char m, k = 0;
      strcpy(s, "page "); k = 5; v = page + 1; m = 0;
      do { d[m++] = '0' + v % 10; v /= 10; } while (v); while (m) s[k++] = d[--m];
      s[k++] = '/'; v = listing.pages ? listing.pages : 1; m = 0;
      do { d[m++] = '0' + v % 10; v /= 10; } while (v); while (m) s[k++] = d[--m];
      strcpy(s + k, " j/k n/p entree=fiche b=retour"); status(s); }
}

static unsigned char load_list(void)
{
    char path[80];
    char d[6]; unsigned char m = 0, k; unsigned int v = page;
    strcpy(path, "/list/"); strcat(path, cat_name); strcat(path, "?platform=oric&ipp=16&page=");
    k = (unsigned char)strlen(path);
    do { d[m++] = '0' + v % 10; v /= 10; } while (v); while (m) path[k++] = d[--m]; path[k] = 0;
    if (!fetch(path)) return 0;
    if (!cli_parse_listing(body, &listing)) { status("reponse inattendue"); return 0; }
    return 1;
}

static void info_screen(const char *id)
{
    char path[80];
    unsigned char y;
    strcpy(path, "/app/"); strcat(path, id);
    if (!fetch(path)) { cgetc(); return; }
    if (!cli_parse_info(body, &info)) { status("reponse inattendue"); cgetc(); return; }
    title("fiche");
    y = wrap(2, info.title);
    if (info.author) { line(y, "par :"); y = wrap(y + 1, info.author); }
    ++y;
    if (info.description) y = wrap(y, info.description);
    { char s[COLS + 1]; strcpy(s, "fichiers : "); s[11] = '0' + (info.files % 10); s[12] = 0; if (y < ROWS - 2) line(y + 1, s); }
    status("b retour");
    while (key() != 'b') ;
}

static void list_screen(void)
{
    unsigned char sel = 0, c;
    page = 0;
    if (!load_list()) { cgetc(); return; }
    for (;;) {
        draw_list(sel);
        c = key();
        if (c == 'b' || c == 'q') return;
        if (c == 'j' && sel + 1 < listing.count) ++sel;
        if (c == 'k' && sel) --sel;
        if (c == 'n' && page + 1 < listing.pages) { ++page; sel = 0; if (!load_list()) { cgetc(); return; } }
        if (c == 'p' && page) { --page; sel = 0; if (!load_list()) { cgetc(); return; } }
        if (c == '\n' && listing.count) {
            char id[40];
            strncpy(id, listing.item[sel].id, 39); id[39] = 0;
            info_screen(id);
            if (!load_list()) { cgetc(); return; }   /* body réutilisé : recharger la liste */
        }
    }
}

int main(void)
{
    unsigned char sel = 0, c;
    title("");
    if (!serial_probe(ACIA_BASE_LOCI)) { status("pas d'ACIA 6551 en $0380 (LOCI ?)"); cgetc(); return 1; }
    serial_init(ACIA_BASE_LOCI);
    if (!fetch("/cat?platform=oric") || !cli_parse_cat(body, cats, 8, &ncats)) { cgetc(); return 1; }
    for (;;) {
        draw_cats(sel);
        c = key();
        if (c == 'q') { clrscr(); return 0; }
        if (c == 'j' && sel + 1 < ncats) ++sel;
        if (c == 'k' && sel) --sel;
        if (c == '\n' && ncats) {
            strncpy(cat_name, cats[sel].name, 23); cat_name[23] = 0;
            list_screen();
            if (!fetch("/cat?platform=oric") || !cli_parse_cat(body, cats, 8, &ncats)) { cgetc(); return 1; }
        }
    }
}
