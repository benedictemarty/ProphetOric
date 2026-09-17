/* download.c — /files/<id> (format cli : n:"nom","chemin/",…) puis chaque
 * /files/<id>/<n> en flux (http_get_stream) écrit sur le LOCI par blocs de 128 octets. */
#include <string.h>
#include "download.h"
#include "http.h"
#include "cli.h"
#include "loci.h"

#define DL_CHUNK 32768UL          /* taille d'une tranche Range */

char dl_dir[32] = "";
const char *dl_error;
char dl_last_tap[64] = "";
char dl_last_dsk[64] = "";
unsigned char dl_skipped;
static int cur_fd = -1;
static char list[512];
static struct cli_file files[CLI_MAX_FILES];

static unsigned char sink(const unsigned char *block, unsigned char n)
{
    return loci_write((unsigned char)cur_fd, block, n) == (int)n;
}

static void ulnum(char *d, unsigned long v) { unsigned char k = 0; char t[11]; do { t[k++] = '0' + (unsigned char)(v % 10); v /= 10; } while (v); while (k) *d++ = t[--k]; *d = 0; }
static void num(char *d, unsigned char v) { unsigned char k = 0; char t[4]; do { t[k++] = '0' + v % 10; v /= 10; } while (v); while (k) *d++ = t[--k]; *d = 0; }

unsigned char download_package(const char *id, dl_progress progress)
{
    unsigned int len; unsigned char nf, i, done = 0;
    char path[96];
    dl_error = 0;
    if (!loci_present()) { dl_error = "pas de LOCI (stockage)"; return 0; }
    strcpy(path, "/files/"); strcat(path, id);
    if (!http_get(path, 0, list, sizeof list, &len)) { dl_error = http_error; return 0; }
    if (http_status != 200) { dl_error = "fichiers introuvables"; return 0; }
    if (!cli_parse_files(list, files, CLI_MAX_FILES, &nf) || nf == 0) { dl_error = "liste de fichiers invalide"; return 0; }
    if (dl_dir[0]) loci_mkdir(dl_dir);            /* existe déjà : erreur ignorée */
    dl_last_tap[0] = 0; dl_last_dsk[0] = 0; dl_skipped = 0;
    for (i = 0; i < nf; ++i) {
        unsigned long got;
        char dst[64];
        unsigned char ln = (unsigned char)strlen(files[i].name);
        if (ln > 30) { dl_error = "nom de fichier trop long"; return done; }
        if (ln > 4 && (!strcmp(files[i].name + ln - 4, ".zip") || !strcmp(files[i].name + ln - 4, ".ZIP"))) { ++dl_skipped; continue; }
        dst[0] = 0;
        if (dl_dir[0]) { strcpy(dst, dl_dir); strcat(dst, "/"); }
        strcat(dst, files[i].name);
        strcpy(path, "/files/"); strcat(path, id); strcat(path, "/"); num(path + strlen(path), i);
        if (progress) progress(files[i].name, 0);
        /* Réception en flux par tranches de 32 Ko (Range: bytes=a-b, réponse 206) : chaque
         * réponse tient dans les tampons du modem (le PicoWiFi émulé de Phosphoric jette au-delà
         * de 64 Ko sans contrôle de flux) et une coupure ne coûte que la tranche en cours
         * (reprise en ajout, 3 essais par tranche). Un serveur sans Range répond 200 : on
         * prend alors le fichier entier d'un coup. */
        {
            unsigned long total = 0, expect = 0xFFFFFFFFUL;
            unsigned char tries = 0, ok = 0;
            cur_fd = loci_open(dst, LOCI_O_WRONLY | LOCI_O_CREAT | LOCI_O_TRUNC);
            if (cur_fd < 0) { dl_error = "creation du fichier impossible"; return done; }
            for (;;) {
                char range[32];
                strcpy(range, "bytes="); ulnum(range + 6, total); strcat(range, "-"); ulnum(range + strlen(range), total + DL_CHUNK - 1);
                if (http_get_stream(path, range, sink, &got)) {
                    if (http_status == 206 || (http_status == 200 && total == 0)) {
                        if (http_status == 200) expect = http_length;                       /* pas de Range : fichier entier */
                        else if (expect == 0xFFFFFFFFUL) expect = http_range_total;          /* taille totale : Content-Range a-b/total */
                        total += got;
                        if (http_status == 200 || total >= expect) { ok = total >= expect || expect == 0xFFFFFFFFUL; break; }
                        if (got == 0) { if (++tries > 3) { dl_error = "fichier incomplet"; break; } }
                        else tries = 0;
                        if (progress) progress(files[i].name, total);
                        continue;
                    }
                    if (http_status == 416) { ok = expect != 0xFFFFFFFFUL && total >= expect; break; }
                    dl_error = http_status == 404 ? "fichier introuvable" : "telechargement refuse"; break;
                }
                total += got;                                                               /* octets écrits avant la coupure */
                if (++tries > 3) { dl_error = http_error ? http_error : "fichier incomplet"; break; }
                if (progress) progress(files[i].name, total);
                loci_close((unsigned char)cur_fd);
                cur_fd = loci_open(dst, LOCI_O_WRONLY | LOCI_O_APPEND);
                if (cur_fd < 0) { dl_error = "reprise impossible"; break; }
            }
            loci_close((unsigned char)cur_fd);
            if (!ok) { if (!dl_error) dl_error = "fichier incomplet"; return done; }
            got = total;
        }
        if (progress) progress(files[i].name, got);
        if (ln > 4 && (!strcmp(files[i].name + ln - 4, ".tap") || !strcmp(files[i].name + ln - 4, ".TAP")) && !dl_last_tap[0]) strcpy(dl_last_tap, dst);
        if (ln > 4 && (!strcmp(files[i].name + ln - 4, ".dsk") || !strcmp(files[i].name + ln - 4, ".DSK")) && !dl_last_dsk[0]) strcpy(dl_last_dsk, dst);
        ++done;
    }
    return done;
}
