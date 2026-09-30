/* loci.c — voir loci.h. Registres MIA accédés en C (volatile) ; l'appel se
 * fait par le stub 6502 que le LOCI expose en $03B0 (clv / bvc / lda / ldx / rts). */
#include "loci.h"

#define REG(a) (*(volatile unsigned char *)(a))
#define XSTACK 0x03AC
#define ERRLO  0x03AD
#define OP     0x03AF
#define AREG   0x03B4
#define XREG   0x03B6

typedef int (*stub_fn)(void);
#define CALL(op_) (REG(OP) = (op_), ((stub_fn)0x03B0)())

#define OP_OPEN 0x14
#define OP_CLOSE 0x15
#define OP_READ_XSTACK 0x16
#define OP_WRITE_XSTACK 0x18
#define OP_OPENDIR 0x80
#define OP_CLOSEDIR 0x81
#define OP_READDIR 0x82
#define OP_MKDIR 0x83
#define OP_MOUNT 0x90
#define OP_MIA_BOOT 0xA0

unsigned char loci_present(void)
{
    return REG(0x03B3) == 0xA9 && REG(0x03B5) == 0xA2 && REG(0x03B7) == 0x60;
}

unsigned int loci_errno(void) { return REG(ERRLO) | (REG(0x03AE) << 8); }

/* pousse une chaîne : NUL d'abord, puis les octets du dernier au premier */
static void push_zstring(const char *s)
{
    const char *p = s;
    while (*p) ++p;
    REG(XSTACK) = 0;
    while (p > s) REG(XSTACK) = (unsigned char)*--p;
}

int loci_open(const char *path, unsigned char flags)
{
    push_zstring(path);
    REG(AREG) = flags; REG(XREG) = 0;
    return CALL(OP_OPEN);
}

int loci_close(unsigned char fd)
{
    REG(AREG) = fd; REG(XREG) = 0;
    return CALL(OP_CLOSE);
}

int loci_mkdir(const char *path)
{
    push_zstring(path);
    REG(AREG) = 0; REG(XREG) = 0;
    return CALL(OP_MKDIR);
}

void __fastcall__ loci_push_rev(const unsigned char *buf, unsigned char n);   /* loci_asm.s */

int loci_write(unsigned char fd, const unsigned char *buf, unsigned char n)
{
    loci_push_rev(buf, n);                        /* loci_asm.s : à l'envers, le firmware dépile dans l'ordre */
    REG(AREG) = fd; REG(XREG) = 0;
    return CALL(OP_WRITE_XSTACK);
}

int loci_read(unsigned char fd, unsigned char *buf, unsigned char n)
{
    int r; unsigned char i;
    REG(XSTACK) = 0;                              /* nombre d'octets demandé (16 bits) : octet haut puis bas */
    REG(XSTACK) = n;
    REG(AREG) = fd; REG(XREG) = 0;
    r = CALL(OP_READ_XSTACK);
    if (r <= 0) return r;
    for (i = 0; i < (unsigned char)r; ++i) buf[i] = REG(XSTACK);   /* lire $03AC dépile dans l'ordre */
    return r;
}

int loci_mount(unsigned char drive, const char *path)
{
    push_zstring(path);
    REG(AREG) = drive; REG(XREG) = 0;
    return CALL(OP_MOUNT);
}

int loci_boot(unsigned char settings)
{
    REG(AREG) = settings; REG(XREG) = 0;
    return CALL(OP_MIA_BOOT);
}

int loci_opendir(const char *path)
{
    if (path && *path) push_zstring(path);        /* pile vide = liste des périphériques */
    REG(AREG) = 0; REG(XREG) = 0;
    return CALL(OP_OPENDIR);
}

int loci_closedir(unsigned char fd)
{
    REG(AREG) = fd; REG(XREG) = 0;
    return CALL(OP_CLOSEDIR);
}

/* dirent de 72 octets sur la xstack : fd(2) nom[64] attrib(1) 0 taille(4) */
int loci_readdir(unsigned char fd, char *name, unsigned char *is_dir)
{
    int r; unsigned char i, b;
    REG(AREG) = fd; REG(XREG) = 0;
    r = CALL(OP_READDIR);
    if (r < 0) return r;
    b = REG(XSTACK); b = REG(XSTACK);             /* fd */
    for (i = 0; i < 64; ++i) { b = REG(XSTACK); if (i < 63) name[i] = (char)b; }
    name[63] = 0;
    b = REG(XSTACK); *is_dir = (b & 0x10) != 0;   /* AM_DIR */
    b = REG(XSTACK); b = REG(XSTACK); b = REG(XSTACK); b = REG(XSTACK); b = REG(XSTACK);
    return 0;
}
