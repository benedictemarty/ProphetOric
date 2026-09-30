; ===========================================================================
; crc32_asm.s - ProphetOric : CRC-32 des octets de la pile XSTACK du LOCI
;
; void __fastcall__ crc32_xstack(unsigned char n);
;
; Depile n octets de $03AC (apres OP_READ_XSTACK, lecture = ordre du fichier)
; et met a jour crc32_v (poids faible d'abord) : crc = (crc >> 8) ^ T[(crc ^ b) & $FF],
; la table T etant rangee en quatre tables d'octets crc32_t0..t3 (crc32.c).
; 6502 NMOS (pas d'instruction 65C02) : ~40 cycles/octet. n = 0 : rien.
; ===========================================================================
        .export _crc32_xstack
        .import _crc32_v, _crc32_t0, _crc32_t1, _crc32_t2, _crc32_t3

XSTACK  = $03AC

_crc32_xstack:
        tay
        beq     @done
@loop:  lda     XSTACK
        eor     _crc32_v
        tax
        lda     _crc32_v+1
        eor     _crc32_t0,x
        sta     _crc32_v
        lda     _crc32_v+2
        eor     _crc32_t1,x
        sta     _crc32_v+1
        lda     _crc32_v+3
        eor     _crc32_t2,x
        sta     _crc32_v+2
        lda     _crc32_t3,x
        sta     _crc32_v+3
        dey
        bne     @loop
@done:  rts
