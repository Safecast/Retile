#include "gbImage_png.h"

// ==============
// gbImage_png.c:
// ==============
//
// Reads/writes RGBA8888 buffers to PNG files on disk.
//
// Writes are optimized, and were improved by <1% when running
// pngcrush -rem alla -reduce -brute on the output for ~300k tiles.

#ifndef FORCE_INLINE
#  ifdef _MSC_VER
#    define FORCE_INLINE __forceinline
#  else
#    ifdef __GNUC__
#      define FORCE_INLINE inline __attribute__((always_inline))
#    else
#      define FORCE_INLINE inline
#    endif
#  endif
#endif

// =========================
// SIMD vectorization notes:
// =========================
//
// - Some vectorization uses Apple's Accelerate framework (OS X / iOS only).
// - The Accelerate framework works on Intel/ARM.
// - If the Accelerate framework is not available, equivalent scalar fallback
//   code is used.
// - The rest of the vectorization uses ARM NEON intrinsics.
// - NEON intrinsics *will* work on Intel platforms provided translation macros
//   from NEONvsSSE_5.h are present.
// - NEON -> SSE conversion is not 100%.  Verify the translation macros are
//   as expected, especially for 8-bit types, unsigned types, and bitshifts
//   involving another vector.








// =======================
// _IsPaletteGrayscalePNG:
// =======================
//
// Returns true if all the png_color palette entries in src are equal to each
// other.  This is a more precise answer than may be necessary (human perception
// of grayscale may not be so precise).
//
// This is used to determine if the PNG should be PNG_COLOR_TYPE_GRAY or
// instead of PNG_COLOR_TYPE_PALETTE.
//
// Note:
//
// Currently PNG_COLOR_TYPE_GRAY_ALPHA is not used, as it always 16 bits per
// pixel, but can be represented by PNG_COLOR_TYPE_PALETTE at 8 bits per pixel.
//
// This could be a further optimization depending on the size of the image and
// and number of palette entries.
//
// Minimum size of IDAT + directly supporting chunks:
//                                                               Indexed8 ->
// Pixels   Colors     GA     PAL       P_IDAT    tRNS    sPLT   Indexed4/2/1
// ======   ======   ====   =====       ======   =====   =====   ============
//      1        1      2      29 =         1    12+ 1   12+ 3 -            0   (Indexed8)
//      2        2      4      32 =         2    12+ 2   12+ 6 -            1   (Indexed4)
//      8        2     16      33 =         8  + 12+ 2 + 12+ 6 -            7   (Indexed1)
//     16        2     32      34 =        16  + 12+ 2 + 12+ 6 -           14   (Indexed1)  <-- BREAKPOINT:   2 colors
//     24        4     48      46 =        24  + 12+ 4 + 12+12 -           18   (Indexed2)  <-- BREAKPOINT:   4 colors
//     58       16    116     117 =        64  + 12+16 + 12+48 -           32   (Indexed4)  <-- BREAKPOINT:  16 colors
//    152       32    304     304 =       152  + 12+32 + 12+96 -            0   (Indexed8)  <-- BREAKPOINT:  32 colors
//    280       64    560     560 =       280  + 12+64 +12+192 -            0   (Indexed8)  <-- BREAKPOINT:  64 colors
//    536      128   1072    1072 =       536  +12+128 +12+384 -            0   (Indexed8)  <-- BREAKPOINT: 128 colors
//   1048      256   2096    2096 =      1048  +12+256 +12+768 -            0   (Indexed8)  <-- BREAKPOINT: 256 colors
//
//
static inline bool _IsPaletteGrayscalePNG(const png_color* src,
                                          const size_t     n)
{
    bool isGrayscale = true;
    
    for (size_t i = 0; i < n; i++)
    {
        if (   src[i].red != src[i].green
            || src[i].red != src[i].blue)
        {
            isGrayscale = false;
            break;
        }//if
    }//for
    
    return isGrayscale;
}//_IsPaletteGrayscalePNG



// =======================
// _Indexed8ToPlanar8_PNG:
// =======================
//
// If the resulting palette was entirely grayscale, rewrite the idat indices
// with the luma value rather than the palette index.
//
// In-place only.
//
// Note this should only be used if there is no alpha channel present.
//
// If there is an alpha channel, it is likely indexed color mode would be
// superior to PNG_COLOR_TYPE_GRAY_ALPHA which is 16 bits per pixel.
//
static inline void _Indexed8ToPlanar8_PNG(const png_color* palette,
                                          uint8_t*         idat,
                                          const size_t     width,
                                          const size_t     height)
{
    for (size_t i = 0; i < width * height; i++)
    {
        idat[i] = palette[idat[i]].red;
    }//for
}//_Indexed8ToPlanar8_PNG


// =================================
// _Planar8ToPlanar4_InPlace_Scalar:
// =================================
//
// If there were <= 16 colors in the palette, use 4 bits per pixel instead of 8.
//
// Only works on widths evenly divisible by two.
// (TODO: per-row with last row byte padding to work on odd widths)
//
// In-place only.  Scalar version.
//
// Note this may not actually be useful, as byte-level data tends to compress
// better.
//
static FORCE_INLINE void _Planar8ToPlanar4_InPlace_scalar(uint8_t*     src,
                                                          const size_t width,
                                                          const size_t height,
                                                          const size_t rowBytes)
{
    size_t x;
    size_t y;
    size_t y_rb;
    size_t destIdx = 0;
    
    for (y = 0; y < height; y++)
    {
        y_rb = y * rowBytes;
        
        for (x = 0; x < width; x += 2)
        {
            src[destIdx++] = (src[y_rb+x] << 4) | src[y_rb+x+1];
        }//for
    }//for
}//_Planar8ToPlanar4_InPlace_scalar



// ===============================
// _Planar8ToPlanar4_InPlace_NEON:
// ===============================
//
// If there were <= 16 colors in the palette, use 4 bits per pixel instead of 8.
//
// Only works on widths evenly divisible by two.
// (TODO: per-row with last row byte padding to work on odd widths)
//
// In-place only.  NEON/SSE version, approx 1.8x faster than scalar w/ SSE.
//
// Note this may not actually be useful, as byte-level data tends to compress
// better.
//
static FORCE_INLINE void _Planar8ToPlanar4_InPlace_NEON(uint8_t*     src,
                                                        const size_t width,
                                                        const size_t height,
                                                        const size_t rowBytes)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    size_t       y;
    size_t       destIdx = 0;
    uint8x16x2_t src_u8x16x2;
    
    for (y = 0; y < height * rowBytes; y += 32)
    {
        src_u8x16x2 = vld2q_u8( &(src[y]) );
        src_u8x16x2.val[0] = vshlq_n_u8(src_u8x16x2.val[0], 4);
        src_u8x16x2.val[0] = vorrq_u8(src_u8x16x2.val[0], src_u8x16x2.val[1]);
        vst1q_u8( &(src[destIdx]), src_u8x16x2.val[0]);
        
        destIdx += 16;
    }//for
#endif
}//_Planar8ToPlanar4_InPlace_NEON

// ==========================
// _Planar8ToPlanar4_InPlace:
// ==========================
//
// If there were <= 16 colors in the palette, use 4 bits per pixel instead of 8.
//
// Only works on widths evenly divisible by two.
// (TODO: per-row with last row byte padding to work on odd widths)
//
// In-place only.  Main entry point.
//
// Note this may not actually be useful, as byte-level data tends to compress
// better.
//
static inline void _Planar8ToPlanar4_InPlace(uint8_t*     src,
                                             const size_t width,
                                             const size_t height,
                                             const size_t rowBytes)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    if (height * rowBytes > 31)
    {
        _Planar8ToPlanar4_InPlace_NEON(src, width, height, rowBytes);
    }//if
    else
    {
        _Planar8ToPlanar4_InPlace_scalar(src, width, height, rowBytes);
    }//else
#else
    _Planar8ToPlanar4_InPlace_scalar(src, width, height, rowBytes);
#endif
}//_Planar8ToPlanar4_InPlace




// =================================
// _Planar8ToPlanar2_InPlace_Scalar:
// =================================
//
// If there were <= 4 colors in the palette, use 2 bits per pixel instead of 8.
//
// Only works on widths evenly divisible by two.
// (TODO: per-row with last row byte padding to work on odd widths)
//
// In-place only.  Scalar version.
//
// Note this may not actually be useful, as byte-level data tends to compress
// better.
//
static FORCE_INLINE void _Planar8ToPlanar2_InPlace_scalar(uint8_t*     src,
                                                          const size_t width,
                                                          const size_t height,
                                                          const size_t rowBytes)
{
    size_t x;
    size_t y;
    size_t y_rb;
    size_t destIdx = 0;
    
    for (y = 0; y < height; y++)
    {
        y_rb = y * rowBytes;
        
        for (x = 0; x < width; x += 4)
        {
            src[destIdx++] = (src[y_rb+x] << 6) | (src[y_rb+x+1] << 4) | (src[y_rb+x+2] << 2) | src[y_rb+x+3];
        }//for
    }//for
}//_Planar8ToPlanar2_InPlace_scalar


// ===============================
// _Planar8ToPlanar2_InPlace_NEON:
// ===============================
//
// If there were <= 4 colors in the palette, use 2 bits per pixel instead of 8.
//
// Only works on widths evenly divisible by two.
// (TODO: per-row with last row byte padding to work on odd widths)
//
// In-place only.  NEON/SSE version, approx 1.8x faster than scalar w/ SSE.
//
// Note this may not actually be useful, as byte-level data tends to compress
// better.
//
static FORCE_INLINE void _Planar8ToPlanar2_InPlace_NEON(uint8_t*     src,
                                                        const size_t width,
                                                        const size_t height,
                                                        const size_t rowBytes)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    size_t       y;
    size_t       destIdx = 0;
    uint8x16x4_t src_u8x16x4;
    
    for (y = 0; y < height * rowBytes; y += 64)
    {
        src_u8x16x4 = vld4q_u8( &(src[y]) );
        src_u8x16x4.val[0] = vshlq_n_u8(src_u8x16x4.val[0], 6);
        src_u8x16x4.val[1] = vshlq_n_u8(src_u8x16x4.val[1], 4);
        src_u8x16x4.val[2] = vshlq_n_u8(src_u8x16x4.val[2], 2);
        
        src_u8x16x4.val[0] = vorrq_u8(src_u8x16x4.val[0], src_u8x16x4.val[1]);
        src_u8x16x4.val[0] = vorrq_u8(src_u8x16x4.val[0], src_u8x16x4.val[2]);
        src_u8x16x4.val[0] = vorrq_u8(src_u8x16x4.val[0], src_u8x16x4.val[3]);
        
        vst1q_u8( &(src[destIdx]), src_u8x16x4.val[0]);
        
        destIdx += 16;
    }//for
#endif
}//_Planar8ToPlanar2_InPlace_NEON


// ===============================
// _Planar8ToPlanar2_InPlace_NEON:
// ===============================
//
// If there were <= 4 colors in the palette, use 2 bits per pixel instead of 8.
//
// Only works on widths evenly divisible by two.
// (TODO: per-row with last row byte padding to work on odd widths)
//
// In-place only.  Main entry point.
//
// Note this may not actually be useful, as byte-level data tends to compress
// better.
//
static inline void _Planar8ToPlanar2_InPlace(uint8_t*     src,
                                             const size_t width,
                                             const size_t height,
                                             const size_t rowBytes)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    if (height * rowBytes > 63)
    {
        _Planar8ToPlanar2_InPlace_NEON(src, width, height, rowBytes);
    }//if
    else
    {
        _Planar8ToPlanar2_InPlace_scalar(src, width, height, rowBytes);
    }//else
#else
    _Planar8ToPlanar2_InPlace_scalar(src, width, height, rowBytes);
#endif
}//_Planar8ToPlanar2_InPlace




// ===============================
// _Planar8ToPlanar2_InPlace_NEON:
// ===============================
//
// If there were <= 2 colors in the palette, use 1 bit per pixel instead of 8.
//
// Only works on widths evenly divisible by two.
// (TODO: per-row with last row byte padding to work on odd widths)
//
// In-place only.  Scalar version and main entry point.
//
// Note this may not actually be useful, as byte-level data tends to compress
// better.
//
static inline void _Planar8ToPlanar1_InPlace(uint8_t*     src,
                                             const size_t width,
                                             const size_t height,
                                             const size_t rowBytes)
{
    size_t x;
    size_t y;
    size_t y_rb;
    size_t destIdx = 0;
    
    for (y = 0; y < height; y++)
    {
        y_rb = y * rowBytes;
        
        for (x = 0; x < width; x += 8)
        {
            src[destIdx++] = (src[y_rb+x]   << 7) | (src[y_rb+x+1] << 6) | (src[y_rb+x+2] << 5) | (src[y_rb+x+3] << 4)
                           | (src[y_rb+x+4] << 3) | (src[y_rb+x+5] << 2) | (src[y_rb+x+6] << 1) |  src[y_rb+x+7];
        }//for
    }//for
}//_Planar8ToPlanar1_InPlace


// ===================================
// _Indexed8ToIndexed124_IfNeeded_PNG:
// ===================================
//
// For a PNG with smaller palette of 16, 4, or 2 colors (or less), reduce the
// number of bits per pixel in the indices (idat) chunk.
//
static FORCE_INLINE void _Indexed8ToIndexed124_IfNeeded_PNG(uint8_t*     src,
                                                            const size_t width,
                                                            const size_t height,
                                                            int*         bitsPerComp,
                                                            const size_t color_n)
{
    if (width % 2 == 0) // todo: make bit->byte work on odd widths
    {
        if (color_n <= 2 && width > 8)
        {
            _Planar8ToPlanar1_InPlace(src, width, height, width);    // Indexed8 -> Indexed1
            *bitsPerComp = 1;
        }//if
        else if (color_n <= 4 && width > 4)
        {
            _Planar8ToPlanar2_InPlace(src, width, height, width);    // Indexed8 -> Indexed2
            *bitsPerComp = 2;
        }//if
        else if (color_n <= 16 && width > 1)
        {
            _Planar8ToPlanar4_InPlace(src, width, height, width);    // Indexed8 -> Indexed4
            *bitsPerComp = 4;
        }//if
    }//if
}//_Indexed8ToIndexed124_IfNeeded_PNG

// ===============================
// _IndexedToPlanar8_IfNeeded_PNG:
// ===============================
//
// For a PNG with a palette of entirely grayscale entries, and with a palette
// of > 16 entries, drop indexed color mode entirely.
//
// Note at the moment, indexed color mode is kept for grayscale palettes with
// < 16 entries.  This is arguable, and comes down to loss of precision vs.
// potential size reduction.  For example, a palette of luma=128 and luma=0
// would always be expanded to luma=255 and luma=0 @ 1 bpp.  Thus, a further
// optimization would threshold Indexed8 -> Planar1/2/4 depending upon the
// specifics of the palette.
//
static FORCE_INLINE void _IndexedToPlanar8_IfNeeded_PNG(uint8_t*         idxs,
                                                        const size_t     width,
                                                        const size_t     height,
                                                        const int        bitsPerComp,
                                                        const png_color* palette,
                                                        const size_t     color_n,
                                                        const bool       isOpaque,
                                                        int*             png_color_type)
{
    if (isOpaque && bitsPerComp == 8 && _IsPaletteGrayscalePNG(palette, color_n))
    {
        *png_color_type = PNG_COLOR_TYPE_GRAY;                  // Indexed8 -> Planar8
        
        _Indexed8ToPlanar8_PNG(palette, idxs, width, height);
    }//if
}//_Indexed8ToPlanar8_IfNeeded_PNG







// ====================
// _svceqqi_u32_scalar:
// ====================
//
// Scalar-vector compare equality and find index, scalar version.
//
// (don't ask what happened to the SIMD version)
//
static FORCE_INLINE size_t _svceqqi_u32_scalar(const uint32_t* src,
                                               const uint32_t  x,
                                               const size_t    n)
{
    size_t idx = UINT32_MAX;
    
    for (size_t i = 0; i < n; i++)
    {
        if (src[i] == x)
        {
            idx = i;
            break;
        }//if
    }//for
    
    return idx;
}//_svceqqi_u32_scalar


// ====================
// _vscgtsubcvtu8_NEON:
// ====================
//
// Vector-scalar uint16_t -> uint8_t narrowing move with conditional subtract
// (NEON version).
//
static FORCE_INLINE void _vscgtsubcvtu8_NEON(const uint16_t* src,
                                             const uint16_t  thres,
                                             const uint16_t  x,
                                             uint8_t*        dest,
                                             const size_t    n)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    uint16x8_t s_u16x8;
    uint16x8_t x_u16x8 = vdupq_n_u16(x);
    uint16x8_t t_u16x8 = vdupq_n_u16(thres);
    uint16x8_t n_u16x8;
    uint16x8_t u_u16x8;
    uint8x8_t   d_u8x8;
    
    for (size_t i = 0; i < n; i += 8)
    {
        s_u16x8 = vld1q_u16( &(src[i]) );
        
        n_u16x8 =  vcgt_u16(s_u16x8, t_u16x8);      // n = s > t ? 0xFFFF : 0x0000
        u_u16x8 = vandq_u16(x_u16x8, n_u16x8);      // u = n & x
        n_u16x8 = vsubq_u16(s_u16x8, u_u16x8);      // n = s - u
        d_u8x8  = vmovn_u16(n_u16x8);               // d = (uint8_t)n
        
        vst1_u8( &(dest[i]), d_u8x8);
    }//for
    
#endif
}//_vscgtsubcvtu8_NEON

// ======================
// _vscgtsubcvtu8_scalar:
// ======================
//
// Vector-scalar uint16_t -> uint8_t narrowing move with conditional subtract
// (scalar version).
//
static FORCE_INLINE void _vscgtsubcvtu8_scalar(const uint16_t* src,
                                               const uint16_t  thres,
                                               const uint16_t  x,
                                               uint8_t*        dest,
                                               const size_t    n)
{
    for (size_t i = 0; i < n; i++)
    {
        dest[i] = src[i] > thres ? src[i] - x
                                 : src[i];
    }//for
}//_vscgtsubcvtu8_scalar


// ===============
// _vscgtsubcvtu8:
// ===============
//
// Vector-scalar uint16_t -> uint8_t narrowing move with conditional subtract
// (main entry point).
//
static inline void _vscgtsubcvtu8(const uint16_t* src,
                                  const uint16_t  thres,
                                  const uint16_t  x,
                                  uint8_t*        dest,
                                  const size_t    n)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    if (n > 7)
    {
        size_t lastCleanWidth = n - n % 8;
        
        _vscgtsubcvtu8_NEON(src, thres, x, dest, lastCleanWidth);
        
        if (n % 8 != 0)
        {
            _vscgtsubcvtu8_scalar(src  + lastCleanWidth,
                                  thres,
                                  x,
                                  dest + lastCleanWidth,
                                  n    - lastCleanWidth);
        }//if
    }//if
    else
    {
        _vscgtsubcvtu8_scalar(src, thres, x, dest, n);
    }//else
#else
    _vscgtsubcvtu8_scalar(src, thres, x, dest, n);
#endif
}//_vscgtsubcvtu8




static FORCE_INLINE void _RGBA8888_to_RGB888_scalar(const uint8_t*  src,
                                                    uint8_t*        dest,
                                                    const size_t    width,
                                                    const size_t    height)
{
    const size_t n       = width * height * 4;
    size_t       destIdx = 3;
    
    if (n > 0)
    {
        dest[0] = src[0];
        dest[1] = src[1];
        dest[2] = src[2];
    }//if
    
    for (size_t i = 4; i < n; i += 4)
    {
        dest[destIdx  ] = src[i  ];
        dest[destIdx+1] = src[i+1];
        dest[destIdx+2] = src[i+2];
        
        destIdx += 3;
    }//for
}//_RGBA8888_to_RGB888_scalar

static FORCE_INLINE void _RGBA8888_to_RGB888_vImage(const uint8_t* src,
                                                    uint8_t*       dest,
                                                    const size_t   width,
                                                    const size_t   height)
{
#ifdef __ACCELERATE__
    vImage_Buffer viDataSrc  = { (void*)src,  height, width, width * 4 };
    vImage_Buffer viDataDest = { (void*)dest, height, width, width * 3 };
    
    vImageConvert_RGBA8888toRGB888(&viDataSrc, &viDataDest, kvImageDoNotTile);
#endif
}//_RGBA8888_to_RGB888_vImage


// ====================
// _RGBA8888_to_RGB888:
// ====================
//
// Converts interleaved RGBA8888 buffer src to RGB888 dest, discarding
// the alpha channel.
//
// The new bytes are compacted and all slack space will be at the end of buffer
// dest.
//
// This will work in-place.
//
static inline void _RGBA8888_to_RGB888(const uint8_t* src,
                                       uint8_t*       dest,
                                       const size_t   width,
                                       const size_t   height)
{
#ifdef __ACCELERATE__
    if (width * height > 16)
    {
        _RGBA8888_to_RGB888_vImage(src, dest, width, height);
    }//if
    else
    {
        _RGBA8888_to_RGB888_scalar(src, dest, width, height);
    }//else
#else
    _RGBA8888_to_RGB888_InPlace_scalar(src, width, height);
#endif
}//_RGBA8888_to_RGB888


// ======================================
// _RGBA8888_opaque_u32_to_png_color_a08:
// ======================================
//
// Converts RGBA8888 src into an array of pointers to png_color structs dest_rgb,
// where it is known that src only contains opaque pixels.
//
static FORCE_INLINE void _RGBA8888_opaque_u32_to_png_color_a08(const uint32_t* src,
                                                               png_color*      dest_rgb,
                                                               const size_t    n)
{
    if (sizeof(png_color) != 3) // in case assumption goes bad.
    {
        for (size_t i = 0; i < n; i++)
        {
            dest_rgb[i].red   = ((src[i]) & 0x000000FF);
            dest_rgb[i].green = ((src[i]) & 0x0000FF00) >> 8;
            dest_rgb[i].blue  = ((src[i]) & 0x00FF0000) >> 16;
        }//for
    }//if
    else
    {
        _RGBA8888_to_RGB888((uint8_t*)src, (uint8_t*)dest_rgb, n, 1);
    }//else
}//_RGBA8888_opaque_u32_to_png_color_a08


// ===============================
// _RGBA8888_u32_to_png_color_a08:
// ===============================
//
// Converts RGBA8888 src into an array of pointers to png_color structs dest_rgb,
// inferring from whether or not a NULL pointer was passed for parameter dest_a
// if the alpha channel should be extracted or not.
//
// This is used to convert a RGBA8888 palette stack into structures suitable
// for saving a PNG with PNG_COLOR_TYPE_PALETTE.  dest_a becomes the tRNS chunk.
//
static inline void _RGBA8888_u32_to_png_color_a08(const uint32_t* src,
                                                  png_color*      dest_rgb,
                                                  uint8_t*        dest_a,
                                                  const size_t    n)
{
    if (dest_a != NULL)
    {
        for (size_t i = 0; i < n; i++)
        {
            dest_rgb[i].red   = ((src[i]) & 0x000000FF);
            dest_rgb[i].green = ((src[i]) & 0x0000FF00) >> 8;
            dest_rgb[i].blue  = ((src[i]) & 0x00FF0000) >> 16;
            dest_a[i]         =  (src[i])               >> 24;
        }//for
    }//if
    else
    {
        _RGBA8888_opaque_u32_to_png_color_a08(src, dest_rgb, n);
    }//else
}//_RGBA8888_u32_to_png_color_a08


// =================================
// _PalettePush_UpdateIdxs_RGBA8888:
// =================================
//
// Helper for _MakePaletteFromRGBA8888.
//
// Pushes RGBA value onto palette stack, updates corresponding value idat_idx,
// and increments pal_idx.
//
// If RGBA value was already on stack, just updates idat_idx.
//
static inline void _PalettePush_UpdateIdxs_RGBA8888(const uint32_t rgba,
                                                    uint32_t*      palette,
                                                    uint16_t*      idat_idx,
                                                    size_t*        pal_idx,
                                                    const uint16_t idat_idx_offset)
{
    size_t existsIdx = _svceqqi_u32_scalar(palette, rgba, *pal_idx);
    
    if (existsIdx != UINT32_MAX)
    {
        *idat_idx         = existsIdx + idat_idx_offset;
    }//if
    else
    {
        *idat_idx         = *pal_idx  + idat_idx_offset;
        palette[*pal_idx] = rgba;

        *pal_idx          = *pal_idx  + 1;
    }//else
}//_PalettePush_UpdateIdxs_RGBA8888

// ==================
// _IsOpaqueRGBA8888:
// ==================
//
// Returns whether or not the interleaved RGBA8888 image src has an entirely
// opaque (0xFF) alpha channel.
//
// Used to determine if the image can be safely converted to RGB888 if the
// primary conversion to indexed color fails because there are too many colors
// found.
//
static inline bool _IsOpaqueRGBA8888(const uint8_t* src,
                                     const size_t   n)
{
    bool            isOpaque = true;
    const uint32_t* src_u32  = (uint32_t*)src;
    
    for (size_t i = 0; i < n; i++)
    {
        if (src_u32[i] >> 24 != 0xFF)   // LSB
        {
            isOpaque = false;
            break;
        }//if
    }//for
    
    return isOpaque;
}//_IsOpaqueRGBA8888






// =========================
// _MakePaletteFromRGBA8888:
// =========================
//
// Converts RGBA8888 src to palette rgbOut, alpha channel aOut (non-opaque first
// sorting), and transforms the source pixels into image data indices idxOut if
// <= 256 distinct colors.
//
// Note this is a lossless palletization ONLY.  No median cuts, dithering, etc.
//
static inline size_t _MakePaletteFromRGBA8888(uint8_t*     src,
                                              const size_t width,
                                              const size_t height,
                                              png_color**  rgbOut,
                                              uint8_t**    aOut,
                                              uint8_t**    idxOut,
                                              size_t*      nonOpaque_n,
                                              size_t*      lastScanIdx)
{
    const size_t        n = width * height;
    uint32_t*     src_u32 = (uint32_t*)src;                                 // oh noes, someone call the programming police
    uint16_t*    idxs_u16 = malloc(sizeof(uint16_t) * n);                   // widen to 16 bits to encode threshold offset
    uint8_t*     idxs_u08 = NULL;
    uint8_t*           _a = NULL;                                           // Planar8 alpha values for palette
    png_color*       _pal = NULL;                                           // Palette RGB888 tuples
    size_t     palIdx_IsO = 0;
    size_t     palIdx_NoO = 0;
    size_t              i;
    
    uint32_t  tempPal_IsO[257] __attribute__ ((aligned(16)));
    uint32_t  tempPal_NoO[257] __attribute__ ((aligned(16)));
    
    for (i = 0; i < n; i++)
    {
        if (src_u32[i] >> 24 != 0xFF)   // LSB
        {
            _PalettePush_UpdateIdxs_RGBA8888(src_u32[i], tempPal_NoO, &( idxs_u16[i] ), &palIdx_NoO, 0);
        }//if
        else
        {
            // add 512 to these indices so can identify and fix later
            _PalettePush_UpdateIdxs_RGBA8888(src_u32[i], tempPal_IsO, &( idxs_u16[i] ), &palIdx_IsO, 512);
        }//if
        
        if (palIdx_NoO + palIdx_IsO > 256)
        {
            break;
        }//if
    }//for
    
    // ========= 2. RGBA8888 -> Indexed8 =========
    if (palIdx_NoO + palIdx_IsO <= 256)
    {
        idxs_u08 = malloc(sizeof(uint8_t)   * n);
        _pal     = malloc(sizeof(png_color) * (palIdx_NoO + palIdx_IsO));
        
        // fix up any opaque indices >= 512 for however many non-opaque entries there actually were
        _vscgtsubcvtu8(idxs_u16,
                       511, 512 - palIdx_NoO,
                       idxs_u08,
                       n);
        
        // RGBA8888 -> RGB888, A8
        if (palIdx_NoO > 0)
        {
            _a = malloc(sizeof(uint8_t) * (palIdx_NoO + palIdx_IsO));
            
            _RGBA8888_u32_to_png_color_a08(tempPal_NoO,
                                           _pal,
                                           _a,
                                           palIdx_NoO);
            
            _RGBA8888_u32_to_png_color_a08(tempPal_IsO,
                                           _pal         + palIdx_NoO,
                                           _a           + palIdx_NoO,
                                           palIdx_IsO);
        }//if
        else // no alpha
        {
            _RGBA8888_u32_to_png_color_a08(tempPal_IsO,
                                           _pal,
                                           NULL,
                                           palIdx_IsO);
        }//else
    }//if
    
    free(idxs_u16);
    idxs_u16 = NULL;
    
    *nonOpaque_n      = palIdx_NoO; // 2014-07-31 ND: bugfix: NoO not IsO
    *rgbOut           = _pal;
    *aOut             = _a;
    *idxOut           = idxs_u08;
    *lastScanIdx      = i;          // ATTENTION!  Fix if adding another for loop!
    
    return palIdx_NoO + palIdx_IsO;
}//_MakePaletteFromRGBA8888







int gbImage_PNG_Write_RGBA8888(const char*  filename,
                               const size_t width,
                               const size_t height,
                               uint8_t*     src)
{
	int         code        = 0;
    int         result;
    bool        shouldWrite = true;
	png_structp png_ptr     = NULL;
	png_infop   info_ptr    = NULL;
	
	FILE*       fp          = fopen(filename, "wb");
    
    if (src == NULL)
    {
		fprintf(stderr, "gbImage_PNG_Write_RGBA8888: Can't write NULL src buffer for %s.\n", filename);
		code        = 1;
		shouldWrite = false;
    }//if
    
	if (fp == NULL)
    {
		fprintf(stderr, "gbImage_PNG_Write_RGBA8888: Could not open file %s for writing\n", filename);
		code        = 1;
		shouldWrite = false;
	}//if
    
    if (shouldWrite)
    {
        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        
        if (png_ptr == NULL)
        {
            fprintf(stderr, "gbImage_PNG_Write_RGBA8888: Could not allocate write struct\n");
            code        = 1;
            shouldWrite = false;
        }//if
    }//if

    if (shouldWrite)
    {
       	info_ptr = png_create_info_struct(png_ptr);
        
        if (info_ptr == NULL)
        {
            fprintf(stderr, "gbImage_PNG_Write_RGBA8888: Could not allocate info struct\n");
            code        = 1;
            shouldWrite = false;
        }//if
    }//if

    if (shouldWrite)
    {
        result = setjmp(png_jmpbuf(png_ptr));
        
        if (result)
        {
            fprintf(stderr, "gbImage_PNG_Write_RGBA8888: Error during png creation\n");
            code        = 1;
            shouldWrite = false;
        }//if
    }//if

    if (shouldWrite)
    {
        // <zlib>
        png_set_compression_level(png_ptr, 9);          // RLE=1
        png_set_compression_strategy(png_ptr, 0);       // RLE=3
        png_set_filter(png_ptr, 0, PNG_NO_FILTERS);     // filters are only useful for webpage gradients
        // </zlib>
        
        png_init_io(png_ptr, fp);
        
        int _png_color_type = PNG_COLOR_TYPE_RGBA;
        int bitsPerComp     = 8;
        size_t y;
        
        // 4 color types are supported and set by the next block:
        //
        // - 1. RGBA8888                32-bit color
        // - 2. RGB888                  24-bit color
        // - 3. Indexed8 w/ tRNS     -> 32-bit color
        // - 4. Indexed8             -> 24-bit color
        
        
        // <indexedColorQuant>
        const bool usePal  = true;
        bool       isOpaque;
        uint8_t*   _a      = NULL;
        uint8_t*   _i      = NULL;
        png_color* _p      = NULL;
        size_t     _nopIdx = UINT32_MAX;
        size_t     _last_i = 0;
        size_t     color_n = usePal ? _MakePaletteFromRGBA8888(src, width, height, &_p, &_a, &_i, &_nopIdx, &_last_i) : 9000;
        
        isOpaque = _a == NULL; // 2014-07-31 ND: bugfix: should not compare color_n, was causing all non-paletted RGBA->RGB conversions to fail.
        
        if (color_n <= 256)
        {
            _png_color_type = PNG_COLOR_TYPE_PALETTE;
            
            _Indexed8ToIndexed124_IfNeeded_PNG(_i, width, height, &bitsPerComp, color_n);
            
            _IndexedToPlanar8_IfNeeded_PNG(_i, width, height, bitsPerComp, _p, color_n, isOpaque, &_png_color_type);    // Indexed8 -> Planar8 (PNG_COLOR_TYPE_GRAY)
            
            if (_png_color_type == PNG_COLOR_TYPE_PALETTE)
            {
                png_set_PLTE(png_ptr, info_ptr, _p, (int)color_n);
                
                if (!isOpaque)
                {
                    png_set_tRNS(png_ptr, info_ptr, &(_a[0]), (int)_nopIdx, NULL);
                }//if
            }//if
            
            free(_p);
            _p = NULL;
            
            if (_a != NULL)
            {
                free(_a);
                _a = NULL;
            }//if
        }//if
        else if (isOpaque)
        {
            _png_color_type = _IsOpaqueRGBA8888(src + 4 * _last_i, width * height - _last_i) ? PNG_COLOR_TYPE_RGB : _png_color_type;
        }//else if
        // </indexedColorQuant>
        
        const int    write_bpp   = _png_color_type == PNG_COLOR_TYPE_RGBA ? 32 : _png_color_type == PNG_COLOR_TYPE_RGB ? 24 : bitsPerComp;
        const size_t rowBytes    = (width * write_bpp) >> 3;
        
        // <PNG_IHDR>
        png_set_IHDR(png_ptr,
                     info_ptr,
                     (uint32_t)width,
                     (uint32_t)height,
                     bitsPerComp,
                     _png_color_type,
                     PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_BASE,
                     PNG_FILTER_TYPE_BASE);

        png_write_info(png_ptr, info_ptr);
        
        setjmp(png_jmpbuf(png_ptr));
        // </PNG_IHDR>
        
        
        // <write>
        if (   _png_color_type == PNG_COLOR_TYPE_PALETTE
            || _png_color_type == PNG_COLOR_TYPE_GRAY)
        {
            for (y=0; y<height; y++)
            {
                png_write_row(png_ptr, &(_i[y*rowBytes]));
            }//for

            free(_i);
            _i = NULL;
        }//if
        else if (_png_color_type == PNG_COLOR_TYPE_RGBA)
        {
            for (y=0; y<height; y++)
            {
                png_write_row(png_ptr, &(src[y*rowBytes]));
            }//for
        }//else if
        else if (_png_color_type == PNG_COLOR_TYPE_RGB)
        {
            _RGBA8888_to_RGB888(src, src, width, height);
            
            for (y=0; y<height; y++)
            {
                png_write_row(png_ptr, &(src[y*rowBytes]));
            }//for
        }//else if
        
        setjmp(png_jmpbuf(png_ptr));
        
        png_write_end(png_ptr, NULL);
        // </write>
    }//if
    
	if (fp       != NULL) { fclose(fp);                                           }//if
	if (info_ptr != NULL) { png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);   }//if
	if (png_ptr  != NULL) { png_destroy_write_struct(&png_ptr, (png_infopp)NULL); }//if

	return code;
}//_WriteImagePNG











void gbImage_PNG_Read_RGBA8888(const char* filename,
                               uint32_t**  dest,
                               size_t*     width,
                               size_t*     height,
                               size_t*     rowBytes)
{
    png_structp png_ptr    = NULL;
	png_infop   info_ptr   = NULL;
    uint32_t*   _dest      = NULL;
    size_t      _width     = 0;
    size_t      _height    = 0;
    bool        shouldRead = true;
    int         result;
    uint8_t     header[8] __attribute__ ((aligned(16)));
    
    FILE *fp = fopen(filename, "rb");
    
    if (!fp)
    {
        printf("gbImage_PNG_Read_RGBA8888: can't open file [%s]\n", filename);
        shouldRead = false;
    }//if
    
    if (shouldRead)
    {
        fread(header, 1, 8, fp);
        
        result = png_sig_cmp(header, 0, 8);
        
        if (result)
        {
            printf("gbImage_PNG_Read_RGBA8888: not PNG: %s.  hdr:[%02x %02x %02x %02x %02x %02x %02x %02x]\n",
                   filename,
                   header[0], header[1], header[2], header[3],
                   header[4], header[5], header[6], header[7]);
            shouldRead = false;
        }//if
    }//if


    if (shouldRead)
    {
        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        
        if (!png_ptr)
        {
            printf("gbImage_PNG_Read_RGBA8888: can't make read struct\n");
            shouldRead = false;
        }//if
    }//if

    
    if (shouldRead)
    {
        info_ptr = png_create_info_struct(png_ptr);
        
        if (!info_ptr)
        {
            printf("gbImage_PNG_Read_RGBA8888: can't make info struct\n");
            shouldRead = false;
        }//if
    }//if

    if (shouldRead)
    {
        result = setjmp(png_jmpbuf(png_ptr));
        
        if (result)
        {
            printf("gbImage_PNG_Read_RGBA8888: err during init_io\n");
            shouldRead = false;
        }//if
    }//if


    if (shouldRead)
    {
        png_init_io(png_ptr, fp);
        png_set_sig_bytes(png_ptr, 8);
        
        png_read_info(png_ptr, info_ptr);
        
        _width                  = png_get_image_width(png_ptr, info_ptr);
        _height                 = png_get_image_height(png_ptr, info_ptr);
        int  color_type         = png_get_color_type(png_ptr, info_ptr);
        int  bit_depth          = png_get_bit_depth(png_ptr, info_ptr);
        bool indexed8_had_alpha = false;
        
        
        // try to force everything to RGBA8888
        
        if (color_type == PNG_COLOR_TYPE_PALETTE)                   // Indexed8 -> RGB8888
        {
            png_set_palette_to_rgb(png_ptr);
        }//if
        
        if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)     // Planar1 -> Planar8
        {                                                           // Planar2 -> Planar8
            png_set_expand_gray_1_2_4_to_8(png_ptr);                // Planar4 -> Planar8
        }//if
        
        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))        // Indexed8+A -> RGBA8888
        {
            png_set_tRNS_to_alpha(png_ptr);
            indexed8_had_alpha = true;
        }//if
        
        if (bit_depth == 16)                                        // Planar16 -> Planar8
        {
            png_set_strip_16(png_ptr);
        }//if
        
        if (   color_type == PNG_COLOR_TYPE_GRAY                    // Planar8 -> RGB888
            || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)             //         -> RGBA8888
        {
            png_set_gray_to_rgb(png_ptr);
        }//if
        
        if (    color_type == PNG_COLOR_TYPE_RGB                    // RGB888   -> RGBA8888
            ||  color_type == PNG_COLOR_TYPE_GRAY                   // Planar8  -> RGBA8888
            || (color_type == PNG_COLOR_TYPE_PALETTE                // Indexed8 -> RGBA8888
                && !indexed8_had_alpha))
        {
            png_set_add_alpha(png_ptr, 255, PNG_FILLER_AFTER);
        }//if
        
        //int number_of_passes = png_set_interlace_handling(png_ptr);   // may not handle interlaced correctly, untested.
        png_read_update_info(png_ptr, info_ptr);
        
        // read file
        if (setjmp(png_jmpbuf(png_ptr)))
        {
            printf("gbImage_PNG_Read_RGBA8888: err during read image\n");
            shouldRead = false;
        }//if

        if (shouldRead)
        {
            const size_t _rowBytes = png_get_rowbytes(png_ptr, info_ptr);
            _dest                  = malloc(sizeof(uint8_t) * _rowBytes * _height);
            size_t       destIdx   = 0;
            const size_t _inc      = _rowBytes / sizeof(uint32_t);
            
            png_bytep row_pointers[_height] __attribute__ ((aligned(16)));
            
            for (size_t y=0; y<_height; y++)
            {
                row_pointers[y]  = (png_byte*) &(_dest[destIdx]);
                destIdx         += _inc;
            }//for
            
            png_read_image(png_ptr, row_pointers);
        }//if
    }//if

    
	if (fp       != NULL) fclose(fp);
	if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
	if (png_ptr  != NULL) png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);

    *dest     = _dest;
    *width    = _width;
    *height   = _height;
    *rowBytes = _width * 4;
}//gbImage_PNG_Read_RGBA8888

















