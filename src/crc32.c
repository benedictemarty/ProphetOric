/* crc32.c — voir crc32.h. Tables construites octet par octet (pas d'unsigned long :
 * les opérations 32 bits du runtime cc65 coûtent des centaines de cycles). */
#include "crc32.h"

unsigned char crc32_t0[256], crc32_t1[256], crc32_t2[256], crc32_t3[256];
unsigned char crc32_v[4];
static unsigned char built;

static void build(void)
{
    unsigned int i; unsigned char k, c0, c1, c2, c3, lsb;
    for (i = 0; i < 256; ++i) {
        c0 = (unsigned char)i; c1 = c2 = c3 = 0;
        for (k = 0; k < 8; ++k) {
            lsb = c0 & 1;
            c0 = (unsigned char)((c0 >> 1) | (c1 << 7));
            c1 = (unsigned char)((c1 >> 1) | (c2 << 7));
            c2 = (unsigned char)((c2 >> 1) | (c3 << 7));
            c3 >>= 1;
            if (lsb) { c0 ^= 0x20; c1 ^= 0x83; c2 ^= 0xB8; c3 ^= 0xED; }
        }
        crc32_t0[i] = c0; crc32_t1[i] = c1; crc32_t2[i] = c2; crc32_t3[i] = c3;
    }
    built = 1;
}

void crc32_start(void)
{
    if (!built) build();
    crc32_v[0] = crc32_v[1] = crc32_v[2] = crc32_v[3] = 0xFF;
}

void crc32_hex(char *out)
{
    static const char hx[] = "0123456789abcdef";
    unsigned char i, b;
    for (i = 0; i < 4; ++i) {
        b = (unsigned char)~crc32_v[3 - i];
        out[2 * i] = hx[b >> 4]; out[2 * i + 1] = hx[b & 15];
    }
    out[8] = 0;
}

#ifdef TEST_HOST
void crc32_buf(const unsigned char *buf, unsigned int n)
{
    unsigned char x;
    while (n--) {                                 /* même séquence que crc32_asm.s */
        x = *buf++ ^ crc32_v[0];
        crc32_v[0] = crc32_v[1] ^ crc32_t0[x];
        crc32_v[1] = crc32_v[2] ^ crc32_t1[x];
        crc32_v[2] = crc32_v[3] ^ crc32_t2[x];
        crc32_v[3] = crc32_t3[x];
    }
}
#endif
