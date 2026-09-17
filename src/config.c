#include <string.h>
#include "config.h"
#include "http.h"
#include "download.h"
#include "loci.h"

#define CFG_NAME "PROPHET.CFG"

unsigned char config_load(void)
{
    static char raw[128];
    int fd, n; unsigned char line = 0; char *p, *q;
    if (!loci_present()) return 0;
    fd = loci_open(CFG_NAME, LOCI_O_RDONLY);
    if (fd < 0) return 0;
    n = loci_read((unsigned char)fd, (unsigned char *)raw, sizeof raw - 1);
    loci_close((unsigned char)fd);
    if (n <= 0) return 0;
    raw[n] = 0;
    for (p = raw; *p && line < 4; ++line) {
        q = strchr(p, '\n');
        if (q) *q++ = 0; else q = p + strlen(p);
        if (*p && p[strlen(p) - 1] == '\r') p[strlen(p) - 1] = 0;
        switch (line) {
        case 0: if (*p && strlen(p) < sizeof http_host) strcpy(http_host, p); break;
        case 1: if (*p && strlen(p) < sizeof http_port) strcpy(http_port, p); break;
        case 2: if (strlen(p) < sizeof dl_dir) strcpy(dl_dir, p); break;
        case 3: if (strlen(p) < sizeof http_pass) strcpy(http_pass, p); break;
        }
        p = q;
    }
    return line >= 2;
}

unsigned char config_save(void)
{
    char raw[128]; int fd, n;
    if (!loci_present()) return 0;
    strcpy(raw, http_host); strcat(raw, "\n"); strcat(raw, http_port); strcat(raw, "\n");
    strcat(raw, dl_dir); strcat(raw, "\n"); strcat(raw, http_pass); strcat(raw, "\n");
    fd = loci_open(CFG_NAME, LOCI_O_WRONLY | LOCI_O_CREAT | LOCI_O_TRUNC);
    if (fd < 0) return 0;
    n = loci_write((unsigned char)fd, (const unsigned char *)raw, (unsigned char)strlen(raw));
    loci_close((unsigned char)fd);
    return n == (int)strlen(raw);
}
