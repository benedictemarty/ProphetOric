; ===========================================================================
; sed.s - spike S1 : commande Sedoric depuis un programme cc65 (Sedoric V4.0)
;
; unsigned char __fastcall__ sed_present(void);   1 si le vecteur "!" $0467 est
;     celui de SEDORIC V4.0 (LDA #$AE / LDY #$D3 : interpreteur $D3AE)
; void __fastcall__ sed_cmd(const char *line);    execute une ligne SEDORIC
;     terminee par $00 (jamais CR : ~/Oric1/docs/SEDORIC.md) via $0467.
;
; cc65 range ses variables de page zero en $E2-$FB, la ou BASIC/SEDORIC ont
; CHRGET (TXTPTR = $E9/$EA, operande du LDA en $E8) : on les sauve, on remet la
; copie de CHRGET relevee au prompt SEDORIC V4.0 (SEDO40u.DSK, RAM $E2-$FB dans
; Phosphoric), on appelle, puis on restaure. Valable pour cette version seulement.
; ===========================================================================
        .export _sed_present, _sed_cmd

ZP0     = $E2
ZPLEN   = 26                    ; $E2-$FB
TXTPTR  = $E9
BANG    = $0467

.rodata
chrget: .byte $E6,$E9,$D0,$02,$E6,$EA,$AD,$36,$00,$C9,$20,$F0,$F3
        .byte $4C,$00,$04,$FF,$FF,$F0,$B7,$2C,$60,$EA,$60,$80,$4F
.bss
zpbak:  .res ZPLEN
cmd:    .res 2
.code
_sed_present:
        lda     BANG
        cmp     #$A9
        bne     @no
        lda     BANG+1
        cmp     #$AE
        bne     @no
        lda     BANG+2
        cmp     #$A0
        bne     @no
        lda     BANG+3
        cmp     #$D3
        bne     @no
        lda     #1
        ldx     #0
        rts
@no:    lda     #0
        tax
        rts

_sed_cmd:
        sta     cmd
        stx     cmd+1
        ldx     #ZPLEN-1
@save:  lda     ZP0,x
        sta     zpbak,x
        lda     chrget,x
        sta     ZP0,x
        dex
        bpl     @save
        lda     cmd
        sta     TXTPTR
        lda     cmd+1
        sta     TXTPTR+1
        jsr     BANG
        ldx     #ZPLEN-1
@rest:  lda     zpbak,x
        sta     ZP0,x
        dex
        bpl     @rest
        rts
