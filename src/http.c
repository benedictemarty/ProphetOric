/*
 * http.c — GET HTTP/1.1 sur modem Hayes (voir http.h). La réception suit la
 * règle d'OricTel : drainer l'ACIA sans rien afficher (RX 1 octet, pas de
 * FIFO) ; l'affichage se fait après, sur le tampon.
 */
#include <string.h>
#include "http.h"
#include "version.h"
#include "serial.h"
#include "at_modem.h"

char http_host[40] = PROPHET_HOST;
char http_port[6]  = PROPHET_PORT;
char http_pass[32] = "";
unsigned int  http_status;
unsigned long http_length;
unsigned long http_range_total;
void (*http_tick)(unsigned long received);
const char   *http_error;

#define RX_IDLE_TIMEOUT_MS 8000     /* silence maximal entre deux octets */

/* Analyse des en-têtes AU FIL DES OCTETS : le 6551 n'a qu'un octet de réception (le
 * firmware LOCI y ajoute un anneau de 32 octets) et le corps suit les en-têtes sans
 * pause — analyser un bloc d'en-têtes après coup (100 ms) perdait les 32 premiers
 * octets du corps à 9600 bauds. Ici chaque octet coûte quelques dizaines de cycles. */
static char hline[40];               /* ligne courante, en minuscules, tronquée */
static unsigned char hl, hfirst, hdone, hvalid;

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

static void hdr_reset(void)
{
    hl = 0; hfirst = 1; hdone = 0; hvalid = 0;
    http_status = 0; http_length = 0xFFFFFFFFUL; http_range_total = 0xFFFFFFFFUL;
}

static void hdr_line_done(void)
{
    hline[hl] = 0;
    if (hfirst) {
        hfirst = 0;
        if (hline[0] == 'h' && hline[1] == 't' && hline[2] == 't' && hline[3] == 'p' && hline[4] == '/' && hline[8] == ' ') {
            http_status = (unsigned int)parse_dec(hline + 9);
            hvalid = http_status >= 100 && http_status <= 599;
            if (!hvalid) http_status = 0;
        }
    } else if (!strncmp(hline, "content-length:", 15)) {
        const char *v = hline + 15;
        while (*v == ' ') ++v;
        http_length = parse_dec(v);
    } else if (!strncmp(hline, "content-range:", 14)) {          /* bytes a-b/total */
        const char *v = hline + 14;
        while (*v && *v != '/') ++v;
        if (*v == '/') http_range_total = parse_dec(v + 1);
    }
    hl = 0;
}

/* un octet d'en-tête ; retour 1 quand la ligne vide (fin des en-têtes) est passée */
static unsigned char hdr_feed(unsigned char b)
{
    if (b == '\r') return 0;
    if (b == '\n') {
        if (hl == 0 && !hfirst) { hdone = 1; return 1; }
        if (hl == 0 && hfirst) return 0;          /* CR LF résiduel avant la ligne de statut */
        hdr_line_done();
        return 0;
    }
    if (b >= 'A' && b <= 'Z') b += 32;
    if (hl < sizeof hline - 1) hline[hl++] = (char)b;
    return 0;
}

unsigned char http_parse_headers(const char *h)
{
    hdr_reset();
    while (*h && !hdr_feed((unsigned char)*h)) ++h;
    if (!hdone && hl) hdr_line_done();
    return hvalid;
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
    unsigned char b;

    http_status = 0; http_error = 0;
    if (!connect_modem()) return 0;

    tx("GET "); tx(path); tx(" HTTP/1.1\r\nHost: "); tx(http_host);
    tx("\r\nResponseFormat: cli\r\nConnection: close\r\nUser-Agent: ProphetOric/" VERSION "\r\n");
    if (range) { tx("Range: "); tx(range); tx("\r\n"); }
    if (http_pass[0]) { tx("X-Prophet-Password: "); tx(http_pass); tx("\r\n"); }
    tx("\r\n");
    serial_tx_flush();

    hdr_reset();
    for (;;) {
        if (!rx_byte(&b, RX_IDLE_TIMEOUT_MS)) { http_error = "pas de reponse"; at_hangup(); return 0; }
        if (hdr_feed(b)) break;
    }
    if (!hvalid) { http_error = "reponse HTTP invalide"; at_hangup(); return 0; }
    return 1;
}

unsigned char http_get(const char *path, const char *range, char *buf, unsigned int max, unsigned int *len)
{
    unsigned int n = 0, room = max - 1;
    unsigned char b, k, chunk;
    unsigned long remaining;

    *len = 0;
    if (!request(path, range)) return 0;
    remaining = http_length;
    /* même discipline que http_get_stream : blocs de ≤ 128 octets en 8 bits (l'anneau de
     * 32 octets du LOCI débordait à 9600 bauds avec une comparaison 32 bits par octet) */
    while (remaining && room) {
        chunk = remaining > 128 ? 128 : (unsigned char)remaining;
        if (chunk > room) chunk = (unsigned char)room;
        for (k = 0; k < chunk; ) {
            if (serial_poll()) { buf[n + k++] = (char)serial_recv(); continue; }
            if (!rx_byte(&b, RX_IDLE_TIMEOUT_MS)) { chunk = k; remaining = chunk; break; }   /* silence : fin */
            buf[n + k++] = (char)b;
        }
        n += chunk; room -= chunk; remaining -= chunk;
    }
    buf[n] = 0;
    *len = n;
    finish();
    return 1;
}

/* Coupure du serveur en cours de corps : le modem (mode sans telnet) signale la perte de
 * porteuse par « \r\nNO CARRIER (hh:mm:ss)\r\n » (durée : PicoWiFi émulé de Phosphoric)
 * sur la même ligne série — ces octets ne sont PAS du fichier. Renvoie le nombre d'octets
 * parasites à la fin de (prev + blk[0..n)). */
static unsigned char prev[16];
static unsigned char junk_len(const unsigned char *blk, unsigned char n)
{
    unsigned char w[48], m, i0, j;                /* 16 octets du bloc précédent + 32 du bloc courant */
    i0 = n > 32 ? n - 32 : 0;
    memcpy(w, prev, 16); memcpy(w + 16, blk + i0, n - i0); m = 16 + n - i0;
    j = m;
    while (j && (w[j - 1] == '\r' || w[j - 1] == '\n')) --j;
    if (j && w[j - 1] == ')') {                   /* « (hh:mm:ss) » facultatif */
        unsigned char q = j;
        while (q && w[q - 1] != '(' && j - q < 14) --q;
        if (q > 1 && w[q - 1] == '(' && w[q - 2] == ' ') j = q - 2;
    }
    if (j < 10 || memcmp(w + j - 10, "NO CARRIER", 10)) return 0;
    j -= 10;
    while (j && (w[j - 1] == '\r' || w[j - 1] == '\n')) --j;
    return m - j;
}

unsigned char http_get_stream(const char *path, const char *range, http_sink sink, unsigned long *len)
{
    static unsigned char block[128];
    unsigned char b, k, chunk, silent;
    unsigned long remaining;

    *len = 0;
    memset(prev, 0, sizeof prev);
    if (!request(path, range)) return 0;
    remaining = http_length;                      /* 0xFFFFFFFF : longueur inconnue, fin par silence */
    while (remaining) {
        chunk = remaining > 128 ? 128 : (unsigned char)remaining;
        silent = 0;
        /* boucle serrée : pas d'arithmétique 32 bits par octet (cc65 : > 1000 cycles, le
         * FIFO de 32 octets du LOCI débordait à 9600 bauds) ; chemin lent seulement si vide */
        for (k = 0; k < chunk; ) {
            if (serial_poll()) { block[k++] = serial_recv(); continue; }
            if (!rx_byte(&b, RX_IDLE_TIMEOUT_MS)) { chunk = k; silent = 1; break; }   /* silence : fin */
            block[k++] = b;
        }
        if (silent) {                             /* fin prématurée : retirer le NO CARRIER du modem */
            if (http_length != 0xFFFFFFFFUL) {
                unsigned char j = junk_len(block, chunk);
                if (j <= chunk) chunk -= j; else { *len -= j - chunk; chunk = 0; }   /* déjà écrit : la reprise se repositionne */
            }
            remaining = chunk;
        }
        if (chunk && !sink(block, chunk)) { http_error = "ecriture impossible"; at_hangup(); return 0; }
        if (chunk == 128) memcpy(prev, block + 112, 16);
        *len += chunk;
        remaining -= chunk;
        if (http_tick) http_tick(*len);
    }
    finish();
    return 1;
}
