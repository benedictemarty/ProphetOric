/* Tests hôte de http.c : analyse des en-têtes et GET complet sur un faux modem. */
#include <stdio.h>
#include <string.h>
#include "http.h"
#include "fake_serial.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char buf[256]; unsigned int len;
    CHECK(http_parse_headers("HTTP/1.1 200 OK\r\ncontent-length: 42\r\nX: y\r\n\r\n") && http_status == 200 && http_length == 42);
    CHECK(http_parse_headers("HTTP/1.0 404 Not Found\r\n\r\n") && http_status == 404 && http_length == 0xFFFFFFFFUL);
    CHECK(!http_parse_headers("42,1,0:") && http_status == 0);
    CHECK(http_parse_headers("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-7/100\r\nContent-Length: 8\r\n\r\n") && http_status == 206 && http_length == 8 && http_range_total == 100);

    strcpy(http_host, "127.0.0.1"); strcpy(http_port, "18994"); http_pass[0] = 0;
    fake_reset("HTTP/1.1 200 OK\r\nContent-Length: 22\r\nConnection: close\r\n\r\ngames (3)\n\rtools (1)\n\r");
    CHECK(http_get("/cat?platform=oric", 0, buf, sizeof buf, &len) == 1);
    CHECK(http_status == 200 && len == 22 && !strcmp(buf, "games (3)\n\rtools (1)\n\r"));
    CHECK(strstr(fake_tx(), "ATD-127.0.0.1:18994\r") != 0);
    CHECK(strstr(fake_tx(), "GET /cat?platform=oric HTTP/1.1\r\nHost: 127.0.0.1\r\nResponseFormat: cli\r\nConnection: close\r\n") != 0);
    CHECK(strstr(fake_tx(), "X-Prophet-Password") == 0 && strstr(fake_tx(), "Range:") == 0);
    CHECK(strstr(fake_tx(), "+++") != 0 && strstr(fake_tx(), "ATH\r") != 0);   /* raccroche après la réponse */

    strcpy(http_pass, "sesame-test");
    fake_reset("HTTP/1.1 206 Partial Content\r\nContent-Length: 4\r\n\r\nABCDEFGH");   /* corps plus long que Content-Length */
    CHECK(http_get("/files/x/0", "bytes=0-3", buf, sizeof buf, &len) == 1 && http_status == 206 && len == 4 && !strcmp(buf, "ABCD"));
    CHECK(strstr(fake_tx(), "Range: bytes=0-3\r\n") != 0 && strstr(fake_tx(), "X-Prophet-Password: sesame-test\r\n") != 0);

    strcpy(http_port, "443");
    fake_reset("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    CHECK(http_get("/cat", 0, buf, sizeof buf, &len) == 1 && strstr(fake_tx(), "ATD-#127.0.0.1:443\r") != 0);   /* TLS par le modem */
    strcpy(http_port, "18994");
    fake_reset("HTTP/1.1 404 Not Found\r\nContent-Length: 17\r\n\r\n404 - not found\n\r");
    CHECK(http_get("/app/nope", 0, buf, sizeof buf, &len) == 1 && http_status == 404 && len == 17);

    fake_reset("pas du http\r\n\r\n");
    CHECK(http_get("/cat", 0, buf, sizeof buf, &len) == 0 && http_error != 0);

    fake_reset("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n0123456789");   /* tampon plus petit que le corps */
    CHECK(http_get("/x", 0, buf, 6, &len) == 1 && len == 5 && !strcmp(buf, "01234"));

    printf("%s : %d échec(s)\n", __FILE__, fails);
    return fails != 0;
}
