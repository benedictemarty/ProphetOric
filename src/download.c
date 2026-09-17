/* download.c — /files/<id> (format cli : n:"nom","chemin/",…) puis chaque
 * /files/<id>/<n> en flux (http_get_stream) écrit sur le LOCI par blocs de 128 octets. */
#include <string.h>
#include "download.h"
#include "http.h"
#include "cli.h"
#include "loci.h"

char dl_dir[32] = "";
const char *dl_error;
char dl_last_tap[64] = "";
unsigned char dl_skipped;
static int cur_fd = -1;
static char list[512];
static struct cli_file files[CLI_MAX_FILES];

static unsigned char sink(const unsigned char *block, unsigned char n)
{
    return loci_write((unsigned char)cur_fd, block, n) == (int)n;
}

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
    dl_last_tap[0] = 0; dl_skipped = 0;
    for (i = 0; i < nf; ++i) {
        unsigned long got;
        char dst[64];
        unsigned char ln = (unsigned char)strlen(files[i].name);
        if (ln > 30) { dl_error = "nom de fichier trop long"; return done; }
        if (ln > 4 && (!strcmp(files[i].name + ln - 4, ".zip") || !strcmp(files[i].name + ln - 4, ".ZIP"))) { ++dl_skipped; continue; }
        dst[0] = 0;
        if (dl_dir[0]) { strcpy(dst, dl_dir); strcat(dst, "/"); }
        strcat(dst, files[i].name);
        cur_fd = loci_open(dst, LOCI_O_WRONLY | LOCI_O_CREAT | LOCI_O_TRUNC);
        if (cur_fd < 0) { dl_error = "creation du fichier impossible"; return done; }
        strcpy(path, "/files/"); strcat(path, id); strcat(path, "/"); num(path + strlen(path), i);
        if (progress) progress(files[i].name, 0);
        if (!http_get_stream(path, 0, sink, &got) || http_status != 200) {
            loci_close((unsigned char)cur_fd);
            dl_error = http_error ? http_error : "telechargement refuse";
            return done;
        }
        loci_close((unsigned char)cur_fd);
        if (http_length != 0xFFFFFFFFUL && got != http_length) { dl_error = "fichier incomplet"; return done; }
        if (progress) progress(files[i].name, got);
        if (ln > 4 && (!strcmp(files[i].name + ln - 4, ".tap") || !strcmp(files[i].name + ln - 4, ".TAP")) && !dl_last_tap[0]) strcpy(dl_last_tap, dst);
        ++done;
    }
    return done;
}
