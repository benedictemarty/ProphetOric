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

#define IPP 16                       /* programmes par page (16 lignes de liste) */
#define ROWS 28
#define COLS 40

static char body[3072];              /* réponse courante (parsée en place) */
static struct cli_cat cats[8];
static unsigned char ncats;
static struct cli_listing listing;
static struct cli_info info;
static char cat_name[24];
static char search_key[24];           /* "" = liste de catégorie, sinon /search/<clé> */
static unsigned int page;

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
static unsigned char wrap(unsigned char y, const char *s)
{
    char buf[COLS + 1];
    while (*s && y < wrap_end) {
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
        title("dossier");
        strcpy(l, at_volumes ? "volumes du LOCI :" : "dossier : /"); if (!at_volumes) strncat(l, cur, COLS - 12);
        line(2, l);
        for (i = 0; i < dir_count && i < DIR_MAX; ++i) { l[0] = i == sel ? '>' : ' '; l[1] = ' '; strcpy(l + 2, dir_names[i]); line(4 + i, l); }
        if (!dir_count) line(4, at_volumes ? "  (aucun volume)" : "  (aucun sous-dossier)");
        status(at_volumes ? "entree=ouvrir  esc=annuler" : "entree b=parent espace=choisir n=nouveau esc");
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
            line(ROWS - 3, "nom du nouveau dossier :");
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

static void config_draw(const char *host, const char *port, const char *dir, const char *pass, unsigned char tls)
{
    title("configuration");
    line(2, "serveur (hote ou IP) :");            line(3, host);
    line(5, "connexion :");                        line(6, tls ? "  HTTP 8998    > TLS 443 (modem)" : "> HTTP 8998      TLS 443 (modem)");
    line(8, "port :");                             line(9, port);
    line(11, "dossier LOCI (entree = explorer) :"); line(12, dir[0] ? dir : "(racine du flash)");
    line(14, "mot de passe zones reservees :");    line(15, pass[0] ? "********" : "(aucun)");
    status("entree = champ suivant   echap = annuler");
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
    strcpy(http_host, host); strcpy(http_port, port); strcpy(dl_dir, dir); strcpy(http_pass, pass);
    status(config_save() ? "enregistre dans PROPHET.CFG - b retour" : "applique (PROPHET.CFG non ecrit) - b retour");
    while (key() != 'b') ;
    return;
out:
    status("annule - b retour");
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
    strcpy(s, "fichier "); cat_uint(s, dl_index + 1); strcat(s, "/"); cat_uint(s, dl_nfiles);
    strcat(s, " : "); strncat(s, name, COLS - 16); line(ROWS - 3, s);
    if (dl_total != 0xFFFFFFFFUL && dl_total) {                 /* [#####.....] 30 cases */
        unsigned char k, f = (unsigned char)(bytes >= dl_total ? 30 : bytes * 30 / dl_total);
        s[0] = '[';
        for (k = 0; k < 30; ++k) s[1 + k] = k < f ? '#' : '.';
        s[31] = ']'; s[32] = ' '; s[33] = 0;
        cat_uint(s, (unsigned int)(bytes >= dl_total ? 100 : bytes * 100 / dl_total)); strcat(s, "%");
        line(ROWS - 2, s);
    } else line(ROWS - 2, "");
    if (dl_phase) { strcpy(s, "verification CRC-32 "); strncat(s, name, 18); }
    else if (!bytes) { strcpy(s, "telechargement "); strncat(s, name, 16); }
    else {
        strcpy(s, "recu "); cat_uint(s, (unsigned int)(bytes >> 10));
        if (dl_total != 0xFFFFFFFFUL) { strcat(s, " / "); cat_uint(s, (unsigned int)((dl_total + 1023) >> 10)); }
        strcat(s, " Ko");
    }
    status(s);
}

/* écran d'aide (?) */
static void help_screen(void)
{
    title("aide");
    wrap(2, "j/k ou fleches : choisir\n"
            "entree : ouvrir     b : retour\n"
            "n/p : page suivante / precedente\n"
            "s : chercher (titre, auteur)\n"
            "c : configuration (serveur, dossier, mot de passe)\n"
            "q : retour au BASIC\n"
            "\n"
            "Fiche : g telecharge sur le LOCI (verifie par CRC-32), "
            "l lance : cassette montee + CLOAD\"\" ou disquette + demarrage Microdisc.\n"
            "\n"
            "Reglages : PROPHET.CFG sur le LOCI. "
            "Marqueurs de telechargement : .prophet/ dans le dossier.\n"
            "\n"
            "ProphetOric " VERSION " - prophet.3617.fr");
    status("une touche = retour");
    cgetc();
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
    status("j/k entree=ouvrir s=chercher c=config ?");
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
      strcpy(s + k, " j/k n/p entree=fiche b ?"); status(s); }
}

static unsigned char load_list(void)
{
    char path[80];
    char d[6]; unsigned char m = 0, k; unsigned int v = page;
    if (search_key[0]) { strcpy(path, "/search/"); strcat(path, search_key); }
    else { strcpy(path, "/list/"); strcat(path, cat_name); }
    strcat(path, "?platform=oric&sort=date&ord=desc&ipp=16&page=");   /* comme ProphetGui : les plus récents d'abord */
    k = (unsigned char)strlen(path);
    do { d[m++] = '0' + v % 10; v /= 10; } while (v); while (m) path[k++] = d[--m]; path[k] = 0;
    if (!fetch(path)) return 0;
    if (!cli_parse_listing(body, &listing)) { status("reponse inattendue"); return 0; }
    return 1;
}

/* bas de fiche : état du paquet (ligne ROWS-4) et touches */
static void info_keys(unsigned char installed)
{
    line(ROWS - 4, installed ? "deja telecharge (identique)" : "");
    if (!dl_nfiles) status("b retour");
    else if (installed) status("l lancer   g retelecharger   b retour");
    else status("g telecharger sur le LOCI   b retour");
}

static void info_screen(const char *id)
{
    char path[80];
    unsigned char y, i, installed;
    strcpy(path, "/app/"); strcat(path, id);
    if (!fetch(path)) { cgetc(); return; }
    if (!cli_parse_info(body, &info)) { status("reponse inattendue"); cgetc(); return; }
    status("connexion...");
    if (!dl_fetch_meta(id)) dl_nfiles = 0;                       /* fiche affichée quand même */
    dl_last_tap[0] = dl_last_dsk[0] = 0;
    installed = dl_nfiles && dl_installed(id);
    title("fiche");
    wrap_end = 5; y = wrap(2, info.title);
    if (info.author) { char s[COLS + 1]; strcpy(s, "par : "); strncat(s, info.author, COLS - 6); line(y++, s); }
    ++y;
    wrap_end = dl_nreq ? ROWS - 10 : ROWS - 8;
    if (info.description) y = wrap(y, info.description);
    wrap_end = ROWS - 1;
    /* fichiers (2 lignes au plus) puis composants minimums (informatif, rien n'est vérifié) */
    {
        char s[COLS * 2 + 1];
        unsigned char r = dl_nreq ? ROWS - 9 : ROWS - 7;
        strcpy(s, "fichiers ("); cat_uint(s, dl_nfiles ? dl_nfiles : info.files); strcat(s, ") : ");
        for (i = 0; i < dl_nfiles && strlen(s) + strlen(dl_files[i].name) + 2 < sizeof s; ++i) {
            if (i) strcat(s, ", ");
            strcat(s, dl_files[i].name);
        }
        if (i < dl_nfiles) strcat(s, "...");
        wrap_end = r + 2; wrap(r, s); wrap_end = ROWS - 1;
        if (dl_nreq) {
            strcpy(s, "! peut ne pas fonctionner sans : ");
            for (i = 0; i < dl_nreq && strlen(s) + strlen(dl_req[i]) + 2 < sizeof s; ++i) { if (i) strcat(s, ", "); strcat(s, dl_req[i]); }
            wrap_end = ROWS - 5; wrap(ROWS - 7, s); wrap_end = ROWS - 1;
        }
    }
    if (!dl_nfiles && dl_error) { char s[COLS + 1]; strcpy(s, "fichiers : "); strncat(s, dl_error, COLS - 11); line(ROWS - 3, s); }
    info_keys(installed);
    for (;;) {
        unsigned char c = key();
        if (c == 'b') return;
        if (c == '?' || c == 'h') { help_screen(); return; }
        if (c == 'g' && dl_nfiles) {
            unsigned char n;
            line(ROWS - 4, "");
            http_tick = spinner;
            n = download_package(id, dl_progress_cb);
            http_tick = 0;
            if (n && !dl_error) {
                char s[COLS + 1]; strcpy(s, "termine : "); cat_uint(s, n); strcat(s, dl_skipped ? " fich. (zip ignore)" : " fichier(s)");
                strcat(s, (dl_last_tap[0] || dl_last_dsk[0]) ? " l=lancer" : " b=retour");
                line(ROWS - 3, dl_verified ? "OK : empreintes CRC-32 verifiees" : "OK (serveur sans empreintes : non verifie)");
                status(s);
            } else {
                char s[COLS + 1]; strcpy(s, "echec : "); strncat(s, dl_error ? dl_error : "?", 31);
                line(ROWS - 3, s); status("g reessayer   b retour");
            }
        }
        if (c == 'l' && dl_last_dsk[0]) {
            /* disquette : montée en lecteur A (0) puis MIA_BOOT (Microdisc + BASIC 1.1) — le
             * LOCI bascule les ROM et resette l'Oric, qui démarre sur la disquette. */
            if (loci_mount(0, dl_last_dsk) < 0) { status("montage disquette impossible"); continue; }
            clrscr(); cputs("Disquette montee en A :\r\n"); cputs(dl_last_dsk); cputs("\r\n\r\nDemarrage...\r\n");
            loci_boot(LOCI_BOOT_FDC | LOCI_BOOT_B11);
            status("MIA_BOOT refuse"); continue;
        }
        if (c == 'l' && dl_last_tap[0]) {
            /* montage de la cassette sur le LOCI puis retour au BASIC : CLOAD"" charge (et
             * lance, si autorun) le programme — le LOCI joue la cassette montée. */
            if (loci_mount(LOCI_MNT_TAP, dl_last_tap) < 0) { status("montage cassette impossible"); continue; }
            clrscr();
            cputs("Cassette montee sur le LOCI :\r\n"); cputs(dl_last_tap); cputs("\r\n\r\nTapez  CLOAD\"\"  pour charger.\r\n\r\n");
            exit(0);
        }
    }
}

static void list_screen(void)
{
    unsigned char sel = 0, c;
    page = 0;
    if (search_key[0]) { strcpy(cat_name, "? "); strncat(cat_name, search_key, 20); }
    if (!load_list()) { cgetc(); return; }
    for (;;) {
        draw_list(sel);
        c = key();
        if (c == 'b' || c == 'q') return;
        if (c == '?' || c == 'h') help_screen();
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
    if (!serial_probe(ACIA_BASE_LOCI)) {             /* ex. page « Jouer » du site : ni LOCI ni modem */
        title("materiel absent");
        { unsigned char y = wrap(3, "Aucune interface serie (ACIA 6551 en $0380) : ProphetOric ne peut pas joindre le serveur.");
          y = wrap(y + 1, "Materiel requis : cartouche LOCI et modem PicoWiFiModemUSB.");
          y = wrap(y + 1, "Le lecteur du site (page Jouer) n'emule ni l'un ni l'autre : ProphetOric n'y fonctionne pas.");
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
    serial_init(ACIA_BASE_LOCI);
    config_load();                                   /* PROPHET.CFG sur le LOCI, sinon valeurs compilées */
    if (!fetch("/cat?platform=oric") || !cli_parse_cat(body, cats, 8, &ncats)) { cgetc(); return 1; }
    for (;;) {
        draw_cats(sel);
        c = key();
        if (c == 'q') { clrscr(); return 0; }
        if (c == '?' || c == 'h') help_screen();
        if (c == 's') {                                  /* recherche : /search/<clé> (titre, description, auteur) */
            title("recherche"); line(2, "mot a chercher (titre, auteur) :"); status("entree = chercher   echap = annuler");
            search_key[0] = 0;
            if (edit_field(3, search_key, 22) && search_key[0]) list_screen();
            search_key[0] = 0;
            if (!fetch("/cat?platform=oric") || !cli_parse_cat(body, cats, 8, &ncats)) { cgetc(); return 1; }
        }
        if (c == 'c') { config_screen(); if (!fetch("/cat?platform=oric") || !cli_parse_cat(body, cats, 8, &ncats)) { cgetc(); return 1; } }
        if (c == 'j' && sel + 1 < ncats) ++sel;
        if (c == 'k' && sel) --sel;
        if (c == '\n' && ncats) {
            strncpy(cat_name, cats[sel].name, 23); cat_name[23] = 0;
            search_key[0] = 0;
            list_screen();
            if (!fetch("/cat?platform=oric") || !cli_parse_cat(body, cats, 8, &ncats)) { cgetc(); return 1; }
        }
    }
}
