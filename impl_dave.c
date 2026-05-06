#include "implementations.h"

static const char hex[] = "0123456789abcdef";

static const unsigned char order[16] = {
    3,2,1,0,  5,4,  7,6,  8,9,10,11,12,13,14,15
};

void guid_dave_original(const IID *iid, char *out)
{
    const unsigned char *p = (const unsigned char *)iid;
    int i, j = 0;

    for (i = 0; i < 16; i++)
    {
        unsigned char b = p[order[i]];

        out[j++] = hex[b >> 4];
        out[j++] = hex[b & 0xF];

        if (j == 8 || j == 13 || j == 18 || j == 23)
            out[j++] = '-';
    }

    out[j] = '\0';
}
