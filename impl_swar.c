#include <string.h>
#include "implementations.h"

// SWAR: 32-bit packed stores — write 4 hex chars at once.
// Combines branchless arithmetic with word-sized memory writes.
static inline uint32_t pair_to_hex4(uint8_t b0, uint8_t b1)
{
    uint32_t n0h = b0 >> 4,   n0l = b0 & 0xF;
    uint32_t n1h = b1 >> 4,   n1l = b1 & 0xF;
    return (n0h + 48 + (39 & -(n0h >= 10)))
         | ((n0l + 48 + (39 & -(n0l >= 10))) << 8)
         | ((n1h + 48 + (39 & -(n1h >= 10))) << 16)
         | ((n1l + 48 + (39 & -(n1l >= 10))) << 24);
}

void guid_swar(const IID *iid, char *out)
{
    const uint8_t *p = (const uint8_t *)iid;
    uint32_t w;

    // Data1: bytes 3,2,1,0
    w = pair_to_hex4(p[3], p[2]); memcpy(out + 0, &w, 4);
    w = pair_to_hex4(p[1], p[0]); memcpy(out + 4, &w, 4);
    out[8] = '-';

    // Data2: bytes 5,4
    w = pair_to_hex4(p[5], p[4]); memcpy(out + 9, &w, 4);
    out[13] = '-';

    // Data3: bytes 7,6
    w = pair_to_hex4(p[7], p[6]); memcpy(out + 14, &w, 4);
    out[18] = '-';

    // Data4[0:2]
    w = pair_to_hex4(p[8], p[9]); memcpy(out + 19, &w, 4);
    out[23] = '-';

    // Data4[2:8]
    w = pair_to_hex4(p[10], p[11]); memcpy(out + 24, &w, 4);
    w = pair_to_hex4(p[12], p[13]); memcpy(out + 28, &w, 4);
    w = pair_to_hex4(p[14], p[15]); memcpy(out + 32, &w, 4);
    out[36] = '\0';
}
