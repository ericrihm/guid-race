#include "implementations.h"

// Pure arithmetic: no lookup table at all.
// nibble + '0' + 39*(nibble >= 10)
// Since 'a' - '0' - 10 = 39, this maps 0-9→'0'-'9', 10-15→'a'-'f'.
// Trades table cache pressure for ALU ops — wins when L1 is contested.
static inline char to_hex(uint8_t nibble)
{
    return (char)(nibble + '0' + (39 & -(nibble >= 10)));
}

void guid_arithmetic(const IID *iid, char *out)
{
    const uint8_t *p = (const uint8_t *)iid;

    out[0]  = to_hex(p[3] >> 4);   out[1]  = to_hex(p[3] & 0xF);
    out[2]  = to_hex(p[2] >> 4);   out[3]  = to_hex(p[2] & 0xF);
    out[4]  = to_hex(p[1] >> 4);   out[5]  = to_hex(p[1] & 0xF);
    out[6]  = to_hex(p[0] >> 4);   out[7]  = to_hex(p[0] & 0xF);
    out[8]  = '-';

    out[9]  = to_hex(p[5] >> 4);   out[10] = to_hex(p[5] & 0xF);
    out[11] = to_hex(p[4] >> 4);   out[12] = to_hex(p[4] & 0xF);
    out[13] = '-';

    out[14] = to_hex(p[7] >> 4);   out[15] = to_hex(p[7] & 0xF);
    out[16] = to_hex(p[6] >> 4);   out[17] = to_hex(p[6] & 0xF);
    out[18] = '-';

    out[19] = to_hex(p[8] >> 4);   out[20] = to_hex(p[8] & 0xF);
    out[21] = to_hex(p[9] >> 4);   out[22] = to_hex(p[9] & 0xF);
    out[23] = '-';

    out[24] = to_hex(p[10] >> 4);  out[25] = to_hex(p[10] & 0xF);
    out[26] = to_hex(p[11] >> 4);  out[27] = to_hex(p[11] & 0xF);
    out[28] = to_hex(p[12] >> 4);  out[29] = to_hex(p[12] & 0xF);
    out[30] = to_hex(p[13] >> 4);  out[31] = to_hex(p[13] & 0xF);
    out[32] = to_hex(p[14] >> 4);  out[33] = to_hex(p[14] & 0xF);
    out[34] = to_hex(p[15] >> 4);  out[35] = to_hex(p[15] & 0xF);
    out[36] = '\0';
}
