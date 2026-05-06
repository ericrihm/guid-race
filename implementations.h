#ifndef IMPLEMENTATIONS_H
#define IMPLEMENTATIONS_H

#include "guid_types.h"

// --- Scalar implementations (all platforms) ---
void guid_sprintf(const IID *iid, char *out);
void guid_dave_original(const IID *iid, char *out);
void guid_lookup16(const IID *iid, char *out);
void guid_unrolled(const IID *iid, char *out);
void guid_arithmetic(const IID *iid, char *out);
void guid_swar(const IID *iid, char *out);

// --- ARM NEON implementations ---
#if defined(HAS_NEON) || defined(__aarch64__) || defined(_M_ARM64)
void guid_neon_simd(const IID *iid, char *out);
void guid_neon_tbl2(const IID *iid, char *out);
void guid_neon_scatter(const IID *iid, char *out);
void guid_neon_arith(const IID *iid, char *out);
void guid_neon_ultimate(const IID *iid, char *out);
void guid_neon_fused(const IID *iid, char *out);
#endif

// --- x86 SSE/SSSE3 implementations ---
#if defined(HAS_SSE) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
void guid_ssse3_scatter(const IID *iid, char *out);
void guid_sse2_basic(const IID *iid, char *out);
#endif

typedef void (*guid_fn)(const IID *iid, char *out);

typedef struct {
    const char *name;
    guid_fn     fn;
} impl_entry;

#endif
