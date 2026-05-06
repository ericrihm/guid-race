#include <stdio.h>
#include "implementations.h"

void guid_sprintf(const IID *iid, char *out)
{
    snprintf(out, GUID_STRING_LEN,
        "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        iid->Data1, iid->Data2, iid->Data3,
        iid->Data4[0], iid->Data4[1],
        iid->Data4[2], iid->Data4[3],
        iid->Data4[4], iid->Data4[5],
        iid->Data4[6], iid->Data4[7]);
}
