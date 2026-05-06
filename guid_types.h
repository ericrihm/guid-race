#ifndef GUID_TYPES_H
#define GUID_TYPES_H

#include <stdint.h>

// Use the real Windows GUID layout when available, otherwise define our own.
// Binary-compatible with Windows GUID / COM IID.
#ifdef _WIN32
#include <guiddef.h>
#else
typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;
#endif

typedef GUID IID;

// Output: "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (36 chars + null)
#define GUID_STRING_LEN 37

#endif
