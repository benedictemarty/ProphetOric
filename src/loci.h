/*
 * loci.h — API MIA du LOCI (op `$03AF`, xstack `$03AC`, stub `$03B0`) :
 * fichiers sur le stockage du LOCI (flash `0:`, clés USB `1:`-`4:`, SD).
 * Protocole relevé dans ~/bbsoric/client/loci.s (validé sur LOCI réel) et
 * ~/Oric1/src/io/loci_fs.c (émulation) : les arguments se POUSSENT en
 * écrivant `$03AC` (chaîne : NUL d'abord puis les octets à l'envers), A/X
 * dans `$03B4/$03B6`, l'op dans `$03AF`, puis JSR `$03B0` ; retour A = octet
 * bas, X = octet haut (négatif = erreur, code dans `$03AD`).
 */
#ifndef LOCI_H
#define LOCI_H

#define LOCI_O_RDONLY 0x01
#define LOCI_O_WRONLY 0x02
#define LOCI_O_CREAT  0x10
#define LOCI_O_TRUNC  0x20
#define LOCI_O_APPEND 0x40

unsigned char loci_present(void);                                  /* signature A9/A2/60 en $03B3/5/7 */
int  loci_open(const char *path, unsigned char flags);            /* fd ≥ 0, ou < 0 */
int  loci_read(unsigned char fd, unsigned char *buf, unsigned char n);   /* octets lus (0 = fin), < 0 erreur */
int  loci_write(unsigned char fd, const unsigned char *buf, unsigned char n); /* octets écrits, < 0 erreur */
int  loci_close(unsigned char fd);
int  loci_mkdir(const char *path);
unsigned int loci_errno(void);
/* répertoires : OPENDIR("") = liste des périphériques (« 0: Internal storage », « 1: MSC … »),
 * sinon le dossier ; READDIR remplit name (≤ 63 car., "" = fin) et is_dir */
int  loci_opendir(const char *path);
int  loci_readdir(unsigned char fd, char *name, unsigned char *is_dir);
int  loci_closedir(unsigned char fd);
#define LOCI_MNT_TAP 4                                              /* lecteur cassette (0-3 = disquettes) */
int  loci_mount(unsigned char drive, const char *path);           /* monte un .tap (4) ou un .dsk (0-3) */
#define LOCI_BOOT_FDC 0x01                                          /* ROM Microdisc en $A000 (boot disquette) */
#define LOCI_BOOT_B11 0x04                                          /* BASIC 1.1 (Atmos) */
int  loci_boot(unsigned char settings);                            /* MIA_BOOT : bascule les ROM et RESET (ne revient pas) */

#endif
