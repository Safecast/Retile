#include "gbImage_Geometry.h"

// =============================================
// _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar
// =============================================
//
// Creates a mask for respecting the authoritay of the original alpha channel.
//
//   if src      then bitmask       else bitmask
// ==========    ============       ============
//  +--+--+         +--+               +--+
//  |00|00|         |00|               |01|
//  +--+--+         +--+               +--+
//
// performs:
// ---------
//
// bitmask[0].alpha = src[0].alpha == 0 && src[1].alpha == 0 ? 0 : 1
//
static inline void _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar(const uint8_t* src,
                                                                 const size_t   src_width,
                                                                 uint8_t*       dest)
{
    size_t       dest_idx = 0;
    const size_t Bpp      = 4;
    const size_t rowBytes = src_width * Bpp;
    const size_t x_inc    = Bpp * 2;
    
    for (size_t x = 0;  x <rowBytes; x += x_inc)
    {
        dest[dest_idx++] = src[x+3] != 0 || src[x+7] != 0 ? 1 : 0; // if any data, set to 1.
    }//for
    /*
    size_t       dest_idx = 0;
    const size_t Bpp      = 4;
    const size_t rowBytes = src_width * Bpp;
    
    for (size_t x=0; x<rowBytes; x+=Bpp*2)
    {
        memset(dest + dest_idx,
               src[x+3] != 0 || src[x+7] != 0 ? 1 : 0,  // if any data, set to 1.
               4);
        
        dest_idx += 4;
    }//for
    */
}// _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar

static inline void _CombineRows_AlphaBitmask_RGBA8888_NEON(const uint8_t* src0,
                                                           const uint8_t* src1,
                                                           const size_t   src_width,
                                                           uint8_t*       dest)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    uint8x16_t src0_u8x16;
    uint8x16_t src1_u8x16;
    uint8x16_t dest_u8x16;
    
    for (size_t x = 0; x < src_width; x += 16)
    {
        src0_u8x16 = vld1q_u8( &(src0[x]) );
        src1_u8x16 = vld1q_u8( &(src1[x]) );
        
        dest_u8x16 = vorrq_u8(src0_u8x16, src1_u8x16);
        
        vst1q_u8(&(dest[x]), dest_u8x16);
    }//for
#endif
}//_CombineRows_AlphaBitmask_RGBA8888_NEON


static inline void _CombineRows_AlphaBitmask_RGBA8888_scalar(const uint8_t* src0,
                                                             const uint8_t* src1,
                                                             const size_t   src_width,
                                                             uint8_t*       dest)
{
    // no need to examine values -- already 0 or 1 from per-row filter.
    
    for (size_t x = 0; x < src_width; x++)
    {
        dest[x] = src0[x] | src1[x]; // set to 1 if any data
    }//for
    
    /*
    const size_t Bpp      = 4;
    const size_t rowBytes = src_width * Bpp;
    
    // no need to examine values -- already 0 or 1 from per-row filter.
    
    for (size_t x=0; x<rowBytes; x+=Bpp)
    {
        memset(dest + x, src0[x+3] | src1[x+3], 4); // set to 1 if any data
    }//for
    */
}//_CombineRows_AlphaBitmask_RGBA8888_scalar

// =========================================
// _CombineRows_AlphaBitmask_RGBA8888_scalar
// =========================================
//
// Creates a mask for respecting the authoritay of the original alpha channel.
//
//   if src      then bitmask       else bitmask
// ==========    ============       ============
//    +--+          +--+               +--+
//    |00|          |00|               |01|
//    +--+          +--+               +--+
//    |00|
//    +--+
//
// performs:
// ---------
//
// bitmask[0].alpha = src[0].alpha == 0 && src[1].alpha == 0 ? 0 : 1
//
static inline void _CombineRows_AlphaBitmask_RGBA8888(const uint8_t* src0,
                                                      const uint8_t* src1,
                                                      const size_t   src_width,
                                                      uint8_t*       dest)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    _CombineRows_AlphaBitmask_RGBA8888_NEON(src0, src1, src_width, dest);
#else
    _CombineRows_AlphaBitmask_RGBA8888_scalar(src0, src1, src_width, dest);
#endif
}//_CombineRows_AlphaBitmask_RGBA8888


// ===========================================
// _ApplyRowAlphaBitmaskFilter_RGBA8888_scalar
// ===========================================
//
// Applies mask for respecting the authoritay of the original alpha channel
// to a row of the same size resampled by Lanczos.
//
// This has two goals:
//
// 1. Mitigate ringing artifacts
//      - All src alpha 0?
//      - Then dest RGBA all must be zero.
// 2. Mitigate generational information loss
//      - At least 1 src alpha > 0?
//      - Then dest alpha must be minimum of 1.
//
static inline void _ApplyRowAlphaBitmaskFilter_RGBA8888_scalar(const uint8_t* bitmask,
                                                               uint8_t*       dest,
                                                               const size_t   width)
{
    size_t       mask_x   = 0;
    const size_t Bpp      = 4;
    const size_t rowBytes = width * Bpp;
    
    for (size_t x = 0; x < rowBytes; x += Bpp)
    {
        if (bitmask[mask_x++] == 0)
        {
            memset(dest + x, 0, 4);
        }//if
        else if (dest[x+3] == 0)
        {
            dest[x+3] = 1;
        }//else
    }//for
    
    /*
    const size_t Bpp      = 4;
    const size_t rowBytes = width * Bpp;
    
    for (size_t x=0; x<rowBytes; x+=Bpp)
    {
        if (bitmask[x+3] == 0)
        {
            memset(dest + x, 0, 4);
        }//if
        else if (dest[x+3] == 0)
        {
            dest[x+3] = 1;
        }//else
    }//for
    */
}//_ApplyRowAlphaBitmaskFilter_RGBA8888_scalar


// =========================================
// gbImage_Resize_Half_AlphaBitmask_RGBA8888
// =========================================
//
// Uses vImage Lanczos for resize, then enforces src NODATA (alpha == 0).
//
// 1. Mitigate ringing artifacts
//      - All src alpha 0?
//      - Then dest RGBA all must be zero.
// 2. Mitigate generational information loss
//      - At least 1 src alpha > 0?
//      - Then dest alpha must be minimum of 1.
//
//
// The high-level reason for this is to make a "pixel perfect" resampling
// from Lanczos that can survive being resampled many times without introducing
// significant errors into the alpha channel.  While these errors are difficult
// to notice visually for about 10 generations or so, the main issue is it
// breaks bitmap indexing on the client.
//
// As a side perk, by removing the ringing artifact fringe, the overall
// compression ratio is improved somewhat.
//
// This supercedes any similar function commented out below.
//
void gbImage_Resize_Half_AlphaBitmask_RGBA8888(const uint8_t*         src,
                                               const size_t     src_width,
                                               const size_t    src_height,
                                               const size_t  src_rowBytes,
                                               uint8_t*              dest,
                                               const size_t    dest_width,
                                               const size_t   dest_height,
                                               const size_t dest_rowBytes)
{
    gbImage_Resize_Half_vImage_Lanczos3x3_RGBA8888(src,
                                                   dest,
                                                   src_width,  src_height,  src_rowBytes,
                                                   dest_width, dest_height, dest_rowBytes);

    size_t src_y;
    size_t src_y0_rowBytes;
    size_t src_y1_rowBytes;
    
    size_t dest_y = 0;
    size_t dest_y_rowBytes;
    
    uint8_t row0[dest_width]  __attribute__ ((aligned(16)));
    uint8_t row1[dest_width]  __attribute__ ((aligned(16)));
    
    for (src_y=0; src_y<src_height; src_y+=2)
    {
        src_y0_rowBytes =  src_y        *  src_rowBytes;
        src_y1_rowBytes = (src_y + 1UL) *  src_rowBytes;
        dest_y_rowBytes =  dest_y       * dest_rowBytes;
        
        _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar(src + src_y0_rowBytes,
                                                      src_width,
                                                      row0);
        
        _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar(src + src_y1_rowBytes,
                                                      src_width,
                                                      row1);
        
        _CombineRows_AlphaBitmask_RGBA8888(row0,
                                           row1,
                                           dest_width,
                                           row0);
        
        _ApplyRowAlphaBitmaskFilter_RGBA8888_scalar(row0,
                                                    dest + dest_y_rowBytes,
                                                    dest_width);
        
        dest_y++;
    }//for
    
    /*
    size_t src_y;
    size_t src_y0_rowBytes;
    size_t src_y1_rowBytes;
    
    size_t dest_y = 0;
    size_t dest_y_rowBytes;
    
    uint8_t row0[dest_width * 4]  __attribute__ ((aligned(16)));
    uint8_t row1[dest_width * 4]  __attribute__ ((aligned(16)));
    
    for (src_y=0; src_y<src_height-1; src_y+=2)
    {
        src_y0_rowBytes =  src_y        *  src_rowBytes;
        src_y1_rowBytes = (src_y + 1UL) *  src_rowBytes;
        dest_y_rowBytes =  dest_y       * dest_rowBytes;
        
        _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar(src + src_y0_rowBytes,
                                                      src_width,
                                                      row0);
        
        _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar(src + src_y1_rowBytes,
                                                      src_width,
                                                      row1);
        
        _CombineRows_AlphaBitmask_RGBA8888_scalar(row0,
                                                  row1,
                                                  dest_width,
                                                  row0);
        
        _ApplyRowAlphaBitmaskFilter_RGBA8888_scalar(row0,
                                                    dest + dest_y_rowBytes,
                                                    dest_width);
        
        dest_y++;
    }//for
    */
}//_gbImage_Resize_Half_AlphaBitmask_RGBA8888







/*
static inline void _DownsampleRow2x_RGBA8888_scalar(const uint8_t* src,
                                                    const size_t   src_width,
                                                    uint8_t*       dest)
{
    uint16_t r0_u16;
    uint16_t g0_u16;
    uint16_t b0_u16;
    uint16_t a0_u16;
    
    uint16_t r1_u16;
    uint16_t g1_u16;
    uint16_t b1_u16;
    uint16_t a1_u16;
    
    size_t dest_idx       = 0;
    const size_t Bpp      = 4;
    const size_t rowBytes = src_width * Bpp;
    
    for (size_t x=0; x<rowBytes; x+=Bpp*2)
    {
        r0_u16 = src[x  ];
        g0_u16 = src[x+1];
        b0_u16 = src[x+2];
        a0_u16 = src[x+3];
        
        r1_u16 = src[x+4];
        g1_u16 = src[x+5];
        b1_u16 = src[x+6];
        a1_u16 = src[x+7];
        
        r0_u16 <<= 8;
        g0_u16 <<= 8;
        b0_u16 <<= 8;
        a0_u16 <<= 8;
        
        r1_u16 <<= 8;
        g1_u16 <<= 8;
        b1_u16 <<= 8;
        a1_u16 <<= 8;
        
        r0_u16 >>= 1;
        g0_u16 >>= 1;
        b0_u16 >>= 1;
        a0_u16 >>= 1;
        
        r1_u16 >>= 1;
        g1_u16 >>= 1;
        b1_u16 >>= 1;
        a1_u16 >>= 1;
        
        r0_u16 += r1_u16;
        g0_u16 += g1_u16;
        b0_u16 += b1_u16;
        a0_u16 += a1_u16;
        
        r0_u16 >>= 8;
        g0_u16 >>= 8;
        b0_u16 >>= 8;
        a0_u16 >>= 8;
        
        dest[dest_idx  ] = (uint8_t)r0_u16;
        dest[dest_idx+1] = (uint8_t)g0_u16;
        dest[dest_idx+2] = (uint8_t)b0_u16;
        dest[dest_idx+3] = (uint8_t)a0_u16;
        
        dest_idx += 4;
    }//for
}// _DownsampleRow2x_RGBA8888_scalar


static inline void _CombineRows_RGBA8888_scalar(const uint8_t* src0,
                                                const uint8_t* src1,
                                                const size_t   src_width,
                                                uint8_t*       dest)
{
    uint16_t r0_u16;
    uint16_t r1_u16;
    uint16_t g0_u16;
    uint16_t g1_u16;
    uint16_t b0_u16;
    uint16_t b1_u16;
    uint16_t a0_u16;
    uint16_t a1_u16;
    
    size_t dest_idx       = 0;
    const size_t Bpp      = 4;
    const size_t rowBytes = src_width * Bpp;
    
    for (size_t x=0; x<rowBytes; x+=Bpp)
    {
        r0_u16 = src0[x  ];
        g0_u16 = src0[x+1];
        b0_u16 = src0[x+2];
        a0_u16 = src0[x+3];
        
        r1_u16 = src1[x  ];
        g1_u16 = src1[x+1];
        b1_u16 = src1[x+2];
        a1_u16 = src1[x+3];
        
        r0_u16 <<= 8;
        g0_u16 <<= 8;
        b0_u16 <<= 8;
        a0_u16 <<= 8;
        
        r1_u16 <<= 8;
        g1_u16 <<= 8;
        b1_u16 <<= 8;
        a1_u16 <<= 8;
        
        r0_u16 >>= 1;
        g0_u16 >>= 1;
        b0_u16 >>= 1;
        a0_u16 >>= 1;
        
        r1_u16 >>= 1;
        g1_u16 >>= 1;
        b1_u16 >>= 1;
        a1_u16 >>= 1;
        
        r0_u16 += r1_u16;
        g0_u16 += g1_u16;
        b0_u16 += b1_u16;
        a0_u16 += a1_u16;
        
        r0_u16 >>= 8;
        g0_u16 >>= 8;
        b0_u16 >>= 8;
        a0_u16 >>= 8;
        
        dest[dest_idx  ] = (uint8_t)r0_u16;
        dest[dest_idx+1] = (uint8_t)g0_u16;
        dest[dest_idx+2] = (uint8_t)b0_u16;
        dest[dest_idx+3] = (uint8_t)a0_u16;
        
        dest_idx += 4;
    }//for
}//_CombineRows_RGBA8888_scalar
*/


/*
static inline void _DownsampleRow2x_RGBA8888_NEON(const uint8_t* src,
                                                  const size_t   src_width,
                                                  uint8_t*       dest)
{
    uint8x8x4_t rgba_8x8x4;
    
    uint16x8_t r0_u16x8;
    uint16x8_t g0_u16x8;
    uint16x8_t b0_u16x8;
    uint16x8_t a0_u16x8;
    
    uint16x8_t r1_u16x8;
    uint16x8_t g1_u16x8;
    uint16x8_t b1_u16x8;
    uint16x8_t a1_u16x8;
    
    
    
    int16x8_t s0_s16x8;
    int16x8_t s1_u16x8;
    
    uint16x8_t eights_16x8 = vdupq_n_u16(8);
    
    size_t dest_idx       = 0;
    const size_t Bpp      = 4;
    const size_t rowBytes = src_width * Bpp;
    
    for (size_t x=0; x<rowBytes; x+=32)
    {
        rgba_8x8x4 = vld4_u8( &(src[x]) );
        
        r0_u16 = src[x  ];
        g0_u16 = src[x+1];
        b0_u16 = src[x+2];
        a0_u16 = src[x+3];
        
        r1_u16 = src[x+4];
        g1_u16 = src[x+5];
        b1_u16 = src[x+6];
        a1_u16 = src[x+7];
        
        r0_u16 <<= 8;
        g0_u16 <<= 8;
        b0_u16 <<= 8;
        a0_u16 <<= 8;
        
        r1_u16 <<= 8;
        g1_u16 <<= 8;
        b1_u16 <<= 8;
        a1_u16 <<= 8;
        
        s0_u16 = a0_u16 != 0 ? 1 : 0;   // assume NODATA is alpha = 0.
        s1_u16 = a1_u16 != 0 ? 1 : 0;
        s0_u16 = s0_u16 & s1_u16;       // only right-shift if both have data
        
        r0_u16 >>= s0_u16;
        g0_u16 >>= s0_u16;
        b0_u16 >>= s0_u16;
        a0_u16 >>= s0_u16;
        
        r1_u16 >>= s0_u16;
        g1_u16 >>= s0_u16;
        b1_u16 >>= s0_u16;
        a1_u16 >>= s0_u16;
        
        r0_u16 += r1_u16;
        g0_u16 += g1_u16;
        b0_u16 += b1_u16;
        a0_u16 += a1_u16;
        
        r0_u16 >>= 8;
        g0_u16 >>= 8;
        b0_u16 >>= 8;
        a0_u16 >>= 8;
        
        dest[dest_idx  ] = (uint8_t)r0_u16;
        dest[dest_idx+1] = (uint8_t)g0_u16;
        dest[dest_idx+2] = (uint8_t)b0_u16;
        dest[dest_idx+3] = (uint8_t)a0_u16;
        
        dest_idx += 4;
    }//for
}//_DownsampleRow2x_RGBA8888_NEON
*/






// =======================================
// _DownsampleRow2x_NODATA_RGBA8888_scalar
// =======================================
//
// Downsamples a RGBA8888 row by average, with the M. Night Shyamalan twist
// that no averaging will be done with pixels whose alpha channel value is 0.
//
// This is to preserve the original NODATA values (represented as a=0), and
// make resampling multiple levels of a raster pyramid not introduce excessive
// generational loss errors.
//
//
// performs:
// ---------
//
// if (   src[0].a > 0
//     && src[1].a > 0)
// {
//     dest[0].r = (src[0].r + src[1].r + 0.5) / 2.0;
//     dest[0].g = (src[0].g + src[1].g + 0.5) / 2.0;
//     dest[0].b = (src[0].b + src[1].b + 0.5) / 2.0;
//     dest[0].a = (src[0].a + src[1].a + 0.5) / 2.0;
// }
// else if (src[0].a > 0)
// {
//     dest[0].r = src[0].r;
//     dest[0].g = src[0].g;
//     dest[0].b = src[0].b;
//     dest[0].a = src[0].a;
// }
// else if (src[1].a > 0)
// {
//     dest[0].r = src[1].r;
//     dest[0].g = src[1].g;
//     dest[0].b = src[1].b;
//     dest[0].a = src[1].a;
// }
// else
// {
//     dest[0].r = 0;
//     dest[0].g = 0;
//     dest[0].b = 0;
//     dest[0].a = 0;
// }
//
static inline void _DownsampleRow2x_NODATA_RGBA8888_scalar(const uint8_t* src,
                                                           const size_t   src_width,
                                                           uint8_t*       dest)
{
    uint16_t r0_u16;
    uint16_t g0_u16;
    uint16_t b0_u16;
    uint16_t a0_u16;
    
    uint16_t r1_u16;
    uint16_t g1_u16;
    uint16_t b1_u16;
    uint16_t a1_u16;
    
    uint16_t s0_u16;
    uint16_t s1_u16;
    
    size_t dest_idx       = 0;
    const size_t Bpp      = 4;
    const size_t rowBytes = src_width * Bpp;
    
    for (size_t x=0; x<rowBytes; x+=Bpp*2)
    {
        r0_u16 = src[x  ];
        g0_u16 = src[x+1];
        b0_u16 = src[x+2];
        a0_u16 = src[x+3];
        
        r1_u16 = src[x+4];
        g1_u16 = src[x+5];
        b1_u16 = src[x+6];
        a1_u16 = src[x+7];
        
        r0_u16 <<= 8;                   // left shift 8 so 255 and 255 don't average to 254.
        g0_u16 <<= 8;
        b0_u16 <<= 8;
        a0_u16 <<= 8;
        
        r1_u16 <<= 8;
        g1_u16 <<= 8;
        b1_u16 <<= 8;
        a1_u16 <<= 8;
        
        s0_u16 = a0_u16 != 0 ? 1 : 0;   // assume NODATA is alpha = 0.
        s1_u16 = a1_u16 != 0 ? 1 : 0;
        
        r0_u16 *= s0_u16;               // multiply RGB by bitmask value
        g0_u16 *= s0_u16;               // such that alpha = 0 is RGB = 0
        b0_u16 *= s0_u16;               // to prevent overflows
        
        r1_u16 *= s1_u16;
        g1_u16 *= s1_u16;
        b1_u16 *= s1_u16;
        
        s0_u16 = s0_u16 & s1_u16;       // only right-shift if both have data
        
        r0_u16 >>= s0_u16;              // right shift to "divide"
        g0_u16 >>= s0_u16;
        b0_u16 >>= s0_u16;
        a0_u16 >>= s0_u16;
        
        r1_u16 >>= s0_u16;
        g1_u16 >>= s0_u16;
        b1_u16 >>= s0_u16;
        a1_u16 >>= s0_u16;
        
        r0_u16 += r1_u16;               // sum each halved pixel or OG value
        g0_u16 += g1_u16;               // if the other had alpha=0
        b0_u16 += b1_u16;
        a0_u16 += a1_u16;
        
        r0_u16 >>= 8;                   // narrowing shift to uint8_t range
        g0_u16 >>= 8;
        b0_u16 >>= 8;
        a0_u16 >>= 8;
        
        dest[dest_idx  ] = (uint8_t)r0_u16;
        dest[dest_idx+1] = (uint8_t)g0_u16;
        dest[dest_idx+2] = (uint8_t)b0_u16;
        dest[dest_idx+3] = (uint8_t)a0_u16;
        
        dest_idx += 4;
    }//for
}// _DownsampleRow2x_NODATA_RGBA8888_scalar


static inline void _CombineRows_NODATA_RGBA8888_scalar(const uint8_t* src0,
                                                       const uint8_t* src1,
                                                       const size_t   src_width,
                                                       uint8_t*       dest)
{
    uint16_t r0_u16;
    uint16_t r1_u16;
    uint16_t g0_u16;
    uint16_t g1_u16;
    uint16_t b0_u16;
    uint16_t b1_u16;
    uint16_t a0_u16;
    uint16_t a1_u16;
    
    uint16_t s0_u16;
    uint16_t s1_u16;
    
    size_t dest_idx       = 0;
    const size_t Bpp      = 4;
    const size_t rowBytes = src_width * Bpp;
    
    for (size_t x=0; x<rowBytes; x+=Bpp)
    {
        r0_u16 = src0[x  ];
        g0_u16 = src0[x+1];
        b0_u16 = src0[x+2];
        a0_u16 = src0[x+3];
        
        r1_u16 = src1[x  ];
        g1_u16 = src1[x+1];
        b1_u16 = src1[x+2];
        a1_u16 = src1[x+3];
        
        r0_u16 <<= 8;
        g0_u16 <<= 8;
        b0_u16 <<= 8;
        a0_u16 <<= 8;
        
        r1_u16 <<= 8;
        g1_u16 <<= 8;
        b1_u16 <<= 8;
        a1_u16 <<= 8;
        
        s0_u16 = a0_u16 != 0 ? 1 : 0;   // assume NODATA is alpha = 0.
        s1_u16 = a1_u16 != 0 ? 1 : 0;
        
        r0_u16 *= s0_u16;               // multiply RGB by bitmask value
        g0_u16 *= s0_u16;               // such that alpha = 0 is RGB = 0
        b0_u16 *= s0_u16;               // to prevent overflows
        
        r1_u16 *= s1_u16;
        g1_u16 *= s1_u16;
        b1_u16 *= s1_u16;
        
        s0_u16 = s0_u16 & s1_u16;       // only right-shift if both have data
        
        r0_u16 >>= s0_u16;
        g0_u16 >>= s0_u16;
        b0_u16 >>= s0_u16;
        a0_u16 >>= s0_u16;
        
        r1_u16 >>= s0_u16;
        g1_u16 >>= s0_u16;
        b1_u16 >>= s0_u16;
        a1_u16 >>= s0_u16;
        
        r0_u16 += r1_u16;
        g0_u16 += g1_u16;
        b0_u16 += b1_u16;
        a0_u16 += a1_u16;
        
        r0_u16 >>= 8;
        g0_u16 >>= 8;
        b0_u16 >>= 8;
        a0_u16 >>= 8;
        
        dest[dest_idx  ] = (uint8_t)r0_u16;
        dest[dest_idx+1] = (uint8_t)g0_u16;
        dest[dest_idx+2] = (uint8_t)b0_u16;
        dest[dest_idx+3] = (uint8_t)a0_u16;
        
        dest_idx += 4;
    }//for
}//_CombineRows_NODATA_RGBA8888_scalar


// ==============================================
// gbImage_Resize_Half_vImage_Lanczos3x3_RGBA8888
// ==============================================
//
// Basic wrapper around vImageScale's Lanczos 3x3 resize.
//
// This produces superior results to downsampling by average, but is slightly
// slower and introduces ringing and overshoot / understood artifacts.
//
// Lanczos 5x5 is obtainable by passing kvImageHighQualityResampling
//             instead of kvImageNoFlags.  However, the artifacts are increased.
//
void gbImage_Resize_Half_vImage_Lanczos3x3_RGBA8888(const uint8_t* src,
                                                    uint8_t*       dest,
                                                    const size_t   src_w,
                                                    const size_t   src_h,
                                                    const size_t   src_rowBytes,
                                                    const size_t   dest_w,
                                                    const size_t   dest_h,
                                                    const size_t   dest_rowBytes)
{
    vImage_Buffer vi_src  = { (void*)src,   src_h,  src_w,  src_rowBytes };
    vImage_Buffer vi_dest = { (void*)dest, dest_h, dest_w, dest_rowBytes };
    
    vImageScale_ARGB8888(&vi_src, &vi_dest, NULL, kvImageNoFlags);
}//_vImageScaleWrapper_RGBA8888



// 2014-07-31 ND: these were attempted bugfixes for an issue involving early
//                tRNS truncation in the PNG write.  were unrelated.
/*
// edge extend by duping last row/column, may(?) prevent artifacts
void gbImage_Resize_Half_vImage_Lanczos3x3_EdgeExtend_RGBA8888(const uint8_t* src,
                                                               uint8_t*       dest,
                                                               const size_t   src_w,
                                                               const size_t   src_h,
                                                               const size_t   src_rowBytes,
                                                               const size_t   dest_w,
                                                               const size_t   dest_h,
                                                               const size_t   dest_rowBytes)
{
    const size_t src_Bpp   = src_rowBytes / src_w;
    const size_t extend_px = 4;
    
    const size_t ext_w     = src_w + extend_px;
    const size_t ext_h     = src_h + extend_px;
    const size_t ext_rb    = ext_w * src_Bpp;
    
    uint8_t* ext = malloc(sizeof(uint8_t) * ext_rb * ext_h);
    
    size_t y;
    size_t x;
    size_t src_y_rowBytes;
    size_t ext_y_rowBytes;
    
    
    
    for (y=0; y<src_h; y++)
    {
        src_y_rowBytes = y * src_rowBytes;
        ext_y_rowBytes = y * ext_rb;
        
        memcpy(ext + ext_y_rowBytes,
               src + src_y_rowBytes,
               src_rowBytes);
        
        for (x = src_rowBytes; x < ext_rb; x += src_Bpp)
        {
            memcpy(ext + ext_y_rowBytes + x,
                   src + src_y_rowBytes + src_rowBytes - src_Bpp,
                   src_Bpp);
        }//for
    }//for
    
    
    
    src_y_rowBytes = (src_h - 1) * ext_rb; // last row copied above
    
    for (y=src_h; y<ext_h; y++)
    {
        ext_y_rowBytes = y * ext_rb;
        
        memcpy(ext + ext_y_rowBytes,
               ext + src_y_rowBytes,
               ext_rb);
    }//for
    
    
    size_t stride = src_rowBytes / src_w;
    size_t ede_px = MAX(extend_px / stride, 1);
    size_t ede_w  = dest_w + ede_px;
    size_t ede_h  = dest_h + ede_px;
    size_t ede_rb = ede_w * src_Bpp;
    
    uint8_t* ede = malloc(sizeof(uint8_t) * ede_rb * ede_h);
    
    
    gbImage_Resize_Half_vImage_Lanczos3x3_RGBA8888(ext,
                                                   ede,
                                                   ext_w, ext_h, ext_rb,
                                                   ede_w, ede_h, ede_rb);
    
    for (y=0; y<dest_h; y++)
    {
        src_y_rowBytes = y * ede_rb;
        ext_y_rowBytes = y * dest_rowBytes;
        
        memcpy(dest + ext_y_rowBytes,
               ede  + src_y_rowBytes,
               dest_w * src_Bpp);
    }//for
    
    free(ext);
    ext = NULL;
    free(ede);
    ede = NULL;
}//gbImage_Resize_Half_vImage_Lanczos3x3_EdgeExtend_RGBA8888



// filter ringing artifacts in alpha 0 = NODATA land. (maybe)
void gbImage_Resize_Half_vImage_Lanczos3x3_RingingFilter_RGBA8888(const uint8_t* src,
                                                                  uint8_t*       dest,
                                                                  const size_t   src_w,
                                                                  const size_t   src_h,
                                                                  const size_t   src_rowBytes,
                                                                  const size_t   dest_w,
                                                                  const size_t   dest_h,
                                                                  const size_t   dest_rowBytes)
{
    uint8_t* lanczos_dest = malloc(sizeof(uint8_t) * dest_rowBytes * dest_h);
    memcpy(lanczos_dest, dest, dest_rowBytes * dest_h);
    
    gbImage_Resize_Half_vImage_Lanczos3x3_RGBA8888(src,
                                                   lanczos_dest,
                                                   src_w, src_h, src_rowBytes,
                                                   dest_w, dest_h, dest_rowBytes);
    
    size_t src_y;
    size_t src_y0_rowBytes;
    size_t src_y1_rowBytes;
    
    size_t dest_y = 0;
    size_t dest_y_rowBytes;
    
    uint8_t row0[dest_w * 4]  __attribute__ ((aligned(16)));
    uint8_t row1[dest_w * 4]  __attribute__ ((aligned(16)));
    
    for (src_y=0; src_y<src_h-1; src_y+=2)
    {
        src_y0_rowBytes =  src_y        *  src_rowBytes;
        src_y1_rowBytes = (src_y + 1UL) *  src_rowBytes;
        dest_y_rowBytes =  dest_y       * dest_rowBytes;
        
        _DownsampleRow2x_NODATA_RGBA8888_scalar(src + src_y0_rowBytes,
                                                src_w,
                                                row0);
        
        _DownsampleRow2x_NODATA_RGBA8888_scalar(src + src_y1_rowBytes,
                                                src_w,
                                                row1);
        
        _CombineRows_NODATA_RGBA8888_scalar(row0,
                                            row1,
                                            dest_w,
                                            dest + dest_y_rowBytes);
        
        dest_y++;
    }//for
    
    size_t y;
    size_t x;
    size_t px_idx;
    
    for (y=0; y<dest_h; y++)
    {
        dest_y_rowBytes = y * dest_rowBytes;
        
        for (x=0; x<dest_w; x++)
        {
            px_idx = dest_y_rowBytes + x * 4;
            
            if (dest[px_idx+3] > 15)
            {
                dest[px_idx]   = lanczos_dest[px_idx];
                dest[px_idx+1] = lanczos_dest[px_idx+1];
                dest[px_idx+2] = lanczos_dest[px_idx+2];
                dest[px_idx+3] = lanczos_dest[px_idx+3];
            }//if
        }//for
    }//for
    
    free(lanczos_dest);
    lanczos_dest = NULL;
}//gbImage_Resize_Half_vImage_Lanczos3x3_RingingFilter_RGBA8888
*/

// griddata_1829_788_11.png





// ============================
// gbImage_Resize_Half_RGBA8888
// ============================
//
// Basic 50% resize for RGBA8888 buffers whose width/height are evenly divisible
// by 2.
//
// Resamples by average.  Provides NODATA protection by not averaging pixels
// whose alpha value is zero.  This produces pixel-perfect output for
// iteratively resampling the previous level of a raster tile pyramid, compared
// to the much slower process of always resampling the base level.
//
// If vImage is available, a hybrid Lanczos 3x3 interpolation with a NODATA
// filter to control undershoot and ringing artifacts is used.  This allows
// Lanczos to be used safely many times on the same set of images, again
// producing pixel-perfect output vs. resampling the base level.
//
// (by "pixel-perfect" it is meant that there is no generational loss which
//  would either make NODATA pixels data, or the opposite.  There is some loss
//  of precision in the exact pixel values, of course.)
//
void gbImage_Resize_Half_RGBA8888(const uint8_t*         src,
                                  const size_t     src_width,
                                  const size_t    src_height,
                                  const size_t  src_rowBytes,
                                  uint8_t*              dest,
                                  const size_t    dest_width,
                                  const size_t   dest_height,
                                  const size_t dest_rowBytes)
{
#ifdef __ACCELERATE__
    //gbImage_Resize_Half_vImage_Lanczos3x3_RGBA8888(src, dest, src_width, src_height, src_rowBytes, dest_width, dest_height, dest_rowBytes);
    gbImage_Resize_Half_AlphaBitmask_RGBA8888(src, src_width, src_height, src_rowBytes, dest, dest_width, dest_height, dest_rowBytes);
    return;
#endif
    
    size_t src_y;
    size_t src_y0_rowBytes;
    size_t src_y1_rowBytes;
    
    size_t dest_y = 0;
    size_t dest_y_rowBytes;
    
    uint8_t row0[dest_width * 4]  __attribute__ ((aligned(16)));
    uint8_t row1[dest_width * 4]  __attribute__ ((aligned(16)));
    
    for (src_y=0; src_y<src_height-1; src_y+=2)
    {
        src_y0_rowBytes =  src_y        *  src_rowBytes;
        src_y1_rowBytes = (src_y + 1UL) *  src_rowBytes;
        dest_y_rowBytes =  dest_y       * dest_rowBytes;
        
        //printf("src_y0_rowBytes=%zu\n", src_y0_rowBytes);
        
        _DownsampleRow2x_NODATA_RGBA8888_scalar(src + src_y0_rowBytes,
                                                src_width,
                                                row0);
        
        //printf("src_y1_rowBytes=%zu\n", src_y1_rowBytes);
        
        _DownsampleRow2x_NODATA_RGBA8888_scalar(src + src_y1_rowBytes,
                                                src_width,
                                                row1);

        _CombineRows_NODATA_RGBA8888_scalar(row0,
                                            row1,
                                            dest_width,
                                            dest + dest_y_rowBytes);
        
        dest_y++;
    }//for
}//_Downsample2x_RGBA8888

// =======================
// _GetPxOffsetForXYZtoXYZ
// =======================
//
// Helper method for converting tile xyz coordinates to pixel xyz.
//
// Finds the offset row or column used a starting point when downsampling
// tile src into tile dest of the same size with, for a resize factor implicitly
// contained in the zoom level difference that is treated as a power of two.
//
// In other words, finds part of the ROI for subtiling an image.
//
static inline uint32_t _GetPxOffsetForXYZtoXYZ(const uint32_t  src_x,
                                               const uint32_t  src_z,
                                               const uint32_t dest_x,
                                               const uint32_t dest_z,
                                               const size_t    width)
{
    uint32_t w_u32   = (uint32_t)width;
    uint32_t src_px  = src_x  * w_u32;
    uint32_t dest_px = dest_x * w_u32;
    
    src_px >>= (src_z - dest_z);
    
    return src_px - dest_px;
}//_GetPxOffsetForXYZtoXYZ



// ================================
// gbImage_Resize_HalfTile_RGBA8888
// ================================
//
// Convienience wrapper for gbImage_Resize_RGBA8888.
//
// This allows for offloading the maths when resampling EPSG:3857 Web Mercator
// tiles using the Google Maps tile convention.
//
// Calculates the necessary destination ROI for buffer dest and resamples
// src to it.
//
// For a difference of one zoom level, src will take up one quarter of dest,
// and thus it is expected this would be called 4 times for 4 source tiles,
// accumulating all 4 source tiles into the same dest tile.
//
// Resamples by average.  Provides NODATA protection by not averaging pixels
// whose alpha value is zero.  This produces pixel-perfect output for
// iteratively resampling the previous level of a raster tile pyramid, compared
// to the much slower process of always resampling the base level.
//
// If vImage is available, a hybrid Lanczos 3x3 interpolation with a NODATA
// filter to control undershoot and ringing artifacts is used.  This allows
// Lanczos to be used safely many times on the same set of images, again
// producing pixel-perfect output vs. resampling the base level.
//
// (by "pixel-perfect" it is meant that there is no generational loss which
//  would either make NODATA pixels data, or the opposite.  There is some loss
//  of precision in the exact pixel values, of course.)
//
void gbImage_Resize_HalfTile_RGBA8888(const uint8_t*      src,
                                      const uint32_t    src_x,
                                      const uint32_t    src_y,
                                      const uint32_t    src_z,
                                      uint8_t*           dest,
                                      const uint32_t   dest_x,
                                      const uint32_t   dest_y,
                                      const uint32_t   dest_z,
                                      const size_t        Bpp,
                                      const size_t      width,
                                      const size_t     height,
                                      const size_t   rowBytes)
{
    const size_t offset_x = (size_t)_GetPxOffsetForXYZtoXYZ(src_x, src_z, dest_x, dest_z, width);
    const size_t offset_y = (size_t)_GetPxOffsetForXYZtoXYZ(src_y, src_z, dest_y, dest_z, height);
    const size_t dest_off = offset_y * rowBytes + offset_x * Bpp;
    const size_t dest_w   = width  >> (src_z - dest_z);
    const size_t dest_h   = height >> (src_z - dest_z);
    
    //printf("src_x=%d, src_y=%d, dest_x=%d, dest_y=%d, off_x=%zu, off_y=%zu\n", src_x, src_y, dest_x, dest_y, offset_x, offset_y);
    
    //printf("rowbytes=%zu, w=%zu, h=%zu, destoff=%zu\n", rowBytes, dest_w, dest_h, dest_off);
    
    // 131584
    
    if (dest_off > 131072 + 65536)
    {
        printf("Unexpected dest_off=%zu\n", dest_off);
    }//if
    
    if (dest_w != 128 || dest_h != 128)
    {
        printf("Unexpected dest_w/dest_h: %zu x %zu\n", dest_w, dest_h);
    }
    
    if (width != 256 || height != 256)
    {
        printf("Unexpected src_w/src_h: %zu x %zu\n", width, height);
    }
        
    gbImage_Resize_Half_RGBA8888(src,
                                 width,
                                 height,
                                 rowBytes,
                                 dest + dest_off,
                                 dest_w,
                                 dest_h,
                                 rowBytes);
}//_Downsample2x_RGBA8888

/*
void _Downsample2x_Planar8(const uint8_t* src_r,
                           const uint8_t* src_g,
                           const uint8_t* src_b,
                           const uint8_t* src_a,
                           uint8_t*       dest,
                           const size_t   width,
                           const size_t   height)
{
    uint16_t c0_u16;
    uint16_t c1_u16;
    uint16_t a0_u16;
    uint16_t a1_u16;
    uint16_t s0_u16;
    uint16_t s1_u16;
    
    for (size_t x=0; x<width; x+=2)
    {
        c0_u16 = src_x[x];
        c1_u16 = src_x[x+1];
        c0_u16 = c0_u16 << 8;
        c1_u16 = c1_u16 << 8;           // load R/G/B and promote to uint16_t
        
        a0_u16 = src_a[x];
        a1_u16 = src_a[x+1];            // load A
        
        s0_u16 = a0_u16 != 0 ? 1 : 0;   // assume NODATA is alpha channel 0.
        s1_u16 = a1_u16 != 0 ? 1 : 0;
        s0_u16 = s0_u16 & s1_u16;       // only right-shift if both have data
        
        c0_u16 = c0_u16 >> s0_u16;
        c1_u16 = c1_u16 >> s0_u16;
        c0_u16 = c0_u16  + c1_u16;
        c0_u16 = c0_u16 >> 8;           // get averaged R/G/B value
        
        dest[x] = (uint8_t)c0_u16;      // store value
    }//for
}// _DownsampleRow2x_Planar8_scalar








void _RGBA8888_to_Planar8_scalar(const uint8_t* src,
                                 uint8_t**      dest_r,
                                 uint8_t**      dest_g,
                                 uint8_t**      dest_b,
                                 uint8_t**      dest_a,
                                 const size_t   width,
                                 const size_t   height)
{
    uint8_t* _dest_r = malloc(sizeof(uint8_t) * width * height);
    uint8_t* _dest_g = malloc(sizeof(uint8_t) * width * height);
    uint8_t* _dest_b = malloc(sizeof(uint8_t) * width * height);
    uint8_t* _dest_a = malloc(sizeof(uint8_t) * width * height);
    
    for (size_t i=0; i<width*height; i++)
    {
        _dest_r[i] = src[i*4  ];
        _dest_g[i] = src[i*4+1];
        _dest_b[i] = src[i*4+2];
        _dest_a[i] = src[i*4+3];
    }//for
    
    *dest_r = _dest_r;
    *dest_g = _dest_g;
    *dest_b = _dest_b;
    *dest_a = _dest_a;
}//_RGBA8888_to_Planar8_scalar



void _DownsampleRow2x_Planar8_scalar(const uint8_t* src_x,
                                     const uint8_t* src_a,
                                     uint8_t*       dest,
                                     const size_t   width)
{
    uint16_t c0_u16;
    uint16_t c1_u16;
    uint16_t a0_u16;
    uint16_t a1_u16;
    uint16_t s0_u16;
    uint16_t s1_u16;
    
    for (size_t x=0; x<width; x+=2)
    {
        c0_u16 = src_x[x];
        c1_u16 = src_x[x+1];
        c0_u16 = c0_u16 << 8;
        c1_u16 = c1_u16 << 8;           // load R/G/B and promote to uint16_t
        
        a0_u16 = src_a[x];
        a1_u16 = src_a[x+1];            // load A
        
        s0_u16 = a0_u16 != 0 ? 1 : 0;   // assume NODATA is alpha channel 0.
        s1_u16 = a1_u16 != 0 ? 1 : 0;
        s0_u16 = s0_u16 & s1_u16;       // only right-shift if both have data
        
        c0_u16 = c0_u16 >> s0_u16;
        c1_u16 = c1_u16 >> s0_u16;
        c0_u16 = c0_u16  + c1_u16;
        c0_u16 = c0_u16 >> 8;           // get averaged R/G/B value
        
        dest[x] = (uint8_t)c0_u16;      // store value
    }//for
}// _DownsampleRow2x_Planar8_scalar
*/
