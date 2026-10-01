/*
 * main.c — ProphetOric, client texte du dépôt Prophet pour Oric + LOCI.
 * Sprint 1 : catégories → liste paginée → fiche. Écran TEXT 40×28 (conio cc65).
 * Touches : j/k (ou flèches) choisir, entrée = ouvrir, b = retour,
 * n/p = page suivante/précédente, g = télécharger (fiche), l = lancer (monte le
 * .tap sur le LOCI et rend la main au BASIC pour CLOAD""), c = configuration,
 * q = quitter (retour BASIC).
 */
#include <conio.h>
#include <string.h>
#include <stdlib.h>
#include "http.h"
#include "cli.h"
#include "serial.h"
#include "download.h"
#include "config.h"
#include "loci.h"
#include "version.h"
#include "lang.h"
#include "himem.h"

#define ROWS 28
#define COLS 40
#define SCR ((unsigned char *)0xBB80)  /* écran TEXT : attributs série écrits directement */
#define MAX_CATS 16
#define MAX_DEV 16
#ifndef PROPHET_ACIA
#define PROPHET_ACIA ACIA_BASE_LOCI       /* -DPROPHET_ACIA=0x031C : ACIA hors LOCI (spikes/sedoric) */
#endif

#define body BODY_BUF                 /* réponse courante (parsée en place), en $AA00 (himem.h) */
static struct cli_cat cats[MAX_CATS];
static unsigned char ncats;
static char cat_names[MAX_CATS][18];  /* copies : cats[].name pointe dans body, écrasé à chaque requête */
static struct cli_listing listing;
static struct cli_info info;
static char search_key[24];           /* "" = onglet, sinon /search/<clé> */
static unsigned char tab;             /* 0 = « tous », 1..ncats = catégorie */
static unsigned char view_list;       /* 0 = grille de cartes (8), 1 = liste compacte (16) */
static unsigned int page;
static unsigned int dev_hash[MAX_DEV]; /* ids de la catégorie en-developpement (empreinte 16 bits) */
static unsigned char ndev;

/* Écrit une ligne complète (tronquée/complétée à 40 colonnes). Jamais 40 cputc
 * suivis d'autre chose : conio (cc65 atmos) incrémente CURS_Y après la 40e colonne
 * et, sur la dernière ligne, CURS_Y = 28 déborde la table des adresses écran —
 * les 40 espaces suivants partaient dans le code ($07BB, plantage du 17/09). */
static void line(unsigned char y, const char *s)
{
    unsigned char n = (unsigned char)strlen(s), i;
    if (n > COLS) n = COLS;
    gotoxy(0, y);
    for (i = 0; i < n; ++i) cputc(s[i]);
    if (n < COLS) cclear(COLS - n);
    gotoxy(0, y);
}

static void title(const char *t)
{
    clrscr();
    line(0, "ProphetOric " VERSION);
    gotoxy(COLS - strlen(t) - 1 > 14 ? COLS - strlen(t) - 1 : 14, 0);
    cputs(t);
}

static void status(const char *s) { line(ROWS - 1, s); }

/* affiche un texte avec retour à la ligne aux mots, depuis la ligne y et avant la ligne
 * wrap_end ; renvoie la ligne suivante */
static unsigned char wrap_end = ROWS - 1;
static unsigned char wrap_cols = COLS;    /* < COLS : colonne de gauche (jaquette à droite) */
static void put_pad(unsigned char y, const char *t, unsigned char w);
static unsigned char wrap(unsigned char y, const char *s)
{
    char buf[COLS + 1];
    while (*s && y < wrap_end) {
        unsigned char n = 0, cut = 0;
        while (s[n] && s[n] != '\n' && n < wrap_cols) { if (s[n] == ' ') cut = n; ++n; }
        if (n == wrap_cols && s[n] && s[n] != ' ' && cut) n = cut;
        memcpy(buf, s, n); buf[n] = 0;
        if (wrap_cols < COLS) put_pad(y++, buf, wrap_cols); else line(y++, buf);
        s += n;
        while (*s == ' ' || *s == '\n') ++s;
    }
    return y;
}

static unsigned char arrows_move;    /* 1 : flèches gauche/droite = h/l (grille), sinon b/n */
static unsigned char key(void)
{
    unsigned char c = cgetc();
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c == CH_CURS_DOWN) c = 'j';
    if (c == CH_CURS_UP) c = 'k';
    if (c == CH_CURS_LEFT) c = arrows_move ? 'h' : 'b';
    if (c == CH_CURS_RIGHT) c = arrows_move ? 'l' : 'n';
    if (c == CH_ENTER || c == '\r') c = '\n';
    return c;
}

/* GET → body ; 0 et message d'erreur à l'écran si échec */
static unsigned char fetch(const char *path)
{
    unsigned int len;
    status(T(S_CONNECTING));
    if (!http_get(path, 0, body, BODY_SIZE, &len)) { status(http_error ? http_error : T(S_ERR_NET)); return 0; }
    if (http_status != 200) {
        char m[24]; unsigned int v = http_status; unsigned char k;
        strcpy(m, http_status == 404 ? T(S_E404) : http_status == 429 ? T(S_E429) : T(S_ESRV));
        k = (unsigned char)strlen(m); m[k++] = '0' + v / 100; m[k++] = '0' + (v / 10) % 10; m[k++] = '0' + v % 10; m[k] = 0;
        status(m); return 0;
    }
    return 1;
}

/* saisie d'une ligne à la position y : entrée = valider, echap = annuler (0) */
static unsigned char edit_field(unsigned char y, char *buf, unsigned char max)
{
    unsigned char n = (unsigned char)strlen(buf), c;
    for (;;) {
        { char l[COLS + 2]; strcpy(l, buf); strcat(l, "_"); line(y, l); }
        c = cgetc();
        if (c == CH_ENTER || c == '\r' || c == '\n') { buf[n] = 0; return 1; }
        if (c == CH_ESC) return 0;
        if (c == CH_DEL || c == 8 || c == 127) { if (n) buf[--n] = 0; continue; }
        if (n < max && c >= 32 && c < 127 && c != ' ') { buf[n++] = (char)c; buf[n] = 0; }
    }
}

/* choix à deux valeurs affiché sur la ligne y : espace/j/k bascule, entrée valide, échap annule */
static unsigned char choose2(unsigned char y, const char *a, const char *b, unsigned char *sel)
{
    unsigned char c;
    for (;;) {
        char l[COLS + 1];
        strcpy(l, *sel ? "  " : "> "); strcat(l, a); strcat(l, *sel ? "    > " : "      "); strcat(l, b);
        line(y, l);
        c = key();
        if (c == '\n') return 1;
        if (c == CH_ESC) return 0;
        if (c == ' ' || c == 'j' || c == 'k' || c == 'n' || c == 'b') *sel = !*sel;
    }
}

/* Explorateur de dossiers du LOCI : niveau « volumes » (0: flash, 1:-4: clés USB) s'il y en a
 * plusieurs, puis les sous-dossiers ; j/k choisir, entrée = entrer, b = dossier parent,
 * espace = choisir le dossier courant, échap = annuler. Résultat : "" (racine du flash),
 * "jeux", "1:" (racine du volume 1) ou "1:jeux/oric". */
#define DIR_MAX 20
static char dir_names[DIR_MAX][26];
static unsigned char dir_count;

static void dir_list(const char *path, unsigned char volumes)
{
    int fd; char name[64]; unsigned char is_dir, i, j;
    dir_count = 0;
    fd = loci_opendir(volumes ? "" : (path[0] ? path : "0:"));
    if (fd < 0) return;
    while (dir_count < DIR_MAX && loci_readdir((unsigned char)fd, name, &is_dir) == 0 && name[0]) {
        if (volumes) {                         /* « N: libellé » ; le modem CDC n'est pas un volume */
            if (name[1] != ':' || strstr(name, "CDC")) continue;
            strncpy(dir_names[dir_count], name, 25); dir_names[dir_count][25] = 0;
        } else {
            if (!is_dir || name[0] == '.') continue;          /* .prophet : marqueurs de téléchargement */
            strncpy(dir_names[dir_count], name, 25); dir_names[dir_count][25] = 0;
        }
        ++dir_count;
    }
    loci_closedir((unsigned char)fd);
    if (!volumes) {                            /* tri simple (insertion) */
        char tmp[26];
        for (i = 1; i < dir_count; ++i) for (j = i; j && strcmp(dir_names[j - 1], dir_names[j]) > 0; --j) {
            strcpy(tmp, dir_names[j]); strcpy(dir_names[j], dir_names[j - 1]); strcpy(dir_names[j - 1], tmp);
        }
    }
}

static unsigned char browse_dir(char *out, unsigned char max)
{
    char cur[40];                               /* "N:a/b" ou "a/b" (flash) */
    unsigned char sel = 0, at_volumes, nvol, c, i;
    dir_list("", 1); nvol = dir_count;
    at_volumes = nvol > 1;
    strcpy(cur, out);
    if (!at_volumes) { if (cur[1] == ':') memmove(cur, cur + 2, strlen(cur + 2) + 1); }   /* un seul volume : pas de préfixe */
    if (!at_volumes) dir_list(cur, 0);
    for (;;) {
        char l[COLS + 1];
        title(T(S_T_DIR));
        strcpy(l, at_volumes ? T(S_VOLUMES) : T(S_DIR_PFX)); if (!at_volumes) strncat(l, cur, COLS - 12);
        line(2, l);
        for (i = 0; i < dir_count && i < DIR_MAX; ++i) { l[0] = i == sel ? '>' : ' '; l[1] = ' '; strcpy(l + 2, dir_names[i]); line(4 + i, l); }
        if (!dir_count) line(4, at_volumes ? T(S_NO_VOL) : T(S_NO_SUBDIR));
        status(at_volumes ? T(S_K_VOL) : T(S_K_DIR));
        c = key();
        if (c == CH_ESC) return 0;
        if (c == 'j' && sel + 1 < dir_count) ++sel;
        if (c == 'k' && sel) --sel;
        if (at_volumes) {
            if (c == '\n' && dir_count) { cur[0] = dir_names[sel][0]; cur[1] = ':'; cur[2] = 0; at_volumes = 0; sel = 0; dir_list(cur, 0); }
            continue;
        }
        if (c == ' ') { if (strlen(cur) >= max) return 0; strcpy(out, cur); return 1; }
        if (c == '\n' && dir_count) {
            if (strlen(cur) + strlen(dir_names[sel]) + 2 >= sizeof cur) continue;
            if (cur[0] && cur[strlen(cur) - 1] != ':') strcat(cur, "/");
            strcat(cur, dir_names[sel]); sel = 0; dir_list(cur, 0);
        }
        if (c == 'n') {                                        /* nouveau dossier dans le dossier courant */
            char nm[26], path[40]; nm[0] = 0;
            line(ROWS - 3, T(S_NEWDIR));
            if (edit_field(ROWS - 2, nm, 24) && nm[0] && strlen(cur) + strlen(nm) + 2 < sizeof path) {
                strcpy(path, cur); if (cur[0] && cur[strlen(cur) - 1] != ':') strcat(path, "/"); strcat(path, nm);
                if (loci_mkdir(cur[0] ? path : nm) >= 0) { sel = 0; dir_list(cur, 0); for (i = 0; i < dir_count; ++i) if (!strcmp(dir_names[i], nm)) sel = i; }
            }
        }
        if (c == 'b') {
            char *sl = strrchr(cur, '/');
            if (sl) *sl = 0;
            else if (cur[1] == ':' && cur[2]) cur[2] = 0;
            else if (cur[0] && cur[1] != ':') cur[0] = 0;
            else if (nvol > 1) { at_volumes = 1; sel = 0; dir_list("", 1); continue; }
            sel = 0; dir_list(cur, 0);
        }
    }
}

static const char *const lang_names[3] = { "fr", "en", "es" };

static void config_draw(const char *host, const char *port, const char *dir, const char *pass, unsigned char tls)
{
    title(T(S_T_CFG));
    line(2, T(S_C_HOST));            line(3, host);
    line(5, T(S_C_CONN));                        line(6, tls ? "  HTTP 8998    > TLS 443 (modem)" : "> HTTP 8998      TLS 443 (modem)");
    line(8, T(S_C_PORT));                             line(9, port);
    line(11, T(S_C_DIR)); line(12, dir[0] ? dir : T(S_C_ROOT));
    line(14, T(S_C_PASS));    line(15, pass[0] ? "********" : T(S_C_NONE));
    line(17, T(S_C_LANG));
    status(T(S_K_CFG));
}

static void config_screen(void)
{
    char host[40], port[6], dir[32], pass[32];
    unsigned char tls;
    strcpy(host, http_host); strcpy(port, http_port); strcpy(dir, dl_dir); strcpy(pass, http_pass);
    tls = strcmp(port, "443") == 0;
    config_draw(host, port, dir, pass, tls);
    if (!edit_field(3, host, 38) || !host[0]) goto out;
    if (!choose2(6, "HTTP 8998", "TLS 443 (modem)", &tls)) goto out;
    if (tls) strcpy(port, "443"); else if (!strcmp(port, "443") || !port[0]) strcpy(port, "8998");   /* port perso conservé */
    line(9, port);
    if (!edit_field(9, port, 5) || !port[0]) goto out;
    if (!browse_dir(dir, 30)) goto out;
    config_draw(host, port, dir, pass, tls);
    if (!edit_field(15, pass, 30)) goto out;
    {   /* langue : espace = suivante (fr, en, es), entrée = valider */
        unsigned char l = lang_code[0] == 'e' ? (lang_code[1] == 'n' ? 1 : 2) : 0, c;
        for (;;) {
            char s[COLS + 1]; unsigned char k;
            s[0] = 0;
            for (k = 0; k < 3; ++k) { strcat(s, k == l ? "> " : "  "); strcat(s, lang_names[k]); strcat(s, "   "); }
            line(18, s);
            c = key();
            if (c == '\n') break;
            if (c == CH_ESC) goto out;
            if (c == ' ' || c == 'j' || c == 'n') l = (unsigned char)((l + 1) % 3);
            if (c == 'k' || c == 'b') l = (unsigned char)((l + 2) % 3);
        }
        strcpy(http_host, host); strcpy(http_port, port); strcpy(dl_dir, dir); strcpy(http_pass, pass);
        if (!lang_set(lang_names[l])) { status(T(S_LNG_MISSING)); cgetc(); }
    }
    status(config_save() ? T(S_SAVED) : T(S_APPLIED));
    while (key() != 'b') ;
    return;
out:
    status(T(S_CANCELLED));
    while (key() != 'b') ;
}

/* indicateur d'activité pendant un téléchargement : -\|/ tournant en bas à droite (écriture
 * directe dans l'écran TEXT, sans passer par conio : quelques cycles, entre deux blocs de
 * 128 octets à 9600 bauds) et « nn Ko » tous les 1 024 octets */
static void spinner(unsigned long received)
{
    static unsigned char phase;
    static unsigned int kb;                       /* Ko affichés : compteur 16 bits, pas de division 32 bits
                                                     (≈ 10 000 cycles en cc65 : l'anneau de 32 octets débordait) */
    static const char wheel[4] = { '-', '\\', '|', '/' };
    *(char *)(0xBB80 + (ROWS - 1) * COLS + COLS - 1) = wheel[phase++ & 3];
    if (received == 128) kb = (unsigned int)(dl_base >> 10);            /* début de tranche */
    if (((unsigned int)received & 1023) == 0 || received == 128) {
        unsigned int v; char d[8]; unsigned char m = 0;
        char *scr = (char *)(0xBB80 + (ROWS - 1) * COLS + COLS - 8);
        if (((unsigned int)received & 1023) == 0) ++kb;
        v = kb;
        do { d[m++] = '0' + (unsigned char)(v % 10); v /= 10; } while (v && m < 5);
        while (m < 5) d[m++] = ' ';
        while (m) *scr++ = d[--m];
        *scr++ = ' '; *scr++ = 'K';
    }
}

/* décimal non signé 16 bits à la fin de d (terminé par NUL) */
static void cat_uint(char *d, unsigned int v)
{
    char t[6]; unsigned char m = 0;
    d += strlen(d);
    do { t[m++] = '0' + (unsigned char)(v % 10); v /= 10; } while (v);
    while (m) *d++ = t[--m];
    *d = 0;
}

/* Appelé entre deux tranches (connexion close : les divisions 32 bits ne coûtent plus rien
 * à la réception). Ligne ROWS-3 : « fichier i/n nom » ; ligne ROWS-2 : barre de progression ;
 * statut : « telechargement nom » (la roue et les Ko du spinner s'y ajoutent pendant la
 * réception), « recu x / y Ko », ou « verification CRC-32 ». */
static void dl_progress_cb(const char *name, unsigned long bytes)
{
    char s[COLS + 1];
    strcpy(s, T(S_P_FILE)); cat_uint(s, dl_index + 1); strcat(s, "/"); cat_uint(s, dl_nfiles);
    strcat(s, " : "); strncat(s, name, COLS - 16); line(ROWS - 3, s);
    if (dl_total != 0xFFFFFFFFUL && dl_total) {                 /* [#####.....] 30 cases */
        unsigned char k, f = (unsigned char)(bytes >= dl_total ? 30 : bytes * 30 / dl_total);
        s[0] = '[';
        for (k = 0; k < 30; ++k) s[1 + k] = k < f ? '#' : '.';
        s[31] = ']'; s[32] = ' '; s[33] = 0;
        cat_uint(s, (unsigned int)(bytes >= dl_total ? 100 : bytes * 100 / dl_total)); strcat(s, "%");
        line(ROWS - 2, s);
    } else line(ROWS - 2, "");
    if (dl_phase) { strcpy(s, T(S_P_VERIFY)); strncat(s, name, 18); }
    else if (!bytes) { strcpy(s, T(S_P_DOWNLOAD)); strncat(s, name, 16); }
    else {
        strcpy(s, T(S_P_RECEIVED)); cat_uint(s, (unsigned int)(bytes >> 10));
        if (dl_total != 0xFFFFFFFFUL) { strcat(s, " / "); cat_uint(s, (unsigned int)((dl_total + 1023) >> 10)); }
        strcat(s, " Ko");
    }
    status(s);
}

/* écran d'aide (?) */
static void help_screen(void)
{
    title(T(S_T_HELP));
    wrap(2, T(S_HELP));
    status(T(S_K_ANYKEY));
    cgetc();
}

/* --- écran principal : onglets de catégories, grille de cartes 4×2 ou liste compacte --- */

static unsigned int hash16(const char *t)
{
    unsigned int h = 0;
    while (*t) h = h * 31 + (unsigned char)*t++;
    return h;
}

static unsigned char is_dev(const char *id)
{
    unsigned int h = hash16(id); unsigned char i;
    for (i = 0; i < ndev; ++i) if (dev_hash[i] == h) return 1;
    return 0;
}

/* texte brut à l'écran (sans conio : les attributs série < 32 passent tels quels) */
static void put(unsigned char x, unsigned char y, const char *t, unsigned char n, unsigned char inv)
{
    unsigned char *p = SCR + y * COLS + x;
    while (n-- && *t) *p++ = (unsigned char)*t++ | inv;
}

/* texte à gauche sur w colonnes, complété d'espaces (ne touche pas la jaquette à droite) */
static void put_pad(unsigned char y, const char *t, unsigned char w)
{
    unsigned char *p = SCR + y * COLS;
    while (w && *t) { *p++ = (unsigned char)*t++; --w; }
    while (w--) *p++ = ' ';
}

/* Jaquette « OLR1 » (/gfx/<id>?fmt=oric, prophetd ≥ 0.23.0) : mosaïque du jeu alternatif,
 * code = 32 + motif de 2×3 blocs ; par ligne : papier, encre, codes. Affichée en haut à
 * droite de la fiche : 3 cases d'attribut (papier, encre, jeu alternatif $09) + les codes. */
#define COVER_COL 20
#define COVER_LEN (6 + 9 * (2 + 17))          /* 177 octets */
static unsigned char cover[COVER_LEN + 1];    /* + NUL ajouté par http_get */
static unsigned char cover_ok;

static void cover_load(const char *id)
{
    char path[64]; unsigned int len;
    cover_ok = 0;
    strcpy(path, "/gfx/"); strncat(path, id, 40); strcat(path, "?fmt=oric");
    if (!http_get(path, 0, (char *)cover, sizeof cover, &len)) return;
    if (http_status != 200 || len != COVER_LEN || memcmp(cover, "OLR1", 4)) return;   /* 404 : pas de jaquette */
    cover_ok = cover[4] == 17 && cover[5] == 9;
}

static void cover_draw(void)
{
    unsigned char r, c; const unsigned char *d = cover + 6;
    for (r = 0; r < 9; ++r, d += 2 + 17) {
        unsigned char *p = SCR + (2 + r) * COLS + COVER_COL;
        p[0] = 0x10 | (d[0] & 7); p[1] = d[1] & 7; p[2] = 0x09;
        for (c = 0; c < 17; ++c) p[3 + c] = d[2 + c];
    }
}

static const char *tab_label(unsigned char t)
{
    if (!t) return T(S_TAB_ALL);
    return strcmp(cat_names[t - 1], "en-developpement") ? cat_names[t - 1] : T(S_TAB_BETA);   /* comme ProphetGui */
}

/* ligne 1 : onglets ; l'onglet courant en vidéo inverse, < > si ça déborde */
static void draw_tabs(void)
{
    unsigned char start = 0, x, t, w;
    line(1, "");
    if (search_key[0]) {
        char l[COLS + 1]; strcpy(l, " ? "); strncat(l, search_key, 30); strcat(l, " ");
        put(0, 1, l, COLS, 0x80); return;
    }
    for (;;) {                                  /* premier onglet affiché : le courant doit tenir */
        w = start ? 1 : 0;
        for (t = start; t <= tab; ++t) w += (unsigned char)strlen(tab_label(t)) + 2;
        if (w <= COLS - 1 || start == tab) break;
        ++start;
    }
    x = 0;
    if (start) { put(0, 1, "<", 1, 0); x = 1; }
    for (t = start; t <= ncats; ++t) {
        const char *l = tab_label(t); unsigned char n = (unsigned char)strlen(l);
        if (x + n + 2 > COLS - 1) { put(COLS - 1, 1, ">", 1, 0); break; }
        put(x, 1, " ", 1, t == tab ? 0x80 : 0); put(x + 1, 1, l, n, t == tab ? 0x80 : 0); put(x + 1 + n, 1, " ", 1, t == tab ? 0x80 : 0);
        x += n + 2;
    }
}

/* Carte i (10 colonnes × 9 lignes) : papier/encre en attributs série, initiale en double
 * hauteur (ligne paire = moitié haute, ligne impaire = moitié basse, cf. ULA), titre sur
 * 2 lignes de 7 coupées aux espaces, bandeau « EN DEV » rouge sur blanc. Carte choisie :
 * fond blanc, encre noire, bandes de sa couleur en haut et en bas. */
static void draw_card(unsigned char i, unsigned char sel)
{
    unsigned char cx = (i & 3) * 10, cy = 3 + (i >> 2) * 10, r, k, paper = 0, ink = 7, cut = 7;
    const char *t = i < listing.count ? listing.item[i].title : 0;
    unsigned char dev = t && is_dev(listing.item[i].id), n = t ? (unsigned char)strlen(t) : 0;
    if (t) {
        paper = 1 + hash16(t) % 6; ink = (paper == 2 || paper == 3 || paper == 6) ? 0 : 7;
        if (n <= 7) cut = n;                                               /* une seule ligne */
        else { for (k = 7; k && t[k] != ' '; --k) ; if (k) cut = k; }      /* coupure au dernier espace */
    }
    for (r = 0; r < 9; ++r) {
        unsigned char *p = SCR + (cy + r) * COLS + cx;
        unsigned char band = i == sel && (r == 0 || r == 8);
        p[0] = 0x10 | (i == sel && !band ? 7 : paper); p[1] = i == sel && !band ? 0 : ink;
        for (k = 2; k < 9; ++k) p[k] = ' ';
        p[9] = 0x10;                              /* intervalle noir */
        if (!t) continue;
        if (r == 1 || r == 2) {                   /* initiale, double hauteur */
            const char *c = t; while (*c == ' ') ++c;
            p[2] = 0x0A; p[5] = (unsigned char)((*c >= 'a' && *c <= 'z') ? *c - 32 : *c);
        }
        if (r == 4) for (k = 0; k < cut && k < n; ++k) p[2 + k] = (unsigned char)t[k];
        if (r == 5) { const char *u = t + cut; while (*u == ' ') ++u; for (k = 0; k < 7 && u[k]; ++k) p[2 + k] = (unsigned char)u[k]; }
        if (r == 7 && dev) { p[0] = 0x17; p[1] = 1; put(cx + 2, cy + r, T(S_EN_DEV), 6, 0); }
    }
}

static void draw_page_line(const char *keys)
{
    char s[COLS + 1];
    strcpy(s, T(S_PAGE)); cat_uint(s, page + 1); strcat(s, "/"); cat_uint(s, listing.pages ? listing.pages : 1);
    strcat(s, "  "); cat_uint(s, listing.total); strcat(s, T(S_PROG)); line(ROWS - 3, s);
    status(keys);
}

static void draw_main(unsigned char sel)
{
    unsigned char i;
    title(search_key[0] ? T(S_T_SEARCH) : T(S_T_CATALOGUE));
    draw_tabs();
    if (view_list) {
        char l[COLS + 1];
        for (i = 0; i < listing.count && i < 16; ++i) {
            l[0] = i == sel ? '>' : ' '; l[1] = ' ';
            strncpy(l + 2, listing.item[i].title, COLS - 8); l[COLS - 6] = 0;
            if (is_dev(listing.item[i].id)) strcat(l, T(S_DEV_MARK));
            line(3 + i, l);
        }
    } else {
        for (i = 0; i < 8; ++i) draw_card(i, sel);
        if (sel < listing.count) {                 /* titre de la carte choisie, double hauteur (22 paire) */
            unsigned char *p = SCR + 22 * COLS;
            p[0] = 0x0A; put(1, 22, listing.item[sel].title, COLS - 1, 0);
            p += COLS; p[0] = 0x0A; put(1, 23, listing.item[sel].title, COLS - 1, 0);
        }
    }
    if (!listing.count) line(5, T(S_EMPTY));
    draw_page_line(search_key[0] ? T(S_K_SEARCHRES) : T(S_K_MAIN));
}

static unsigned char load_list(void)
{
    char path[80];
    if (search_key[0]) { strcpy(path, "/search/"); strcat(path, search_key); }
    else { strcpy(path, "/list/"); strcat(path, tab ? cat_names[tab - 1] : "all"); }
    strcat(path, view_list ? "?platform=oric&sort=date&ord=desc&ipp=16&page=" : "?platform=oric&sort=date&ord=desc&ipp=8&page=");
    cat_uint(path, page);                          /* comme ProphetGui : les plus récents d'abord */
    if (!fetch(path)) return 0;
    if (!cli_parse_listing(body, &listing)) { status(T(S_BADREPLY)); return 0; }
    return 1;
}

/* catégories (onglets) et ids de en-developpement (marqueur EN DEV) */
static unsigned char load_cats(void)
{
    unsigned char i, has_dev = 0;
    if (!fetch("/cat?platform=oric") || !cli_parse_cat(body, cats, MAX_CATS, &ncats)) return 0;
    for (i = 0; i < ncats; ++i) {
        strncpy(cat_names[i], cats[i].name, 17); cat_names[i][17] = 0;
        if (!strcmp(cat_names[i], "en-developpement")) has_dev = 1;
    }
    if (tab > ncats) tab = 0;
    ndev = 0;
    if (has_dev && fetch("/list/en-developpement?platform=oric&ipp=16&page=0") && cli_parse_listing(body, &listing))
        for (i = 0; i < listing.count && ndev < MAX_DEV; ++i) dev_hash[ndev++] = hash16(listing.item[i].id);
    return 1;
}

static void info_keys(unsigned char installed)
{
    line(ROWS - 4, installed ? T(S_INSTALLED) : "");
    if (!dl_nfiles) status(T(S_K_BACK));
    else if (installed) status(T(S_K_INSTALLED));
    else status(T(S_K_INFO));
}

static void info_screen(const char *id)
{
    char path[80];
    unsigned char y, i, installed;
    strcpy(path, "/app/"); strcat(path, id);
    if (!fetch(path)) { cgetc(); return; }
    if (!cli_parse_info(body, &info)) { status(T(S_BADREPLY)); cgetc(); return; }
    status(T(S_CONNECTING));
    if (!dl_fetch_meta(id)) dl_nfiles = 0;                       /* fiche affichée quand même */
    dl_last_tap[0] = dl_last_dsk[0] = 0;
    installed = dl_nfiles && dl_installed(id);
    cover_load(id);
    title(T(S_T_INFO));
    if (is_dev(id)) { SCR[COLS] = 0x11; SCR[COLS + 1] = 7; put(2, 1, T(S_BANNER), 18, 0); }   /* bandeau rouge */
    if (cover_ok) {                          /* jaquette à droite : titre et auteur sur 19 colonnes */
        cover_draw();
        wrap_cols = COVER_COL - 1; wrap_end = 9; y = wrap(2, info.title);
        if (info.author) { char s[COLS + 1]; strcpy(s, T(S_BY)); strncat(s, info.author, COLS - 6); wrap_end = 11; y = wrap(y, s); }
        wrap_cols = COLS;
        if (y < 12) y = 12;
    } else {
        wrap_end = 5; y = wrap(2, info.title);
        if (info.author) { char s[COLS + 1]; strcpy(s, T(S_BY)); strncat(s, info.author, COLS - 6); line(y++, s); }
        ++y;
    }
    wrap_end = dl_nreq ? ROWS - 10 : ROWS - 8;
    if (info.description) y = wrap(y, info.description);
    wrap_end = ROWS - 1;
    /* fichiers (2 lignes au plus) puis composants minimums (informatif, rien n'est vérifié) */
    {
        char s[COLS * 2 + 1];
        unsigned char r = dl_nreq ? ROWS - 9 : ROWS - 7;
        strcpy(s, T(S_FILES_OPEN)); cat_uint(s, dl_nfiles ? dl_nfiles : info.files); strcat(s, ") : ");
        for (i = 0; i < dl_nfiles && strlen(s) + strlen(dl_files[i].name) + 2 < sizeof s; ++i) {
            if (i) strcat(s, ", ");
            strcat(s, dl_files[i].name);
        }
        if (i < dl_nfiles) strcat(s, "...");
        wrap_end = r + 2; wrap(r, s); wrap_end = ROWS - 1;
        if (dl_nreq) {
            strcpy(s, T(S_REQUIRES));
            for (i = 0; i < dl_nreq && strlen(s) + strlen(dl_req[i]) + 2 < sizeof s; ++i) { if (i) strcat(s, ", "); strcat(s, dl_req[i]); }
            wrap_end = ROWS - 5; wrap(ROWS - 7, s); wrap_end = ROWS - 1;
        }
    }
    if (!dl_nfiles && dl_error) { char s[COLS + 1]; strcpy(s, T(S_FILES_ERR)); strncat(s, dl_error, COLS - 11); line(ROWS - 3, s); }
    info_keys(installed);
    for (;;) {
        unsigned char c = key();
        if (c == 'b') return;
        if (c == '?' || c == '/') { help_screen(); return; }
        if (c == 'g' && dl_nfiles) {
            unsigned char n;
            line(ROWS - 4, "");
            http_tick = spinner;
            n = download_package(id, dl_progress_cb);
            http_tick = 0;
            if (n && !dl_error) {
                char s[COLS + 1]; strcpy(s, T(S_DONE)); cat_uint(s, n); strcat(s, dl_skipped ? T(S_N_ZIP) : T(S_N_FILES));
                strcat(s, (dl_last_tap[0] || dl_last_dsk[0]) ? T(S_K_LAUNCH) : T(S_K_BACK2));
                line(ROWS - 3, dl_verified ? T(S_OK_CRC) : T(S_OK_NOCRC));
                status(s);
            } else {
                char s[COLS + 1]; strcpy(s, T(S_FAILED)); strncat(s, dl_error ? dl_error : "?", 31);
                line(ROWS - 3, s); status(T(S_K_RETRY));
            }
        }
        if (c == 'l' && dl_last_dsk[0]) {
            /* disquette : montée en lecteur A (0) puis MIA_BOOT (Microdisc + BASIC 1.1) — le
             * LOCI bascule les ROM et resette l'Oric, qui démarre sur la disquette. */
            if (loci_mount(0, dl_last_dsk) < 0) { status(T(S_MNT_DSK_ERR)); continue; }
            clrscr(); cputs(T(S_DSK_MOUNTED)); cputs("\r\n"); cputs(dl_last_dsk); cputs("\r\n\r\n"); cputs(T(S_BOOTING)); cputs("\r\n");
            loci_boot(LOCI_BOOT_FDC | LOCI_BOOT_B11);
            status(T(S_BOOT_ERR)); continue;
        }
        if (c == 'l' && dl_last_tap[0]) {
            /* montage de la cassette sur le LOCI puis retour au BASIC : CLOAD"" charge (et
             * lance, si autorun) le programme — le LOCI joue la cassette montée. */
            if (loci_mount(LOCI_MNT_TAP, dl_last_tap) < 0) { status(T(S_MNT_TAP_ERR)); continue; }
            clrscr();
            cputs(T(S_TAP_MOUNTED)); cputs("\r\n"); cputs(dl_last_tap); cputs("\r\n\r\n"); cputs(T(S_TYPE_CLOAD)); cputs("\r\n\r\n");
            exit(0);
        }
    }
}

int main(void)
{
    unsigned char sel = 0, c;
    title("");
    if (!serial_probe(PROPHET_ACIA)) {             /* ni LOCI ni modem (Oric nu, émulateur sans --loci) */
        title("materiel absent");
        { unsigned char y = wrap(3, "Aucune interface serie (ACIA 6551 en $0380) : ProphetOric ne peut pas joindre le serveur.");
          y = wrap(y + 1, "Materiel requis : cartouche LOCI et modem PicoWiFiModemUSB.");
          y = wrap(y + 1, "Sans le materiel : page Jouer de ProphetOric sur prophet.3617.fr (LOCI et modem emules).");
          wrap(y + 1, "Sur PC : Phosphoric --loci --serial picowifi"); }
        status("une touche = retour au BASIC");
        cgetc(); clrscr(); return 1;
    }
    if (!loci_present()) {                           /* ACIA sans l'API MIA ($03AF) : catalogue seulement */
        title("LOCI absent");
        { unsigned char y = wrap(3, "Interface serie trouvee, mais pas l'API du LOCI ($03AF) : pas de stockage.");
          y = wrap(y + 1, "Le catalogue reste consultable ; telechargement, lancement et PROPHET.CFG sont impossibles.");
          wrap(y + 1, "Verifier la cartouche LOCI."); }
        status("une touche = continuer");
        cgetc();
    }
    serial_init(PROPHET_ACIA);
    config_load();                                   /* PROPHET.CFG sur le LOCI, sinon valeurs compilées */
    if (cfg_lang[0] && !lang_set(cfg_lang)) { status(T(S_LNG_MISSING)); cgetc(); }   /* 5e ligne : fr / en / es */
    if (!load_cats()) { cgetc(); return 1; }
    page = 0;
    if (!load_list()) cgetc();
    for (;;) {
        unsigned char per = view_list ? 16 : 8, reload = 0;
        if (sel >= listing.count) sel = listing.count ? listing.count - 1 : 0;
        arrows_move = 1;
        draw_main(sel);
        c = key();
        arrows_move = 0;
        if (c == 'q' && !search_key[0]) { clrscr(); return 0; }
        if (c == '?' || c == '/') help_screen();
        /* déplacement : grille (h/l ±1, j/k ±4, changement de page aux bords) ou liste (j/k ±1) */
        if (c == 'l' || (c == 'j' && view_list)) {
            if (sel + 1 < listing.count) ++sel;
            else if (page + 1 < listing.pages) { ++page; sel = 0; reload = 1; }
        }
        if (c == 'h' || (c == 'k' && view_list)) {
            if (sel) --sel;
            else if (page) { --page; sel = per - 1; reload = 1; }
        }
        if (c == 'j' && !view_list) { if (sel + 4 < listing.count) sel += 4; else if (page + 1 < listing.pages) { ++page; sel &= 3; reload = 1; } }
        if (c == 'k' && !view_list) { if (sel >= 4) sel -= 4; else if (page) { --page; sel += 4; reload = 1; } }
        if (c == 'n' && page + 1 < listing.pages) { ++page; sel = 0; reload = 1; }
        if (c == 'p' && page) { --page; sel = 0; reload = 1; }
        if ((c == '.' || c == ']') && !search_key[0]) { tab = tab < ncats ? tab + 1 : 0; page = 0; sel = 0; reload = 1; }
        if ((c == ',' || c == '[') && !search_key[0]) { tab = tab ? tab - 1 : ncats; page = 0; sel = 0; reload = 1; }
        if (c == 'v') { view_list = !view_list; page = 0; sel = 0; reload = 1; }
        if (c == 'b' && search_key[0]) { search_key[0] = 0; page = 0; sel = 0; reload = 1; }
        if (c == 's') {                                  /* recherche : /search/<clé> (titre, description, auteur) */
            char k[24]; k[0] = 0;
            title(T(S_T_SEARCH)); line(2, T(S_S_PROMPT)); status(T(S_K_SPROMPT));
            if (edit_field(3, k, 22) && k[0]) { strcpy(search_key, k); page = 0; sel = 0; }
            reload = 1;
        }
        if (c == 'c') { config_screen(); if (!load_cats()) { cgetc(); return 1; } page = 0; sel = 0; reload = 1; }
        if ((c == '\n' || c == ' ') && sel < listing.count) {
            char id[40];
            strncpy(id, listing.item[sel].id, 39); id[39] = 0;
            info_screen(id);
            reload = 1;                                  /* body réutilisé : recharger la liste */
        }
        if (reload && !load_list()) cgetc();
    }
}
