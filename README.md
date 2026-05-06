# guid-race: 1 Billion GUID-to-String Conversions Per Second

A response to [Dave Plummer's challenge](https://www.youtube.com/watch?v=VYTF4KIF2z0) from *Dave's Garage*:

> "If you can think of a faster way to do it, let me know in the comments."

**We found one. It's 5x faster than Dave's nibble walker on ARM, and ~180x faster than `sprintf`.**

The secret: ARM NEON's `vqtbl3q` instruction builds the entire output string -- hex characters, hyphens, and all -- in a single 3-register table lookup. Hyphens aren't checked or inserted; they're *free*, just another index in the scatter table.

## Results

### Apple M4 (ARM64 NEON)

| Implementation | Median (ns) | vs Dave | vs sprintf | ops/sec |
|---|---:|---:|---:|---:|
| **neon_scatter** | **1.03** | **5.5x** | **181x** | **~970M** |
| neon_fused | 1.04 | 5.5x | 179x | ~960M |
| neon_ultimate | 1.13 | 5.0x | 166x | ~885M |
| neon_arith | 1.50 | 3.7x | 125x | ~667M |
| neon_simd | 1.89 | 2.9x | 98x | ~530M |
| lookup16 | 3.40 | 1.6x | 55x | ~294M |
| **dave_original** | **5.55** | **1.0x** | **33x** | **~180M** |
| unrolled | 6.48 | 0.86x | 29x | ~154M |
| arithmetic | 6.35 | 0.88x | 29x | ~157M |
| swar | 7.07 | 0.79x | 27x | ~141M |
| sprintf | 188 | 0.03x | 1.0x | ~5.3M |

> Methodology: 10M iterations x 11 samples, median reported. 256 random GUIDs cycled through (warm L1). Apple M4, clang -O3 -march=native.

## How It Works

### Dave's Original (the baseline to beat)

From the video, Dave's optimized version replaces `sprintf` with a nibble walker:

```c
static const unsigned char order[16] = {
    3,2,1,0,  5,4,  7,6,  8,9,10,11,12,13,14,15
};
for (i = 0; i < 16; i++) {
    unsigned char b = p[order[i]];
    out[j++] = hex[b >> 4];
    out[j++] = hex[b & 0xF];
    if (j == 8 || j == 13 || j == 18 || j == 23)
        out[j++] = '-';
}
```

This is clean, correct, and ~33x faster than `sprintf`. But it has three costs:
1. **Two table lookups per byte** (32 total)
2. **A branch for hyphen insertion** (checked 16 times, taken 4 times)
3. **Loop overhead** (16 iterations with serial dependency on `j`)

### The Winning Approach: NEON Scatter (`neon_scatter`)

Process all 16 bytes in parallel using ARM NEON vector instructions:

```
GUID bytes ──> [reorder] ──> [split nibbles] ──> [hex lookup] ──> [zip] ──> [scatter+hyphens] ──> output
   16 B          tbl(1)       shr+and(2)          tbl(2)         zip(2)      tbl3+store(4)
```

The key insight is `vqtbl3q_u8` -- a 3-register table lookup that acts as a programmable byte scatter. We build a table of `{hex_chars_lo, hex_chars_hi, all_hyphens}` and use pre-computed index vectors to place everything in one shot:

```c
// scatter1 picks hex chars from registers 0-1 and hyphens from register 2
static const uint8_t scatter1[16] = {
     0,  1,  2,  3,  4,  5,  6,  7,   // 8 hex chars from Data1
    32,  8,  9, 10, 11, 32, 12, 13    // hyphens at 8,13; hex chars between
};
//  ^-- index 32 = hyphen register     ^-- another hyphen

uint8x16x3_t tbl = { zipped.val[0], zipped.val[1], vdupq_n_u8('-') };
vst1q_u8(out,      vqtbl3q_u8(tbl, scatter1));  // output[0..15]
vst1q_u8(out + 16, vqtbl3q_u8(tbl, scatter2));  // output[16..31]
```

**Hyphens emerge naturally from the scatter topology** -- exactly the "elegant branchless trick" Dave was hoping existed.

### The Assembly (17 Instructions Total)

Clang -O3 compiles `neon_scatter` to just 17 ARM instructions:

```asm
ldr    q0, [x0]              ; load 16-byte GUID
ldr    q1, [byte_order]      ; load endian-swap table
tbl    v0, {v0}, v1          ; reorder bytes
ushr   v1, v0, #4            ; high nibbles
movi   v2, #15
and    v0, v0, v2            ; low nibbles
ldr    q2, [hex_lut]         ; '0'..'f' lookup table
tbl    v1, {v2}, v1          ; high nibbles -> hex chars
tbl    v0, {v2}, v0          ; low nibbles -> hex chars
zip1   v2, v1, v0            ; interleave first 16
zip2   v3, v1, v0            ; interleave last 16
movi   v4, #45               ; '-' in all lanes
ldr    q0, [scatter1]        ; output layout table
tbl    v0, {v2,v3,v4}, v0    ; build chunk 1 with hyphens
ldr    q1, [scatter2]        ; output layout table
tbl    v1, {v2,v3,v4}, v1    ; build chunk 2 with hyphens
stp    q0, q1, [x1]          ; store both chunks (32 bytes!)
; + 3 instructions for tail (4 hex chars + null terminator)
```

The compiler even merges both 16-byte stores into a single `stp` (store pair) instruction.

## All Implementations

| # | Name | Technique | Platform |
|---|---|---|---|
| 1 | `sprintf` | Standard library `snprintf` with format string | All |
| 2 | `dave_original` | Dave's nibble walker from the video | All |
| 3 | `lookup16` | 256-entry uint16 table (1 lookup/byte vs 2) | All |
| 4 | `unrolled` | Fully unrolled, zero branches, direct stores | All |
| 5 | `arithmetic` | Branchless `nibble + '0' + 39*(nibble >= 10)` | All |
| 6 | `swar` | 32-bit packed stores, arithmetic hex conversion | All |
| 7 | `neon_simd` | Basic NEON vectorized with temp buffer | ARM64 |
| 8 | `neon_tbl2` | NEON with direct lane stores, no temp buffer | ARM64 |
| 9 | `neon_scatter` | **NEON vqtbl3q scatter (winner)** | ARM64 |
| 10 | `neon_arith` | NEON with arithmetic hex (no LUT) | ARM64 |
| 11 | `neon_ultimate` | vqtbl4q 4-register, overlapping stores | ARM64 |
| 12 | `neon_fused` | Byte reorder fused into scatter tables | ARM64 |
| 13 | `ssse3_scatter` | x86 pshufb scatter (same principle as NEON) | x86_64 |
| 14 | `sse2_basic` | SSE2 arithmetic hex, no pshufb required | x86_64 |

## Surprising Findings

1. **Scalar unrolled is *slower* than Dave's loop.** The M4's branch predictor handles Dave's `if` perfectly -- the pattern is fixed and short. Unrolling adds code size without reducing work.

2. **`vqtbl3q` beats `vqtbl4q`.** The 3-register table lookup has lower latency than the 4-register version on Apple Silicon. Using `zip` + `tbl3` beats skipping `zip` with `tbl4`.

3. **The hex LUT beats arithmetic.** Despite `neon_arith` avoiding a memory load, the `tbl` instruction used as a 16-entry lookup table is faster than the `vcgt` + `vand` + `vadd` arithmetic chain.

4. **`lookup16` is the best scalar approach.** A 256-entry uint16 table (512 bytes, fits in L1) halves the lookup count and enables 16-bit stores. This is the approach to use if you can't use SIMD.

## Building

### macOS / Linux (ARM64)
```bash
make          # uses Makefile
./guid_race
```

### CMake (cross-platform)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/guid_race
```

### Windows (MSVC)
```cmd
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
build\Release\guid_race.exe
```

## Context

In the video, Dave tells the story of optimizing `IIDtoString` in the Windows COM runtime during his early days on the OLE team at Microsoft. The original used `sprintf` with a format string -- clean and correct, but dragging a full formatting interpreter into a hot path that ran millions of times during COM initialization, marshalling, and registry lookups.

Dave replaced it with a nibble walker: read each byte, index into a hex table twice (high nibble, low nibble), write the characters, check for hyphen positions. About 100x faster than `sprintf`, reviewed with a "cool, nice one," and absorbed into the machine.

30 years later, we have SIMD. The same insight Dave had -- "this is a fixed encoding problem, not a formatting problem" -- extends one step further: it's a *parallel* fixed encoding problem. Every byte is independent. Every nibble maps the same way. The output layout is constant. This is exactly what vector shuffle instructions were designed for.

## Prior Art

- [crashoz/uuid_v4](https://github.com/crashoz/uuid_v4) -- SSE4.1/AVX2 UUID library
- [zbjornson/fast-hex](https://github.com/zbjornson/fast-hex) -- AVX2 hex encoding
- [Daniel Lemire's hex encoding analysis](https://lemire.me/blog/2022/12/23/fast-base16-encoding/)
- [johnnylee-sde: Fast unsigned integer to hex string](https://johnnylee-sde.github.io/Fast-unsigned-integer-to-hex-string/)

## License

MIT
