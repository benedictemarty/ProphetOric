/*
 * cli.h — parseur du format texte `cli` du serveur Prophet (le même que
 * consomme prophet.neo). Analyse EN PLACE : le tampon est modifié (fins de
 * champ remplacées par NUL), les résultats pointent dedans.
 *
 *   /list, /search : "total,pages,page:" puis "\x83id\x82 : titre\n\r"...
 *   /cat           : "nom (n)\n\r"...
 *   /crc32/<id>, /requires/<id>, /launch/<id> : une valeur par ligne
 *   /app/<id>      : "Title: \x83T\x82    id: I\n\r\n\r[Author: A\n\r\n\r]
 *                    [Description: D\n\r]Files: n\n\r"
 */
#ifndef CLI_H
#define CLI_H

#define CLI_MAX_ITEMS 64

struct cli_item { char *id; char *title; };

struct cli_listing {
    unsigned int total, pages, page;
    unsigned char count;
    struct cli_item item[CLI_MAX_ITEMS];
};

struct cli_cat { char *name; unsigned int count; };

struct cli_info {
    char *title, *id, *author, *description;
    unsigned int files;
};

#define CLI_MAX_FILES 16
struct cli_file { char *name; char *path; };

/* Retour : 1 si le format est reconnu, 0 sinon. */
unsigned char cli_parse_files(char *buf, struct cli_file *files, unsigned char max, unsigned char *n);
unsigned char cli_parse_listing(char *buf, struct cli_listing *l);
unsigned char cli_parse_cat(char *buf, struct cli_cat *cats, unsigned char max, unsigned char *n);
unsigned char cli_parse_info(char *buf, struct cli_info *i);
/* une valeur par ligne (/crc32, /requires, /launch) : 1 si au moins une ligne */
unsigned char cli_parse_lines(char *buf, char **out, unsigned char max, unsigned char *n);

#endif
