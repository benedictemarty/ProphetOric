/*
 * http.c — GET HTTP/1.1 sur modem Hayes (voir http.h). La réception suit la
 * règle d'OricTel : drainer l'ACIA sans rien afficher (RX 1 octet, pas de
 * FIFO) ; l'affichage se fait après, sur le tampon.
 */
#include <string.h>
#include "http.h"
#include "serial.h"
#include "at_modem.h"

char http_host[40] = PROPHET_HOST;
char http_port[6]  = PROPHET_PORT;
char http_pass[32] = "";
unsigned int  http_status;
unsigned long http_length;
const char   *http_error;

#define HDR_MAX 512
#define RX_IDLE_TIMEOUT_MS 8000     /* silence maximal entre deux octets */

static char hdr[HDR_MAX];

#ifdef TEST_HOST
static void delay_ms(unsigned int ms) { (void)ms; }
#else
static void delay_ms(unsigned int ms)
{
    unsigned int i;
    for (i = 0; i < ms; ++i) {
        __asm__("ldx #$C8");
        __asm__("_hdl_lp: dex");
        __asm__("bne _hdl_lp");
    }
}
#endif

/* Attend un octet, au plus timeout_ms de silence. Retour 1 si reçu. */
static unsigned char rx_byte(unsigned char *b, unsigned int timeout_ms)
{
    unsigned int elapsed = 0;
    while (!serial_poll()) {
        if (elapsed >= timeout_ms) return 0;
        delay_ms(1);
        ++elapsed;
    }
    *b = serial_recv();
    return 1;
}

static unsigned long parse_dec(const char *p)
{
    unsigned long v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
    return v;
}

static unsigned char ieq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return a == b;
}

/* Cherche un en-tête (insensible à la casse) en début de ligne ; renvoie sa valeur. */
static const char *header_value(const char *h, const char *name)
{
    const char *p = h;
    unsigned char n = (unsigned char)strlen(name);
    while (*p) {
        unsigned char i;
        for (i = 0; i < n && p[i] && ieq(p[i], name[i]); ++i) ;
        if (i == n && p[n] == ':') {
            p += n + 1;
            while (*p == ' ') ++p;
            return p;
        }
        while (*p && *p != '\n') ++p;
        if (*p) ++p;
    }
    return 0;
}

unsigned char http_parse_headers(const char *h)
{
    const char *v;
    http_status = 0;
    http_length = 0xFFFFFFFFUL;
    if (strncmp(h, "HTTP/1.", 7) != 0) return 0;
    v = h + 8;
    while (*v == ' ') ++v;
    http_status = (unsigned int)parse_dec(v);
    if (http_status < 100 || http_status > 599) { http_status = 0; return 0; }
    v = header_value(h, "Content-Length");
    if (v) http_length = parse_dec(v);
    return 1;
}

/* Envoie une chaîne sans CR final. */
static void tx(const char *s) { while (*s) serial_send(*s++); }

static unsigned char connect_modem(void)
{
    at_send("ATZ");
    if (!at_wait_response("OK", 3000)) {
        at_hangup();                          /* modem resté en ligne (OricTel) */
        at_send("ATZ");
        if (!at_wait_response("OK", 3000)) { http_error = "pas de modem (ATZ)"; return 0; }
    }
    at_wait_ip(15000);                        /* IP Wi-Fi prête (immédiat sinon) */
    /* PicoWiFiModemUSB : '-' = pas de telnet (sinon CR → CR NUL, requête refusée 400),
     * '#' = TLS terminé par le modem (port 443 : firmware ≥ 0.2.0, émulé par Phosphoric ;
     * non vérifié sur matériel). */
    tx(strcmp(http_port, "443") == 0 ? "ATD-#" : "ATD-"); tx(http_host); tx(":"); tx(http_port); serial_send(0x0D); serial_tx_flush();
    if (!at_wait_response("CONNECT", 20000)) { http_error = "connexion refusee (ATD)"; return 0; }
    return 1;
}

unsigned char http_get(const char *path, const char *range, char *buf, unsigned int max, unsigned int *len)
{
    unsigned int n = 0;
    unsigned char b, state = 0;
    unsigned long want;

    *len = 0; http_status = 0; http_error = 0;
    if (!connect_modem()) return 0;

    tx("GET "); tx(path); tx(" HTTP/1.1\r\nHost: "); tx(http_host);
    tx("\r\nResponseFormat: cli\r\nConnection: close\r\nUser-Agent: ProphetOric/0.1\r\n");
    if (range) { tx("Range: "); tx(range); tx("\r\n"); }
    if (http_pass[0]) { tx("X-Prophet-Password: "); tx(http_pass); tx("\r\n"); }
    tx("\r\n");
    serial_tx_flush();

    /* en-têtes : jusqu'à \r\n\r\n (état 0..4), au plus HDR_MAX-1 octets */
    while (state < 4) {
        if (!rx_byte(&b, RX_IDLE_TIMEOUT_MS)) { http_error = "pas de reponse"; at_hangup(); return 0; }
        if (n == 0 && (b == '\r' || b == '\n')) continue;   /* reliquat du CR LF de CONNECT */
        if (n < HDR_MAX - 1) hdr[n++] = (char)b;
        if (b == '\r')      state = (state == 2) ? 3 : 1;
        else if (b == '\n') state = (state == 1) ? 2 : (state == 3) ? 4 : 0;
        else                state = 0;
    }
    hdr[n] = 0;
    if (!http_parse_headers(hdr)) { http_error = "reponse HTTP invalide"; at_hangup(); return 0; }

    want = http_length;
    n = 0;
    while (n < max - 1 && (want == 0xFFFFFFFFUL || n < want)) {
        if (!rx_byte(&b, RX_IDLE_TIMEOUT_MS)) break;        /* fin par silence (sans Content-Length) */
        buf[n++] = (char)b;
    }
    buf[n] = 0;
    *len = n;
    at_hangup();                              /* le serveur ferme : drain + +++ + ATH */
    return 1;
}
