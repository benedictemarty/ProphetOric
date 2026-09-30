/*
 * crc32.h — CRC-32 IEEE (polynôme réfléchi 0xEDB88320, celui de zlib et de
 * /crc32/<id> du serveur). Quatre tables d'octets (poids faible → fort) construites
 * au premier appel ; la boucle Oric (crc32_asm.s, 6502 NMOS) lit directement la
 * pile XSTACK du LOCI après un READ_XSTACK, sans copie en RAM.
 */
#ifndef CRC32_H
#define CRC32_H

extern unsigned char crc32_t0[256], crc32_t1[256], crc32_t2[256], crc32_t3[256];
extern unsigned char crc32_v[4];                      /* état courant, poids faible d'abord */

void crc32_start(void);                               /* construit les tables si besoin, état = FFFFFFFF */
void crc32_hex(char *out);                            /* CRC final en 8 chiffres hex minuscules + NUL */
#ifdef TEST_HOST
void crc32_buf(const unsigned char *buf, unsigned int n);        /* référence C (tests hôte) */
#else
void __fastcall__ crc32_xstack(unsigned char n);                 /* dépile n octets de $03AC (crc32_asm.s) */
#endif

#endif
