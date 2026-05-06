#include <immintrin.h>
#include <tmmintrin.h>
#include <string.h>
#include "implementations.h"

// ============================================================================
// SSSE3 scatter: the x86 equivalent of our ARM NEON scatter approach.
// Uses pshufb (the x86 equivalent of ARM's tbl) to:
//   1. Reorder bytes for endianness
//   2. Split into nibbles via shift + mask
//   3. Map nibbles to hex chars via pshufb-as-LUT
//   4. Scatter hex chars + hyphens into final positions
// ============================================================================

void guid_ssse3_scatter(const IID *iid, char *out)
{
    // Load 16 GUID bytes
    __m128i raw = _mm_loadu_si128((const __m128i *)iid);

    // Byte reorder for little-endian GUID fields
    const __m128i byte_order = _mm_setr_epi8(
        3, 2, 1, 0,  5, 4,  7, 6,  8, 9, 10, 11, 12, 13, 14, 15
    );
    __m128i ordered = _mm_shuffle_epi8(raw, byte_order);

    // Extract high and low nibbles
    __m128i lo_nibbles = _mm_and_si128(ordered, _mm_set1_epi8(0x0F));
    __m128i hi_nibbles = _mm_and_si128(_mm_srli_epi16(ordered, 4), _mm_set1_epi8(0x0F));

    // Hex lookup table: nibble -> ASCII hex char
    const __m128i hex_lut = _mm_setr_epi8(
        '0','1','2','3','4','5','6','7',
        '8','9','a','b','c','d','e','f'
    );
    __m128i hi_hex = _mm_shuffle_epi8(hex_lut, hi_nibbles);
    __m128i lo_hex = _mm_shuffle_epi8(hex_lut, lo_nibbles);

    // Interleave: hi[0],lo[0], hi[1],lo[1], ..., hi[7],lo[7]
    __m128i hex_lo16 = _mm_unpacklo_epi8(hi_hex, lo_hex); // bytes 0-7 -> 16 hex chars
    __m128i hex_hi16 = _mm_unpackhi_epi8(hi_hex, lo_hex); // bytes 8-15 -> 16 hex chars

    // Scatter tables: place hex chars + hyphens into output positions.
    // pshufb can only reference bytes within the same 128-bit register,
    // so we need a different strategy than ARM's tbl3.
    //
    // Approach: build each output chunk from a single source register,
    // then patch in hyphens via blend/OR with a mask.

    // Output chunk 1 (positions 0-15):
    // hex_lo16[0..7], '-', hex_lo16[8..11], '-', hex_lo16[12..13]
    const __m128i scatter1 = _mm_setr_epi8(
         0,  1,  2,  3,  4,  5,  6,  7,
        -1,  8,  9, 10, 11, -1, 12, 13
    );
    __m128i out1 = _mm_shuffle_epi8(hex_lo16, scatter1);
    // Patch hyphens: positions 8 and 13 (where scatter1 has -1, pshufb zeros them)
    const __m128i hyphen_mask1 = _mm_setr_epi8(
        0, 0, 0, 0, 0, 0, 0, 0,
        '-', 0, 0, 0, 0, '-', 0, 0
    );
    out1 = _mm_or_si128(out1, hyphen_mask1);

    // Output chunk 2 (positions 16-31):
    // hex_lo16[14..15], '-', hex_hi16[0..3], '-', hex_hi16[4..11]
    // Problem: this spans TWO source registers. Build in two parts and merge.

    // Part A: from hex_lo16 -> positions 16-17
    const __m128i scatter2a = _mm_setr_epi8(
        14, 15, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1
    );
    __m128i part_a = _mm_shuffle_epi8(hex_lo16, scatter2a);

    // Part B: from hex_hi16 -> positions 19-31
    const __m128i scatter2b = _mm_setr_epi8(
        -1, -1, -1,  0,  1,  2,  3, -1,
         4,  5,  6,  7,  8,  9, 10, 11
    );
    __m128i part_b = _mm_shuffle_epi8(hex_hi16, scatter2b);

    // Merge: OR parts together (non-overlapping)
    __m128i out2 = _mm_or_si128(part_a, part_b);
    // Patch hyphens at positions 18 and 23
    const __m128i hyphen_mask2 = _mm_setr_epi8(
        0, 0, '-', 0, 0, 0, 0, '-',
        0, 0, 0, 0, 0, 0, 0, 0
    );
    out2 = _mm_or_si128(out2, hyphen_mask2);

    // Store first 32 bytes
    _mm_storeu_si128((__m128i *)out, out1);
    _mm_storeu_si128((__m128i *)(out + 16), out2);

    // Tail: hex_hi16[12..15] -> positions 32-35
    // Extract as 32-bit int and store
    uint32_t tail = (uint32_t)_mm_extract_epi32(hex_hi16, 3);
    memcpy(out + 32, &tail, 4);
    out[36] = '\0';
}

// ============================================================================
// SSE2 basic: works on any x86_64 (no SSSE3 required).
// Uses SSE2 arithmetic instead of pshufb for nibble-to-hex.
// ============================================================================

void guid_sse2_basic(const IID *iid, char *out)
{
    const uint8_t *p = (const uint8_t *)iid;

    // Byte order for GUID canonical form
    static const uint8_t order[16] = {
        3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15
    };

    // Load and reorder bytes manually (no pshufb in SSE2)
    uint8_t ordered[16];
    for (int i = 0; i < 16; i++)
        ordered[i] = p[order[i]];

    // Load reordered bytes into SSE2 register
    __m128i data = _mm_loadu_si128((const __m128i *)ordered);

    // Extract nibbles
    __m128i mask_0f = _mm_set1_epi8(0x0F);
    __m128i lo = _mm_and_si128(data, mask_0f);
    __m128i hi = _mm_and_si128(_mm_srli_epi16(data, 4), mask_0f);

    // Arithmetic hex conversion (SSE2, no lookup table):
    // nibble + '0' + 39*(nibble > 9)
    __m128i ascii_0 = _mm_set1_epi8('0');
    __m128i nine = _mm_set1_epi8(9);
    __m128i adj = _mm_set1_epi8(39);

    // SSE2 doesn't have unsigned byte compare, use signed trick:
    // cmpgt works on signed bytes. Since nibbles are 0-15 (all positive), this works.
    __m128i hi_mask = _mm_cmpgt_epi8(hi, nine);
    __m128i lo_mask = _mm_cmpgt_epi8(lo, nine);

    __m128i hi_hex = _mm_add_epi8(_mm_add_epi8(hi, ascii_0), _mm_and_si128(hi_mask, adj));
    __m128i lo_hex = _mm_add_epi8(_mm_add_epi8(lo, ascii_0), _mm_and_si128(lo_mask, adj));

    // Interleave
    __m128i hex_lo16 = _mm_unpacklo_epi8(hi_hex, lo_hex);
    __m128i hex_hi16 = _mm_unpackhi_epi8(hi_hex, lo_hex);

    // Store to temp buffer then scatter with hyphens
    uint8_t hex32[32];
    _mm_storeu_si128((__m128i *)hex32, hex_lo16);
    _mm_storeu_si128((__m128i *)(hex32 + 16), hex_hi16);

    memcpy(out, hex32, 8);
    out[8] = '-';
    memcpy(out + 9, hex32 + 8, 4);
    out[13] = '-';
    memcpy(out + 14, hex32 + 12, 4);
    out[18] = '-';
    memcpy(out + 19, hex32 + 16, 4);
    out[23] = '-';
    memcpy(out + 24, hex32 + 20, 12);
    out[36] = '\0';
}
