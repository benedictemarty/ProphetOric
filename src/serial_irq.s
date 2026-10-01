; ===========================================================================
; serial_irq.s - ProphetOric : reception sous IRQ pour un ACIA 6551 SANS tampon
; (build -DSERIAL_NO_FIFO : Oric sans LOCI, ACIA en $031C ; spike S2 Sedoric)
;
; Le 6551 n'a qu'un octet de reception ; a 9600 bauds un octet arrive toutes les
; 1,04 ms. En scrutation, tout traitement C plus long (fin de ligne d'en-tete,
; conversions) l'ecrasait (docs/SPIKE-SEDORIC.md). Pendant un echange HTTP,
; l'IRQ de reception de l'ACIA est autorisee et cette routine range chaque
; octet dans l'anneau de serial.c (serial_rx_ring/head/tail, 64 octets).
;
; void __fastcall__ serial_irq_install(unsigned acia_base);
; void __fastcall__ serial_irq_remove(void);
;
; Vecteur : la ROM saute en $0244 (JMP xxxx ; SEDORIC V4.0 y met JMP $0488) ;
; on remplace l'operande $0245/$0246 et on chaine vers l'ancienne cible pour
; toute IRQ qui n'est ni l'ACIA ni le Timer 1 du VIA. Le Timer 1 (100 Hz :
; clavier, curseur) est seulement acquitte : son traitement ROM, trop long,
; n'est pas fait pendant l'echange (le clavier n'y est pas lu).
; ===========================================================================
        .export _serial_irq_install, _serial_irq_remove
        .import _serial_rx_ring, _serial_rx_head, _serial_rx_tail

IRQVEC   = $0245                ; operande du JMP en $0244
VIA_IFR  = $030D
VIA_T1CL = $0304
RDRF     = $08
RXMASK   = 63                   ; anneau de 64 octets (serial.c)
CMD_IRQ  = $01                  ; DTR, IRQ de reception AUTORISEE (IRD=0), TIC=00
CMD_POLL = $03                  ; DTR, IRQ de reception coupee (config « emu » d'acia6551_init)

.bss
saved:  .res 2
served: .res 1

.code
_serial_irq_install:
        sta     q_da+1          ; DATA = base
        stx     q_da+2
        clc
        adc     #1
        sta     q_st+1          ; STATUS = base+1
        txa
        adc     #0
        sta     q_st+2
        lda     q_da+1
        clc
        adc     #2
        sta     q_cm+1          ; COMMAND = base+2
        sta     q_cm2+1
        lda     q_da+2
        adc     #0
        sta     q_cm+2
        sta     q_cm2+2
        php
        sei
        lda     IRQVEC
        sta     saved
        lda     IRQVEC+1
        sta     saved+1
        lda     #<isr
        sta     IRQVEC
        lda     #>isr
        sta     IRQVEC+1
        lda     #CMD_IRQ
q_cm:   sta     $FFFF           ; operande ecrite ci-dessus
        plp
        cli                     ; l'echange tourne IRQ actives
        rts

_serial_irq_remove:
        php
        sei
        lda     #CMD_POLL
q_cm2:  sta     $FFFF
        lda     saved
        sta     IRQVEC
        lda     saved+1
        sta     IRQVEC+1
        plp
        rts

isr:    pha
        txa
        pha
        lda     #0
        sta     served
q_st:   lda     $FFFF           ; statut : lit (et acquitte) l'IRQ ACIA
        and     #RDRF
        beq     i_via
q_da:   lda     $FFFF           ; donnee
        ldx     _serial_rx_tail
        sta     _serial_rx_ring,x
        inx
        txa
        and     #RXMASK
        cmp     _serial_rx_head
        beq     i_full           ; plein : octet perdu, l'anneau reste coherent
        sta     _serial_rx_tail
i_full:  inc     served
i_via:   lda     VIA_IFR
        and     #$40            ; Timer 1 ?
        beq     i_other
        lda     VIA_T1CL        ; acquitte T1 (traitement ROM saute)
        jmp     i_out
i_other: lda     served
        bne     i_out
        pla                     ; ni ACIA ni T1 : gestionnaire d'origine
        tax
        pla
        jmp     (saved)
i_out:   pla
        tax
        pla
        rti
