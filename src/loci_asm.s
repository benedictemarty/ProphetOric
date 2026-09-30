; ===========================================================================
; loci_asm.s - ProphetOric : empilage rapide sur la pile XSTACK du LOCI
;
; void __fastcall__ loci_push_rev(const unsigned char *buf, unsigned char n);
;
; Pousse buf[n-1] .. buf[0] dans XSTACK ($03AC) : a l'envers, le firmware
; depile dans l'ordre. ~16 cycles/octet, contre ~100 pour la boucle C (pointeur
; sur la pile cc65) : entre deux blocs de 128 octets recus a 9600 bauds,
; l'anneau de 32 octets du LOCI ne gardait que 3-4 octets de marge.
; ===========================================================================
        .export _loci_push_rev
        .import popax
        .importzp ptr1, tmp1

XSTACK  = $03AC

_loci_push_rev:
        sta     tmp1            ; n
        jsr     popax           ; buf
        sta     ptr1
        stx     ptr1+1
        ldy     tmp1
        beq     @done
@loop:  dey
        lda     (ptr1),y
        sta     XSTACK
        tya
        bne     @loop
@done:  rts
