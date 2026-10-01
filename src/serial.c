/* ProphetOric : fichier repris tel quel d'OricTel (~/orictel, bmarty, EUPL-1.2), voir docs/CADRAGE.md */
/**
 * @file serial.c
 * @brief Couche serie OricTel : renvoi vers le driver 6551 assembleur.
 *
 * L'ACIA 6551 est a la base LOCI ($0380). Une seule interface materielle est
 * supportee, celle du LOCI ($0380), partagee par le materiel LOCI reel et le
 * PicoWiFiModemUSB (emule ou reel). serial_init() retient la base et chaque
 * appel serial_*() est transmis tel quel au driver assembleur acia6551_*.
 * La couche reste fine : un simple renvoi, sans surcout mesurable devant
 * vtx_process() qui domine la boucle de drainage RX.
 *
 * serial_probe() est en C : une seule execution, hors de tout chemin
 * critique, et sa logique (sonde du miroir VIA) se teste sur l'hote via les
 * macros REG_RD/REG_WR (tests/test_serial_probe.c).
 */

#include "serial.h"

/* Driver bas niveau (assembleur, serial_asm.s) */
void __fastcall__ acia6551_init(unsigned acia_base);
unsigned char __fastcall__ acia6551_send_raw(unsigned char byte);
unsigned char __fastcall__ acia6551_tx_ready(void);
unsigned char __fastcall__ acia6551_recv(void);
unsigned char __fastcall__ acia6551_poll(void);
unsigned char __fastcall__ acia6551_dcd(void);

/* Acces registre pour la sonde. Sur cible : lecture/ecriture absolue
 * volatile. Sur l'hote (TEST_HOST) : redirige vers un faux bus fourni par
 * le test, qui joue soit un 6551, soit le miroir VIA. */
#ifdef TEST_HOST
unsigned char test_bus_read(unsigned addr);
void          test_bus_write(unsigned addr, unsigned char value);
#define REG_RD(a)    test_bus_read(a)
#define REG_WR(a, v) test_bus_write((a), (v))
#define IRQ_OFF()    ((void)0)
#define IRQ_ON()     ((void)0)
#else
#define REG_RD(a)    (*(volatile unsigned char*)(a))
#define REG_WR(a, v) (*(volatile unsigned char*)(a) = (v))
#define IRQ_OFF()    __asm__("sei")
#define IRQ_ON()     __asm__("cli")
#endif

static unsigned s_base;     /* base retenue par serial_init (pour DSR) */

/* Anneau de réception logiciel (S2, Sedoric sans LOCI) : le 6551 n'a qu'UN octet de
 * réception. Avec le LOCI, l'anneau de 32 octets du firmware absorbe l'écho du modem
 * pendant que l'Oric émet une commande AT ; sans LOCI, le 2e octet d'écho écrasait le 1er
 * (OVERRUN, docs/SPIKE-SEDORIC.md). serial_rx_grab() range l'octet reçu pendant les
 * attentes d'émission (serial_tx.c) ; serial_poll/serial_recv vident l'anneau d'abord. */
#define RX_RING 64                  /* puissance de 2 ; serial_irq.s range aussi dedans (RXMASK) */
unsigned char serial_rx_ring[RX_RING];
unsigned char serial_rx_head, serial_rx_tail;
#define rx_ring serial_rx_ring
#define rx_head serial_rx_head
#define rx_tail serial_rx_tail

void serial_rx_grab(void)
{
#ifdef SERIAL_NO_FIFO                 /* avec le LOCI (anneau de 32 octets) : rien — faire passer des
                                         octets par cet anneau ralentissait chaque lecture du
                                         téléchargement (anneau LOCI saturé, baud_ring32 cassé) */
    unsigned char next;
    if (!acia6551_poll()) return;
    __asm__("php");                   /* serial_irq.s peut aussi ranger dans l'anneau */
    __asm__("sei");
    next = (rx_tail + 1) & (RX_RING - 1);
    if (next != rx_head && acia6551_poll()) {   /* plein : l'octet reste dans l'ACIA */
        rx_ring[rx_tail] = acia6551_recv();
        rx_tail = next;
    }
    __asm__("plp");
#endif
}

unsigned char __fastcall__ serial_probe(unsigned acia_base)
{
    unsigned      st = acia_base + 1;   /* 6551 STATUS / miroir VIA ORA */
    unsigned char saved;
    unsigned char r1, r2;

    IRQ_OFF();
    saved = REG_RD(st);
    REG_WR(st, 0x55);
    r1 = REG_RD(st);
    REG_WR(st, 0xAA);
    r2 = REG_RD(st);
    REG_WR(st, saved);      /* ORA restaure (sur 6551 : 3e reset, inoffensif) */
    IRQ_ON();

    /* Les deux valeurs relues telles quelles = registre memorisant (ORA du
     * VIA, ou RAM) : pas de 6551 ici. */
    if (r1 == 0x55 && r2 == 0xAA) {
        return 0;
    }
    return 1;
}

void __fastcall__ serial_init(unsigned acia_base)
{
    s_base = acia_base;
    acia6551_init(acia_base);
}

unsigned char __fastcall__ serial_modem_absent(void)
{
    return REG_RD(s_base + 1) & ACIA_NOT_DSR;
}

/* Attend au plus ~ms millisecondes qu'un octet arrive ; rend 1 dès qu'il y en a un. Pas de
 * 0,1 ms (lecture ~45 cycles + boucle) : sans LOCI, l'ACIA n'a qu'un octet de réception et un
 * octet arrive toutes les 1,04 ms à 9600 bauds — un sommeil de 1 à 2 ms entre deux lectures
 * (at_wait_response, rx_byte) en perdait (OVERRUN, spike Sedoric S2). */
unsigned char serial_wait(unsigned int ms)
{
    unsigned int i; unsigned char k;
    for (i = 0; i < ms; ++i)
        for (k = 0; k < 10; ++k) {
            if (serial_poll()) return 1;
            __asm__("ldx #$08");
            __asm__("_swt_lp: dex");
            __asm__("bne _swt_lp");
        }
    return serial_poll();
}

unsigned char __fastcall__ serial_poll(void)
{
    if (rx_head != rx_tail) return 1;
    return acia6551_poll();
}

unsigned char __fastcall__ serial_recv(void)
{
    unsigned char b;
    if (rx_head == rx_tail) return acia6551_recv();
    b = rx_ring[rx_head];             /* sans LOCI, l'IRQ de réception (serial_irq.s) remplit l'anneau */
    rx_head = (rx_head + 1) & (RX_RING - 1);
    return b;
}

unsigned char __fastcall__ serial_tx_ready(void)
{
    return acia6551_tx_ready();
}

void __fastcall__ serial_send_raw(unsigned char byte)
{
    acia6551_send_raw(byte);
}

unsigned char __fastcall__ serial_dcd(void)
{
    return acia6551_dcd();
}
