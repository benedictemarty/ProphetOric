/* Faux port série + faux modem Hayes réactif pour les tests hôte (inspiré de
 * tests/test_atmodem.c d'OricTel) : ATZ → OK, ATI → CONNECTED TO WIFI, ATD → CONNECT
 * puis la réponse HTTP préparée par le test ; +++ → OK ; ATH → NO CARRIER. */
#include <string.h>
#include "fake_serial.h"

static unsigned char rx_q[8192];
static int rx_head, rx_len;
static char tx_cap[1024];
static int tx_len;
static const char *http_reply;
static char line[128];
static int lp;
static unsigned char last;

void fake_reset(const char *reply) { rx_head = rx_len = tx_len = lp = 0; http_reply = reply; tx_cap[0] = 0; }
const char *fake_tx(void) { tx_cap[tx_len] = 0; return tx_cap; }
static void rx_push(const char *s) { while (*s && rx_len < (int)sizeof rx_q) rx_q[rx_len++] = (unsigned char)*s++; }

unsigned char serial_poll(void) { return rx_head < rx_len; }
unsigned char serial_recv(void) { return rx_head < rx_len ? rx_q[rx_head++] : 0xFF; }
void serial_tx_flush(void) {}
void serial_send(unsigned char b)
{
    if (tx_len < (int)sizeof tx_cap - 1) tx_cap[tx_len++] = (char)b;
    if (b == '+' && lp >= 2 && line[lp - 1] == '+' && line[lp - 2] == '+') { rx_push("\r\nOK\r\n"); lp = 0; return; }
    if (b == 0x0A && last == 0x0D) { last = b; return; }      /* LF du CR LF : pas une ligne */
    last = b;
    if (b != 0x0D && b != 0x0A) { if (lp < (int)sizeof line - 1) line[lp++] = (char)b; return; }
    line[lp] = 0; lp = 0;
    if (!strcmp(line, "ATZ")) rx_push("\r\nOK\r\n");
    else if (!strcmp(line, "ATI")) rx_push("\r\nCONNECTED TO WIFI\r\nOK\r\n");
    else if (!strncmp(line, "ATD", 3)) { rx_push("\r\nCONNECT\r\n"); }
    else if (!strcmp(line, "ATH")) rx_push("\r\nNO CARRIER\r\n");
    else if (!strcmp(line, "") && http_reply) { rx_push(http_reply); http_reply = 0; }   /* ligne vide = fin de la requête */
}
