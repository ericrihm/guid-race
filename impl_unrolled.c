#include "implementations.h"

static const char hex[] = "0123456789abcdef";

// Fully unrolled: zero branches, zero loops, direct stores.
// Answers Dave's challenge about branchless hyphen insertion —
// hyphens are just explicit writes at known offsets.
void guid_unrolled(const IID *iid, char *out)
{
    const uint8_t *p = (const uint8_t *)iid;

    // Data1: bytes 3,2,1,0 → positions 0-7
    out[0]  = hex[p[3] >> 4];   out[1]  = hex[p[3] & 0xF];
    out[2]  = hex[p[2] >> 4];   out[3]  = hex[p[2] & 0xF];
    out[4]  = hex[p[1] >> 4];   out[5]  = hex[p[1] & 0xF];
    out[6]  = hex[p[0] >> 4];   out[7]  = hex[p[0] & 0xF];
    out[8]  = '-';

    // Data2: bytes 5,4 → positions 9-12
    out[9]  = hex[p[5] >> 4];   out[10] = hex[p[5] & 0xF];
    out[11] = hex[p[4] >> 4];   out[12] = hex[p[4] & 0xF];
    out[13] = '-';

    // Data3: bytes 7,6 → positions 14-17
    out[14] = hex[p[7] >> 4];   out[15] = hex[p[7] & 0xF];
    out[16] = hex[p[6] >> 4];   out[17] = hex[p[6] & 0xF];
    out[18] = '-';

    // Data4[0..1] → positions 19-22
    out[19] = hex[p[8] >> 4];   out[20] = hex[p[8] & 0xF];
    out[21] = hex[p[9] >> 4];   out[22] = hex[p[9] & 0xF];
    out[23] = '-';

    // Data4[2..7] → positions 24-35
    out[24] = hex[p[10] >> 4];  out[25] = hex[p[10] & 0xF];
    out[26] = hex[p[11] >> 4];  out[27] = hex[p[11] & 0xF];
    out[28] = hex[p[12] >> 4];  out[29] = hex[p[12] & 0xF];
    out[30] = hex[p[13] >> 4];  out[31] = hex[p[13] & 0xF];
    out[32] = hex[p[14] >> 4];  out[33] = hex[p[14] & 0xF];
    out[34] = hex[p[15] >> 4];  out[35] = hex[p[15] & 0xF];
    out[36] = '\0';
}
