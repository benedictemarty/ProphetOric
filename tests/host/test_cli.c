/* test_cli.c — tests hôte (gcc) du parseur du format serveur.
 * Les fixtures sont des réponses RÉELLES de prophet.3617.fr (2026-09-16). */
#include <stdio.h>
#include <string.h>
#include "../../src/cli.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char list[] = "42,14,0:\x83" "aerial\x82 : Aerial\n\r\x83" "antiair\x82 : AntiAir\n\r\x83" "ascend\x82 : Ascend\n\r";
    char cat[] = "games (37)\n\rtools (5)\n\rother (1)\n\r";
    char info[] = "Title: \x83Neo-Tetris\x82    id: tetris\n\r\n\rAuthor: Wojciech Bocianski (bocianu@gmail.com)\n\r\n\r"
                  "Description: A colorful Tetris clone written in Mad-Pascal.\n\rFiles: 1\n\r";
    char info2[] = "Title: \x83Power Bricks\x82    id: power_bricks\n\r\n\rFiles: 2\n\r";
    char bad[] = "404 - not found\n\r";
    struct cli_listing l; struct cli_cat c[8]; unsigned char n; struct cli_info i;

    CHECK(cli_parse_listing(list, &l) == 1);
    CHECK(l.total == 42 && l.pages == 14 && l.page == 0 && l.count == 3);
    CHECK(!strcmp(l.item[0].id, "aerial") && !strcmp(l.item[0].title, "Aerial"));
    CHECK(!strcmp(l.item[2].id, "ascend") && !strcmp(l.item[2].title, "Ascend"));

    CHECK(cli_parse_cat(cat, c, 8, &n) == 1 && n == 3);
    CHECK(!strcmp(c[0].name, "games") && c[0].count == 37);
    CHECK(!strcmp(c[2].name, "other") && c[2].count == 1);

    CHECK(cli_parse_info(info, &i) == 1);
    CHECK(!strcmp(i.title, "Neo-Tetris") && !strcmp(i.id, "tetris"));
    CHECK(!strcmp(i.author, "Wojciech Bocianski (bocianu@gmail.com)"));
    CHECK(!strcmp(i.description, "A colorful Tetris clone written in Mad-Pascal.") && i.files == 1);

    CHECK(cli_parse_info(info2, &i) == 1 && i.author == 0 && i.description == 0 && i.files == 2);
    CHECK(cli_parse_listing(bad, &l) == 0);
    CHECK(cli_parse_info(bad, &i) == 0);
    {
        char empty[] = "0,0,0:";
        CHECK(cli_parse_listing(empty, &l) == 1 && l.count == 0 && l.total == 0);
    }
    {
        char files[] = "2:\"pget.neo\",\"prophet/\",\"prophet.neo\",\"prophet/\",";
        char one[] = "1:\"asteroneo.neo\",\"asteroneo/\",";
        char none[] = "0:";
        char badf[] = "2:\"a.neo\",\"x/\"";
        struct cli_file f[4]; unsigned char nf;
        CHECK(cli_parse_files(files, f, 4, &nf) == 1 && nf == 2);
        CHECK(!strcmp(f[0].name, "pget.neo") && !strcmp(f[0].path, "prophet/"));
        CHECK(!strcmp(f[1].name, "prophet.neo") && !strcmp(f[1].path, "prophet/"));
        CHECK(cli_parse_files(one, f, 4, &nf) == 1 && nf == 1 && !strcmp(f[0].name, "asteroneo.neo"));
        CHECK(cli_parse_files(none, f, 4, &nf) == 1 && nf == 0);
        CHECK(cli_parse_files(badf, f, 4, &nf) == 0);
    }
    {   /* réponses réelles de prophetd 0.14 (tests/repo, 2026-09-30) */
        char crc[] = "7eba6c8f\n\r";
        char crc2[] = "7eba6c8f\n\r0badf00d\n\r";
        char req[] = "loci>=0.3.1\n\rpicowifi\n\r";
        char empty[] = "";
        char *v[4]; unsigned char nv;
        CHECK(cli_parse_lines(crc, v, 4, &nv) == 1 && nv == 1 && !strcmp(v[0], "7eba6c8f"));
        CHECK(cli_parse_lines(crc2, v, 4, &nv) == 1 && nv == 2 && !strcmp(v[1], "0badf00d"));
        CHECK(cli_parse_lines(req, v, 1, &nv) == 1 && nv == 1 && !strcmp(v[0], "loci>=0.3.1"));
        CHECK(cli_parse_lines(empty, v, 4, &nv) == 0 && nv == 0);
    }
    printf("%s : %d échec(s)\n", __FILE__, fails);
    return fails != 0;
}
