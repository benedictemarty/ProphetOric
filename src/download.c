/* download.c — métadonnées d'un paquet (/files, /crc32, /requires), puis chaque
 * /files/<id>/<n> en flux (http_get_stream) écrit sur le LOCI par blocs de 128 octets,
 * relu et vérifié par CRC-32 (/crc32/<id>) ; marqueur « déjà téléchargé » en
 * <dossier>/.prophet/<id>.crc (même principe que .prophetgui.sha de Neo6502ProphetGui). */
#include <string.h>
#include "download.h"
#include "http.h"
#include "cli.h"
#include "loci.h"
#include "crc32.h"

#define DL_CHUNK 32768UL          /* taille d'une tranche Range */
#define MARK_DIR ".prophet"

char dl_dir[32] = "";
const char *dl_error;
char dl_last_tap[64] = "";
char dl_last_dsk[64] = "";
unsigned char dl_skipped;
unsigned long dl_base;
unsigned char dl_index, dl_phase, dl_verified;
unsigned long dl_total;
unsigned char dl_nfiles, dl_ncrc, dl_nreq;
struct cli_file dl_files[CLI_MAX_FILES];
char *dl_crc[CLI_MAX_FILES];
char *dl_req[DL_MAX_REQ];
static int cur_fd = -1;
static char list[512];
static char crcbuf[CLI_MAX_FILES * 10 + 8];
static char reqbuf[96];
static char mark[300];

static unsigned char sink(const unsigned char *block, unsigned char n)
{
    return loci_write((unsigned char)cur_fd, block, n) == (int)n;
}

static void ulnum(char *d, unsigned long v) { unsigned char k = 0; char t[11]; do { t[k++] = '0' + (unsigned char)(v % 10); v /= 10; } while (v); while (k) *d++ = t[--k]; *d = 0; }
static void num(char *d, unsigned char v) { unsigned char k = 0; char t[4]; do { t[k++] = '0' + v % 10; v /= 10; } while (v); while (k) *d++ = t[--k]; *d = 0; }

/* chemin dans le dossier cible : "" → nom, "jeux" → jeux/nom, "1:" (racine du volume 1) → 1:nom */
static void in_dir(char *dst, const char *name)
{
    dst[0] = 0;
    if (dl_dir[0]) { strcpy(dst, dl_dir); if (dst[strlen(dst) - 1] != ':') strcat(dst, "/"); }
    strcat(dst, name);
}

static void mark_path(char *dst, const char *id)
{
    in_dir(dst, MARK_DIR "/"); strncat(dst, id, 24); strcat(dst, ".crc");
}

/* GET facultatif : 1 et buf rempli si 200, 0 sinon (404 = « le serveur n'en a pas ») */
static unsigned char get_opt(const char *what, const char *id, char *buf, unsigned int max)
{
    char path[64]; unsigned int len;
    strcpy(path, what); strncat(path, id, 40);
    buf[0] = 0;
    return http_get(path, 0, buf, max, &len) && http_status == 200;
}

unsigned char dl_fetch_meta(const char *id)
{
    unsigned int len;
    char path[64];
    dl_error = 0; dl_nfiles = dl_ncrc = dl_nreq = 0;
    strcpy(path, "/files/"); strncat(path, id, 40);
    if (!http_get(path, 0, list, sizeof list, &len)) { dl_error = http_error; return 0; }
    if (http_status != 200) { dl_error = "fichiers introuvables"; return 0; }
    if (!cli_parse_files(list, dl_files, CLI_MAX_FILES, &dl_nfiles) || dl_nfiles == 0) { dl_error = "liste de fichiers invalide"; return 0; }
    /* empreintes : une par fichier, sinon pas de vérification (serveur < 0.13.1 : 404) */
    if (get_opt("/crc32/", id, crcbuf, sizeof crcbuf) && cli_parse_lines(crcbuf, dl_crc, CLI_MAX_FILES, &dl_ncrc) && dl_ncrc != dl_nfiles) dl_ncrc = 0;
    if (get_opt("/requires/", id, reqbuf, sizeof reqbuf)) cli_parse_lines(reqbuf, dl_req, DL_MAX_REQ, &dl_nreq);
    return 1;
}

static unsigned char ends(const char *s, const char *lo, const char *up)
{
    unsigned char n = (unsigned char)strlen(s);
    return n > 4 && (!strcmp(s + n - 4, lo) || !strcmp(s + n - 4, up));
}

static unsigned char exists(const char *path)
{
    int fd = loci_open(path, LOCI_O_RDONLY);
    if (fd < 0) return 0;
    loci_close((unsigned char)fd);
    return 1;
}

/* contenu du marqueur : "C:" + empreintes de tous les fichiers bout à bout, "T:" .tap, "D:" .dsk */
static void mark_build(char *m)
{
    unsigned char i;
    strcpy(m, "C:");
    for (i = 0; i < dl_ncrc; ++i) strncat(m, dl_crc[i], 8);
    strcat(m, "\nT:"); strcat(m, dl_last_tap);
    strcat(m, "\nD:"); strcat(m, dl_last_dsk);
    strcat(m, "\n");
}

unsigned char dl_installed(const char *id)
{
    char path[64]; int fd, r; unsigned int n = 0; char *t, *d, *e;
    if (!dl_ncrc || !loci_present()) return 0;
    mark_path(path, id);
    fd = loci_open(path, LOCI_O_RDONLY);
    if (fd < 0) return 0;
    while (n < sizeof mark - 1 && (r = loci_read((unsigned char)fd, (unsigned char *)mark + n, (unsigned char)(sizeof mark - 1 - n > 255 ? 255 : sizeof mark - 1 - n))) > 0) n += (unsigned int)r;
    loci_close((unsigned char)fd);
    mark[n] = 0;
    t = strstr(mark, "\nT:"); d = strstr(mark, "\nD:");
    if (!t || !d || mark[0] != 'C' || mark[1] != ':') return 0;
    *t = 0; t += 3; *d = 0; d += 3;
    if ((e = strchr(d, '\n'))) *e = 0;
    {   /* empreintes actuelles du serveur = celles enregistrées ? */
        unsigned char i; const char *p = mark + 2;
        if (strlen(p) != (unsigned int)dl_ncrc * 8) return 0;
        for (i = 0; i < dl_ncrc; ++i, p += 8) if (strncmp(p, dl_crc[i], 8)) return 0;
    }
    if (t[0] && !exists(t)) return 0;
    if (d[0] && !exists(d)) return 0;
    strcpy(dl_last_tap, t); strcpy(dl_last_dsk, d);
    return 1;
}

/* relit le fichier sur le LOCI et compare son CRC-32 à l'empreinte du serveur */
static unsigned char verify(const char *dst, const char *want)
{
    char h[9]; int fd, r; unsigned char i;
    fd = loci_open(dst, LOCI_O_RDONLY);
    if (fd < 0) return 0;
    crc32_start();
    while ((r = loci_read_crc((unsigned char)fd, 255)) > 0) ;
    loci_close((unsigned char)fd);
    if (r < 0) return 0;
    crc32_hex(h);
    for (i = 0; i < 8; ++i) if (h[i] != (want[i] >= 'A' && want[i] <= 'F' ? want[i] + 32 : want[i])) return 0;
    return 1;
}

static void mark_write(const char *id)
{
    char path[64]; int fd; unsigned int n, k;
    in_dir(path, MARK_DIR); loci_mkdir(path);                   /* existe déjà : erreur ignorée */
    mark_path(path, id);
    fd = loci_open(path, LOCI_O_WRONLY | LOCI_O_CREAT | LOCI_O_TRUNC);
    if (fd < 0) return;
    mark_build(mark);
    n = (unsigned int)strlen(mark);
    for (k = 0; k < n; k += 128) loci_write((unsigned char)fd, (unsigned char *)mark + k, (unsigned char)(n - k > 128 ? 128 : n - k));
    loci_close((unsigned char)fd);
}

unsigned char download_package(const char *id, dl_progress progress)
{
    unsigned char i, done = 0;
    char path[96];
    dl_error = 0; dl_verified = 0;
    if (!loci_present()) { dl_error = "pas de LOCI (stockage)"; return 0; }
    if (!dl_nfiles) { dl_error = "liste de fichiers vide"; return 0; }
    if (dl_dir[0] && dl_dir[strlen(dl_dir) - 1] != ':') loci_mkdir(dl_dir);   /* existe déjà : erreur ignorée */
    dl_last_tap[0] = 0; dl_last_dsk[0] = 0; dl_skipped = 0;
    for (i = 0; i < dl_nfiles; ++i) {
        unsigned long got;
        char dst[64];
        const char *name = dl_files[i].name;
        unsigned char ln = (unsigned char)strlen(name);
        dl_index = i; dl_phase = 0; dl_total = 0xFFFFFFFFUL;
        if (ln > 30) { dl_error = "nom de fichier trop long"; return done; }
        if (ends(name, ".zip", ".ZIP")) { ++dl_skipped; continue; }
        in_dir(dst, name);
        strcpy(path, "/files/"); strcat(path, id); strcat(path, "/"); num(path + strlen(path), i);
        if (progress) progress(name, 0);
        /* Réception en flux par tranches de 32 Ko (Range: bytes=a-b, réponse 206) : chaque
         * réponse tient dans les tampons du modem (le PicoWiFi émulé de Phosphoric jette au-delà
         * de 64 Ko sans contrôle de flux) et une coupure ne coûte que la tranche en cours
         * (reprise en ajout, 3 essais par tranche). Un serveur sans Range répond 200 : on
         * prend alors le fichier entier d'un coup. */
        {
            unsigned long total = 0, expect = 0xFFFFFFFFUL;
            unsigned char tries = 0, ok = 0;
            cur_fd = loci_open(dst, LOCI_O_WRONLY | LOCI_O_CREAT | LOCI_O_TRUNC);
            if (cur_fd < 0) { dl_error = "fichier refuse par le LOCI (dossier ?)"; return done; }
            for (;;) {
                char range[32];
                dl_base = total;
                strcpy(range, "bytes="); ulnum(range + 6, total); strcat(range, "-"); ulnum(range + strlen(range), total + DL_CHUNK - 1);
                if (http_get_stream(path, range, sink, &got)) {
                    if (http_status == 206 || (http_status == 200 && total == 0)) {
                        if (http_status == 200) expect = http_length;                       /* pas de Range : fichier entier */
                        else if (expect == 0xFFFFFFFFUL) expect = http_range_total;          /* taille totale : Content-Range a-b/total */
                        dl_total = expect;
                        total += got;
                        if (http_status == 200 || total >= expect) { ok = total >= expect || expect == 0xFFFFFFFFUL; break; }
                        if (got == 0) { if (++tries > 3) { dl_error = "fichier incomplet"; break; } }
                        else tries = 0;
                        if (progress) progress(name, total);
                        continue;
                    }
                    if (http_status == 416) { ok = expect != 0xFFFFFFFFUL && total >= expect; break; }
                    dl_error = http_status == 404 ? "fichier introuvable" : "telechargement refuse"; break;
                }
                total += got;                                                               /* octets valides avant la coupure */
                if (++tries > 3) { dl_error = http_error ? http_error : "fichier incomplet"; break; }
                if (progress) progress(name, total);
                /* reprise : repositionnement exact (LSEEK) plutôt qu'en ajout — le NO CARRIER du
                 * modem a pu être écrit avant d'être reconnu, il sera recouvert */
                loci_close((unsigned char)cur_fd);
                cur_fd = loci_open(dst, LOCI_O_WRONLY);
                if (cur_fd < 0 || loci_seek_set((unsigned char)cur_fd, total) < 0) { dl_error = "reprise impossible"; break; }
            }
            loci_close((unsigned char)cur_fd);
            if (!ok) { if (!dl_error) dl_error = "fichier incomplet"; return done; }
            got = total;
        }
        if (progress) progress(name, got);
        if (dl_ncrc) {                                                  /* relecture + CRC-32 (hors réception) */
            dl_phase = 1;
            if (progress) progress(name, got);
            if (!verify(dst, dl_crc[i])) { dl_error = "EMPREINTE DIFFERENTE (garde)"; return done; }
        }
        /* fichier à lancer : le premier .tap / .dsk (/launch/<id> du serveur n'accepte que .neo/.bas) */
        if (ends(name, ".tap", ".TAP") && !dl_last_tap[0]) strcpy(dl_last_tap, dst);
        if (ends(name, ".dsk", ".DSK") && !dl_last_dsk[0]) strcpy(dl_last_dsk, dst);
        ++done;
    }
    if (dl_ncrc) { dl_verified = 1; mark_write(id); }
    return done;
}
