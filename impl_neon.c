#include <arm_neon.h>
#include <string.h>
#include "implementations.h"

// NEON SIMD: process all 16 GUID bytes in parallel.
// Uses vqtbl1q_u8 as a 16-entry vectorized lookup table.
void guid_neon_simd(const IID *iid, char *out)
{
    // Byte reorder table for little-endian GUID fields
    static const uint8_t byte_order[16] __attribute__((aligned(16))) = {
        3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15
    };
    // Hex character lookup table
    static const uint8_t hex_lut[16] __attribute__((aligned(16))) = {
        '0','1','2','3','4','5','6','7',
        '8','9','a','b','c','d','e','f'
    };

    uint8x16_t raw = vld1q_u8((const uint8_t *)iid);
    uint8x16_t order = vld1q_u8(byte_order);
    uint8x16_t ordered = vqtbl1q_u8(raw, order);

    uint8x16_t hi_nibbles = vshrq_n_u8(ordered, 4);
    uint8x16_t lo_nibbles = vandq_u8(ordered, vdupq_n_u8(0x0F));

    uint8x16_t lut = vld1q_u8(hex_lut);
    uint8x16_t hi_hex = vqtbl1q_u8(lut, hi_nibbles);
    uint8x16_t lo_hex = vqtbl1q_u8(lut, lo_nibbles);

    // Interleave: hi[0],lo[0], hi[1],lo[1], ..., hi[7],lo[7]
    //             hi[8],lo[8], ..., hi[15],lo[15]
    uint8x16x2_t zipped = vzipq_u8(hi_hex, lo_hex);
    // zipped.val[0] = 16 chars for GUID bytes 0-7
    // zipped.val[1] = 16 chars for GUID bytes 8-15

    uint8_t hex32[32];
    vst1q_u8(hex32, zipped.val[0]);
    vst1q_u8(hex32 + 16, zipped.val[1]);

    // Scatter into output with hyphens at 8,13,18,23
    memcpy(out, hex32, 8);           // Data1: 8 hex chars
    out[8] = '-';
    memcpy(out + 9, hex32 + 8, 4);   // Data2: 4 hex chars
    out[13] = '-';
    memcpy(out + 14, hex32 + 12, 4); // Data3: 4 hex chars
    out[18] = '-';
    memcpy(out + 19, hex32 + 16, 4); // Data4[0:2]: 4 hex chars
    out[23] = '-';
    memcpy(out + 24, hex32 + 20, 12); // Data4[2:8]: 12 hex chars
    out[36] = '\0';
}

// NEON v2: eliminate temp buffer by scattering directly via shuffle.
// Uses a 48-byte "mega-shuffle" that places hex chars + hyphens + null
// into three 16-byte output chunks.
void guid_neon_tbl2(const IID *iid, char *out)
{
    static const uint8_t byte_order[16] __attribute__((aligned(16))) = {
        3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15
    };
    static const uint8_t hex_lut[16] __attribute__((aligned(16))) = {
        '0','1','2','3','4','5','6','7',
        '8','9','a','b','c','d','e','f'
    };

    uint8x16_t raw = vld1q_u8((const uint8_t *)iid);
    uint8x16_t ordered = vqtbl1q_u8(raw, vld1q_u8(byte_order));

    uint8x16_t hi = vshrq_n_u8(ordered, 4);
    uint8x16_t lo = vandq_u8(ordered, vdupq_n_u8(0x0F));
    uint8x16_t lut = vld1q_u8(hex_lut);
    uint8x16_t hi_hex = vqtbl1q_u8(lut, hi);
    uint8x16_t lo_hex = vqtbl1q_u8(lut, lo);

    // We have 32 hex chars in hi_hex[i] and lo_hex[i] interleaved.
    // Build output directly using ext/zip/tbl to place chars.
    // Use a 4-register table {hi_hex, lo_hex} and a single tbl4 to scatter.

    // Build source: 32 chars indexed as:
    //   hi_hex[0]=idx 0, lo_hex[0]=idx 16, hi_hex[1]=idx 1, lo_hex[1]=idx 17, ...
    // We need output order (hex positions only, hyphens separate):
    //   h[0],l[0], h[1],l[1], h[2],l[2], h[3],l[3],  <- Data1
    //   h[4],l[4], h[5],l[5],                          <- Data2
    //   h[6],l[6], h[7],l[7],                          <- Data3
    //   h[8],l[8], h[9],l[9],                          <- Data4[0:2]
    //   h[10],l[10],...,h[15],l[15]                     <- Data4[2:8]

    // Output chunk 1 (positions 0-15): 8 hex + '-' + 4 hex + '-' + 2 hex
    // Indices into {hi_hex, lo_hex} 2-register table:
    //   0,16, 1,17, 2,18, 3,19, '-', 4,20, 5,21, '-', 6,22, 7,23
    // But we can't encode '-' via table lookup. Use a merge approach instead.

    // Simpler: use vzipq result + scalar hyphen insertion (already fast)
    uint8x16x2_t zipped = vzipq_u8(hi_hex, lo_hex);

    // Direct stores from NEON registers using lane extractions
    // First 8 bytes of zipped.val[0] → out[0..7]
    vst1_u8((uint8_t *)out, vget_low_u8(zipped.val[0]));
    out[8] = '-';

    // Bytes 8-11 of zipped.val[0] → out[9..12]
    uint32_t d2;
    vst1q_lane_u32(&d2, vreinterpretq_u32_u8(zipped.val[0]), 2);
    memcpy(out + 9, &d2, 4);
    out[13] = '-';

    // Bytes 12-15 of zipped.val[0] → out[14..17]
    uint32_t d3;
    vst1q_lane_u32(&d3, vreinterpretq_u32_u8(zipped.val[0]), 3);
    memcpy(out + 14, &d3, 4);
    out[18] = '-';

    // First 4 bytes of zipped.val[1] → out[19..22]
    uint32_t d4a;
    vst1q_lane_u32(&d4a, vreinterpretq_u32_u8(zipped.val[1]), 0);
    memcpy(out + 19, &d4a, 4);
    out[23] = '-';

    // Bytes 4-15 of zipped.val[1] → out[24..35]
    uint32_t d4b;
    vst1q_lane_u32(&d4b, vreinterpretq_u32_u8(zipped.val[1]), 1);
    memcpy(out + 24, &d4b, 4);
    vst1_u8((uint8_t *)(out + 28), vget_high_u8(zipped.val[1]));

    out[36] = '\0';
}

// NEON v3: vqtbl3q_u8 scatter — build output with hyphens pre-placed.
// 3-register source table: {hex_first_16, hex_last_16, all_hyphens}.
// Three 16-byte stores (last overlapping) cover entire output.
void guid_neon_scatter(const IID *iid, char *out)
{
    static const uint8_t byte_order[16] __attribute__((aligned(16))) = {
        3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15
    };
    static const uint8_t hex_lut[16] __attribute__((aligned(16))) = {
        '0','1','2','3','4','5','6','7',
        '8','9','a','b','c','d','e','f'
    };

    static const uint8_t scatter1[16] __attribute__((aligned(16))) = {
         0,  1,  2,  3,  4,  5,  6,  7,
        32,  8,  9, 10, 11, 32, 12, 13
    };
    static const uint8_t scatter2[16] __attribute__((aligned(16))) = {
        14, 15, 32, 16, 17, 18, 19, 32,
        20, 21, 22, 23, 24, 25, 26, 27
    };

    uint8x16_t raw = vld1q_u8((const uint8_t *)iid);
    uint8x16_t ordered = vqtbl1q_u8(raw, vld1q_u8(byte_order));

    uint8x16_t hi = vshrq_n_u8(ordered, 4);
    uint8x16_t lo = vandq_u8(ordered, vdupq_n_u8(0x0F));
    uint8x16_t lut = vld1q_u8(hex_lut);
    uint8x16_t hi_hex = vqtbl1q_u8(lut, hi);
    uint8x16_t lo_hex = vqtbl1q_u8(lut, lo);

    uint8x16x2_t zipped = vzipq_u8(hi_hex, lo_hex);

    uint8x16x3_t tbl = { zipped.val[0], zipped.val[1], vdupq_n_u8('-') };

    vst1q_u8((uint8_t *)out,        vqtbl3q_u8(tbl, vld1q_u8(scatter1)));
    vst1q_u8((uint8_t *)(out + 16), vqtbl3q_u8(tbl, vld1q_u8(scatter2)));

    uint32_t tail;
    vst1q_lane_u32(&tail, vreinterpretq_u32_u8(zipped.val[1]), 3);
    memcpy(out + 32, &tail, 4);
    out[36] = '\0';
}

// NEON v4: arithmetic nibble-to-hex (no lookup table at all).
// Eliminates the hex_lut load — uses vectorized arithmetic instead.
void guid_neon_arith(const IID *iid, char *out)
{
    static const uint8_t byte_order[16] __attribute__((aligned(16))) = {
        3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15
    };
    static const uint8_t scatter1[16] __attribute__((aligned(16))) = {
         0,  1,  2,  3,  4,  5,  6,  7,
        32,  8,  9, 10, 11, 32, 12, 13
    };
    static const uint8_t scatter2[16] __attribute__((aligned(16))) = {
        14, 15, 32, 16, 17, 18, 19, 32,
        20, 21, 22, 23, 24, 25, 26, 27
    };

    uint8x16_t raw = vld1q_u8((const uint8_t *)iid);
    uint8x16_t ordered = vqtbl1q_u8(raw, vld1q_u8(byte_order));

    uint8x16_t hi = vshrq_n_u8(ordered, 4);
    uint8x16_t lo = vandq_u8(ordered, vdupq_n_u8(0x0F));

    uint8x16_t nine = vdupq_n_u8(9);
    uint8x16_t ascii_0 = vdupq_n_u8('0');
    uint8x16_t adj = vdupq_n_u8(39);

    uint8x16_t hi_mask = vcgtq_u8(hi, nine);
    uint8x16_t lo_mask = vcgtq_u8(lo, nine);
    uint8x16_t hi_hex = vaddq_u8(vaddq_u8(hi, ascii_0), vandq_u8(hi_mask, adj));
    uint8x16_t lo_hex = vaddq_u8(vaddq_u8(lo, ascii_0), vandq_u8(lo_mask, adj));

    uint8x16x2_t zipped = vzipq_u8(hi_hex, lo_hex);

    uint8x16x3_t tbl = { zipped.val[0], zipped.val[1], vdupq_n_u8('-') };

    vst1q_u8((uint8_t *)out, vqtbl3q_u8(tbl, vld1q_u8(scatter1)));
    vst1q_u8((uint8_t *)(out + 16), vqtbl3q_u8(tbl, vld1q_u8(scatter2)));

    uint32_t tail2;
    vst1q_lane_u32(&tail2, vreinterpretq_u32_u8(zipped.val[1]), 3);
    memcpy(out + 32, &tail2, 4);
    out[36] = '\0';
}

// NEON v5 "ultimate": skip zip entirely. Use vqtbl4q_u8 with 4-register
// table {hi_hex, lo_hex, hyphens, nulls}. Scatter tables interleave
// hi/lo nibbles, place hyphens, and null-terminate in one pass.
// Three overlapping 16-byte stores write the entire output.
void guid_neon_ultimate(const IID *iid, char *out)
{
    static const uint8_t byte_order[16] __attribute__((aligned(16))) = {
        3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15
    };
    static const uint8_t hex_lut[16] __attribute__((aligned(16))) = {
        '0','1','2','3','4','5','6','7',
        '8','9','a','b','c','d','e','f'
    };

    // 4-register table layout (64 bytes):
    //   reg0 (idx 0-15):  hi_hex[0..15]  — high nibble hex chars
    //   reg1 (idx 16-31): lo_hex[0..15]  — low nibble hex chars
    //   reg2 (idx 32-47): all '-'        — hyphens
    //   reg3 (idx 48-63): all '\0'       — null terminators

    // Output: h0 l0 h1 l1 h2 l2 h3 l3 '-' h4 l4 h5 l5 '-' h6 l6
    //         h7 l7 '-' h8 l8 h9 l9 '-' hA lA hB lB hC lC hD lD
    //         hE lE hF lF '\0'

    // scatter1: positions 0-15
    static const uint8_t s1[16] __attribute__((aligned(16))) = {
         0, 16,  1, 17,  2, 18,  3, 19,
        32,  4, 20,  5, 21, 32,  6, 22
    };
    // scatter2: positions 16-31
    static const uint8_t s2[16] __attribute__((aligned(16))) = {
         7, 23, 32,  8, 24,  9, 25, 32,
        10, 26, 11, 27, 12, 28, 13, 29
    };
    // scatter3: positions 21-36 (overlapping store, includes null)
    static const uint8_t s3[16] __attribute__((aligned(16))) = {
         9, 25, 32, 10, 26, 11, 27, 12,
        28, 13, 29, 14, 30, 15, 31, 48
    };

    uint8x16_t raw = vld1q_u8((const uint8_t *)iid);
    uint8x16_t ordered = vqtbl1q_u8(raw, vld1q_u8(byte_order));

    uint8x16_t hi_hex = vqtbl1q_u8(vld1q_u8(hex_lut), vshrq_n_u8(ordered, 4));
    uint8x16_t lo_hex = vqtbl1q_u8(vld1q_u8(hex_lut), vandq_u8(ordered, vdupq_n_u8(0x0F)));

    uint8x16x4_t tbl = { hi_hex, lo_hex, vdupq_n_u8('-'), vdupq_n_u8('\0') };

    vst1q_u8((uint8_t *)out,       vqtbl4q_u8(tbl, vld1q_u8(s1)));
    vst1q_u8((uint8_t *)(out + 16), vqtbl4q_u8(tbl, vld1q_u8(s2)));
    vst1q_u8((uint8_t *)(out + 21), vqtbl4q_u8(tbl, vld1q_u8(s3)));
}

// NEON v6 "fused": byte reorder encoded INTO scatter tables.
// Eliminates the tbl1 byte-reorder instruction — nibble extraction
// operates on raw memory-order bytes, scatter tables handle canonical order.
void guid_neon_fused(const IID *iid, char *out)
{
    static const uint8_t hex_lut[16] __attribute__((aligned(16))) = {
        '0','1','2','3','4','5','6','7',
        '8','9','a','b','c','d','e','f'
    };

    uint8x16_t raw = vld1q_u8((const uint8_t *)iid);

    // Nibble extraction on RAW bytes (no reorder)
    uint8x16_t hi = vshrq_n_u8(raw, 4);
    uint8x16_t lo = vandq_u8(raw, vdupq_n_u8(0x0F));
    uint8x16_t lut = vld1q_u8(hex_lut);
    uint8x16_t hi_hex = vqtbl1q_u8(lut, hi);
    uint8x16_t lo_hex = vqtbl1q_u8(lut, lo);

    // Zip produces hex pairs in MEMORY order (not canonical)
    uint8x16x2_t zipped = vzipq_u8(hi_hex, lo_hex);
    // zipped.val[0] = {h0,l0, h1,l1, h2,l2, h3,l3, h4,l4, h5,l5, h6,l6, h7,l7}
    // zipped.val[1] = {h8,l8, h9,l9, h10,l10, ..., h15,l15}
    // Byte order: 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15

    // Scatter tables encode little-endian reversal for Data1/Data2/Data3:
    // Data1: bytes 3,2,1,0 → zipped positions 6,7,4,5,2,3,0,1
    // Data2: bytes 5,4 → zipped positions 10,11,8,9
    // Data3: bytes 7,6 → zipped positions 14,15,12,13
    // Data4: bytes 8-15 in order → zipped positions 16+: 0,1,2,3,...

    // scatter1 (output positions 0-15):
    // Data1: z[6],z[7],z[4],z[5],z[2],z[3],z[0],z[1]
    // '-'
    // Data2: z[10],z[11],z[8],z[9]
    // '-'
    // Data3 start: z[14],z[15]
    static const uint8_t s1[16] __attribute__((aligned(16))) = {
         6,  7,  4,  5,  2,  3,  0,  1,
        32, 10, 11,  8,  9, 32, 14, 15
    };

    // scatter2 (output positions 16-31):
    // Data3 end: z[12],z[13]
    // '-'
    // Data4[0:2]: zipped.val[1][0..3] = indices 16..19
    // '-'
    // Data4[2:8] start: zipped.val[1][4..11] = indices 20..27
    static const uint8_t s2[16] __attribute__((aligned(16))) = {
        12, 13, 32, 16, 17, 18, 19, 32,
        20, 21, 22, 23, 24, 25, 26, 27
    };

    uint8x16x3_t tbl = { zipped.val[0], zipped.val[1], vdupq_n_u8('-') };

    vst1q_u8((uint8_t *)out,        vqtbl3q_u8(tbl, vld1q_u8(s1)));
    vst1q_u8((uint8_t *)(out + 16), vqtbl3q_u8(tbl, vld1q_u8(s2)));

    // Tail: hex[28..31] from zipped.val[1] lanes 12-15
    uint32_t tail;
    vst1q_lane_u32(&tail, vreinterpretq_u32_u8(zipped.val[1]), 3);
    memcpy(out + 32, &tail, 4);
    out[36] = '\0';
}
