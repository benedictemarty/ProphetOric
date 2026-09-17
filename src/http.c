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

static unsigned char modem_ready;          /* ATZ fait une fois (un ATZ relance l'association Wi-Fi du PicoWiFi) */

static unsigned char connect_modem(void)
{
    unsigned char tries;
    if (!modem_ready) {
        at_send("ATZ");
        if (!at_wait_response("OK", 3000)) {
            at_hangup();                          /* modem resté en ligne (OricTel) */
            at_send("ATZ");
            if (!at_wait_response("OK", 3000)) { http_error = "pas de modem (ATZ)"; return 0; }
        }
        modem_ready = 1;
    }
    /* Pas d'attente ATI « CONNECTED TO WIFI » (OricTel) : l'émulation répond « WiFi: x UP »
     * et l'attente durait 15 s par requête. On compose, et on réessaie si le Wi-Fi n'est pas
     * encore associé (NO CARRIER). PicoWiFiModemUSB : '-' = pas de telnet (sinon CR → CR NUL,
     * requête refusée 400), '#' = TLS terminé par le modem (port 443 : firmware ≥ 0.2.0,
     * émulé par Phosphoric ; non vérifié sur matériel). */
    for (tries = 0; tries < 3; ++tries) {
        tx(strcmp(http_port, "443") == 0 ? "ATD-#" : "ATD-"); tx(http_host); tx(":"); tx(http_port); serial_send(0x0D); serial_tx_flush();
        if (at_wait_response("CONNECT", 20000)) return 1;
        delay_ms(3000);
    }
    http_error = "connexion refusee (ATD)";
    return 0;
}

/* fin de réponse : le serveur ferme (Connection: close) → le modem repasse en mode
 * commande et émet NO CARRIER ; sinon (réponse tronquée, serveur muet) échappement +++/ATH. */
static void finish(void)
{
    if (!at_wait_response("NO CARRIER", 3000)) at_hangup();
}

/* connexion + requête + en-têtes ; retour 1 si les en-têtes sont valides (http_status posé) */
static unsigned char request(const char *path, const char *range)
{
    unsigned int n = 0;
    unsigned char b, state = 0;

    http_status = 0; http_error = 0;
    if (!connect_modem()) return 0;

    tx("GET "); tx(path); tx(" HTTP/1.1\r\nHost: "); tx(http_host);
    tx("\r\nResponseFormat: cli\r\nConnection: close\r\nUser-Agent: ProphetOric/0.2\r\n");
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
    return 1;
}

unsigned char http_get(const char *path, const char *range, char *buf, unsigned int max, unsigned int *len)
{
    unsigned int n = 0;
    unsigned char b;
    unsigned long want;

    *len = 0;
    if (!request(path, range)) return 0;
    want = http_length;
    while (n < max - 1 && (want == 0xFFFFFFFFUL || n < want)) {
        if (!rx_byte(&b, RX_IDLE_TIMEOUT_MS)) break;        /* fin par silence (sans Content-Length) */
        buf[n++] = (char)b;
    }
    buf[n] = 0;
    *len = n;
    finish();
    return 1;
}

unsigned char http_get_stream(const char *path, const char *range, http_sink sink, unsigned long *len)
{
    static unsigned char block[128];
    unsigned char b, k = 0;
    unsigned long n = 0, want;

    *len = 0;
    if (!request(path, range)) return 0;
    want = http_length;
    while (want == 0xFFFFFFFFUL || n < want) {
        if (!rx_byte(&b, RX_IDLE_TIMEOUT_MS)) break;
        block[k++] = b; ++n;
        if (k == sizeof block) { if (!sink(block, k)) { http_error = "ecriture impossible"; at_hangup(); return 0; } k = 0; }
    }
    if (k && !sink(block, k)) { http_error = "ecriture impossible"; at_hangup(); return 0; }
    *len = n;
    finish();
    return 1;
}
