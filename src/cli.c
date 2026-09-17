/* cli.c — parseur du format texte du serveur Prophet (voir cli.h) */
#include "cli.h"

#define HI_ON  '\x83'
#define HI_OFF '\x82'

static char *skip_eol(char *p)
{
    while (*p == '\n' || *p == '\r') p++;
    return p;
}

static char *cut_eol(char *p)   /* termine la ligne à \n ou \r, renvoie le début de la suivante */
{
    while (*p && *p != '\n' && *p != '\r') p++;
    if (*p) { *p++ = 0; p = skip_eol(p); }
    return p;
}

static unsigned char parse_uint(char **pp, unsigned int *v)
{
    char *p = *pp;
    unsigned char ok = 0;
    *v = 0;
    while (*p >= '0' && *p <= '9') { *v = *v * 10 + (*p - '0'); p++; ok = 1; }
    *pp = p;
    return ok;
}

unsigned char cli_parse_listing(char *buf, struct cli_listing *l)
{
    char *p = buf;
    l->count = 0;
    if (!parse_uint(&p, &l->total) || *p++ != ',') return 0;
    if (!parse_uint(&p, &l->pages) || *p++ != ',') return 0;
    if (!parse_uint(&p, &l->page)  || *p++ != ':') return 0;
    while (*p == HI_ON && l->count < CLI_MAX_ITEMS) {
        p++;
        l->item[l->count].id = p;
        while (*p && *p != HI_OFF) p++;
        if (!*p) return 0;
        *p++ = 0;
        if (p[0] == ' ' && p[1] == ':' && p[2] == ' ') p += 3;
        l->item[l->count].title = p;
        p = cut_eol(p);
        l->count++;
    }
    return 1;
}

unsigned char cli_parse_cat(char *buf, struct cli_cat *cats, unsigned char max, unsigned char *n)
{
    char *p = buf, *q;
    *n = 0;
    while (*p && *n < max) {
        cats[*n].name = p;
        q = p;
        while (*q && *q != '\n' && *q != '\r') q++;
        /* remonte jusqu'à " (" */
        {
            char *e = q;
            while (e > p && !(e[-1] == '(' && e[-2] == ' ')) e--;
            if (e == p) return 0;
            e[-2] = 0;
            if (!parse_uint(&e, &cats[*n].count)) return 0;
        }
        p = cut_eol(q);
        (*n)++;
    }
    return *n > 0;
}

static unsigned char starts(const char *p, const char *k)
{
    while (*k) if (*p++ != *k++) return 0;
    return 1;
}

unsigned char cli_parse_info(char *buf, struct cli_info *i)
{
    char *p = buf;
    i->title = i->id = i->author = i->description = 0;
    i->files = 0;
    if (!starts(p, "Title: ")) return 0;
    p += 7;
    if (*p == HI_ON) p++;
    i->title = p;
    while (*p && *p != HI_OFF) p++;
    if (!*p) return 0;
    *p++ = 0;
    while (*p == ' ') p++;
    if (!starts(p, "id: ")) return 0;
    p += 4;
    i->id = p;
    p = cut_eol(p);
    while (*p) {
        if (starts(p, "Author: ")) { i->author = p + 8; p = cut_eol(p); }
        else if (starts(p, "Description: ")) { i->description = p + 13; p = cut_eol(p); }
        else if (starts(p, "Files: ")) { p += 7; parse_uint(&p, &i->files); p = cut_eol(p); }
        else p = cut_eol(p);
    }
    return 1;
}

/* /files/<id> : n:"nom","chemin/","nom2","chemin2/",... */
static char *quoted(char *p, char **out)
{
    if (*p++ != '"') return 0;
    *out = p;
    while (*p && *p != '"') p++;
    if (!*p) return 0;
    *p++ = 0;
    if (*p == ',') p++;
    return p;
}

unsigned char cli_parse_files(char *buf, struct cli_file *files, unsigned char max, unsigned char *n)
{
    char *p = buf;
    unsigned int count;
    *n = 0;
    if (!parse_uint(&p, &count) || *p++ != ':') return 0;
    while (*n < count && *n < max) {
        if (!(p = quoted(p, &files[*n].name))) return 0;
        if (!(p = quoted(p, &files[*n].path))) return 0;
        (*n)++;
    }
    return 1;
}
