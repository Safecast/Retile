#include "gbImage_Geometry.h"

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

// 2014-08-11 ND: NEON version of this has a bug, use the scalar one for now.
//                (it only replaces 1 line of scalar code anyway...)
//
static FORCE_INLINE void _DownsampleRow2x_AlphaBitmask_RGBA8888_NEON(const uint8_t* src,
                                                                     const size_t   src_width,
                                                                     uint8_t*       dest)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    size_t       dest_idx   = 0;
    uint16x8_t   zero_u16x8 = vdupq_n_u16(0x0000);
    uint16x8_t   onem_u16x8 = vdupq_n_u16(0x0001);
    uint8x16x4_t src_8x16x4;
    uint16x8_t   dest_u16x8;
    uint8x8_t    dest_u8x8;
    
    for (size_t x = 0;  x < src_width; x += 64)
    {
        src_8x16x4 = vld4q_u8( &(src[x]   ) );                // R0G0B0A0 R1G1B1A1... -> A00 A01 A02...
                                                              // [00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15]
        dest_u16x8  = vpaddlq_u8(src_8x16x4.val[3]);          // [0000   0001  0002  0003  0004  0005  0006  0007]
        dest_u16x8  = vcgtq_u16(dest_u16x8, zero_u16x8);      // a = a > 0x0000 ? 0xFFFF : 0x0000
        dest_u16x8  = vandq_u16(dest_u16x8, onem_u16x8);      // a = a & 0x0001
        dest_u8x8   = vmovn_u16(dest_u16x8);                  // [  00     01    02    03    04    05    06    07]
        vst1_u8( &(dest[dest_idx]), dest_u8x8);
        dest_idx += 8;                                        // RGBA8888 -> Planar8 and 2x downsample = 64 bytes -> 16 bytes -> 8 bytes
    }//for
#endif
}// _DownsampleRow2x_AlphaBitmask_RGBA8888_NEON

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
static FORCE_INLINE void _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar(const uint8_t* src,
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
}// _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar

static FORCE_INLINE void _DownsampleRow2x_AlphaBitmask_RGBA8888(const uint8_t* src,
                                                                const size_t   src_width,
                                                                uint8_t*       dest)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    //_DownsampleRow2x_AlphaBitmask_RGBA8888_NEON(src, src_width, dest); // NEON version has bug, use scalar for now
    _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar(src, src_width, dest);
#else
    _DownsampleRow2x_AlphaBitmask_RGBA8888_scalar(src, src_width, dest);
#endif
}//_DownsampleRow2x_AlphaBitmask_RGBA8888



static FORCE_INLINE void _CombineRows_AlphaBitmask_RGBA8888_NEON(const uint8_t* src0,
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


static FORCE_INLINE void _CombineRows_AlphaBitmask_RGBA8888_scalar(const uint8_t* src0,
                                                                   const uint8_t* src1,
                                                                   const size_t   src_width,
                                                                   uint8_t*       dest)
{
    // no need to examine values -- already 0 or 1 from per-row filter.
    
    for (size_t x = 0; x < src_width; x++)
    {
        dest[x] = src0[x] | src1[x]; // set to 1 if any data
    }//for
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
static FORCE_INLINE void _CombineRows_AlphaBitmask_RGBA8888(const uint8_t* src0,
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
static FORCE_INLINE void _ApplyRowAlphaBitmaskFilter_RGBA8888_scalar(const uint8_t* bitmask,
                                                                     uint8_t*       dest,
                                                                     const size_t   width)
{
    size_t    mask_x   = 0;
    uint32_t* dest_u32 = (uint32_t*)dest;
    
    for (size_t x = 0; x < width; x++)
    {
        if (bitmask[mask_x++] == 0)
        {
            dest_u32[x] = 0;
        }//if
        else if (dest_u32[x] >> 24 == 0)
        {
            dest_u32[x] = 0x01000000;
        }//else
    }//for
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
                                               const size_t dest_rowBytes,
                                               const int interpolationTypeId)
{
    if (interpolationTypeId == kGB_Image_Interp_Lanczos3x3)
    {
        gbImage_Resize_vImage_Lanczos3x3_RGBA8888(src,
                                                  dest,
                                                  src_width,  src_height,  src_rowBytes,
                                                  dest_width, dest_height, dest_rowBytes);
    }//if
    else if (interpolationTypeId == kGB_Image_Interp_Lanczos5x5)
    {
        gbImage_Resize_vImage_Lanczos5x5_RGBA8888(src,
                                                  dest,
                                                  src_width,  src_height,  src_rowBytes,
                                                  dest_width, dest_height, dest_rowBytes);
    }//else if
    else
    {
        gbImage_Resize_Bilinear_RGBA8888(src,
                                         dest,
                                         src_width,  src_height,
                                         dest_width, dest_height);
    }//else

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
}//_gbImage_Resize_Half_AlphaBitmask_RGBA8888





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
static FORCE_INLINE void _DownsampleRow2x_NODATA_RGBA8888_scalar(const uint8_t* src,
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


static FORCE_INLINE void _CombineRows_NODATA_RGBA8888_scalar(const uint8_t* src0,
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


// =========================================
// gbImage_Resize_vImage_Lanczos3x3_RGBA8888
// ==========================================
//
// Basic wrapper around vImageScale's Lanczos 3x3 resize.
//
// This produces superior results to downsampling by average, but is slightly
// slower and introduces ringing and overshoot / undershoot artifacts.
//
// Lanczos 5x5 is obtainable by passing kvImageHighQualityResampling
//             instead of kvImageNoFlags.  However, the artifacts are increased.
//
void gbImage_Resize_vImage_Lanczos3x3_RGBA8888(const uint8_t* src,
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
}//gbImage_Resize_vImage_Lanczos3x3_RGBA8888

void gbImage_Resize_vImage_Lanczos5x5_RGBA8888(const uint8_t* src,
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
    
    vImageScale_ARGB8888(&vi_src, &vi_dest, NULL, kvImageHighQualityResampling);
}//gbImage_Resize_vImage_Lanczos5x5_RGBA8888

// =================================
// gbImage_Resize_Bilinear_RGBA8888:
// =================================
//
// This code is from: http://pulsar.webshaker.net/2011/05/25/bilinear-enlarge-with-neon/
//
// TODO: replace with vectorized implementation that works on ARM/IA32
//
void gbImage_Resize_Bilinear_RGBA8888(const uint8_t* src,
                                      uint8_t*       dest,
                                      const size_t   src_w,
                                      const size_t   src_h,
                                      const size_t   dest_w,
                                      const size_t   dest_h)
{
    uint32_t* bSrc = (uint32_t*)src;
    uint32_t* bDst = (uint32_t*)dest;
    int wSrc = (int)src_w;
    int hSrc = (int)src_h;
    int wDst = (int)dest_w;
    int hDst = (int)dest_h;
    
    uint32_t wStepFixed16b, hStepFixed16b, wCoef, hCoef, x, y;
    uint32_t pixel1, pixel2, pixel3, pixel4;
    uint32_t hc1, hc2, wc1, wc2, offsetX, offsetY;
    uint32_t r, g, b, a;
    
    wStepFixed16b = ((wSrc - 1) << 16) / (wDst - 1);
    hStepFixed16b = ((hSrc - 1) << 16) / (hDst - 1);
    
    hCoef = 0;
    
    for (y = 0 ; y < hDst ; y++)
    {
        offsetY = (hCoef >> 16);
        hc2 = (hCoef >> 9) & 127;
        hc1 = 128 - hc2;
        
        wCoef = 0;
        for (x = 0 ; x < wDst ; x++)
        {
            offsetX = (wCoef >> 16);
            wc2 = (wCoef >> 9) & 127;
            wc1 = 128 - wc2;
            
            pixel1 = *(bSrc + offsetY * wSrc + offsetX);
            pixel2 = *(bSrc + (offsetY + 1) * wSrc + offsetX);
            pixel3 = *(bSrc + offsetY * wSrc + offsetX + 1);
            pixel4 = *(bSrc + (offsetY + 1) * wSrc + offsetX + 1);
            
            r = ((((pixel1 >> 24) & 255) * hc1 + ((pixel2 >> 24) & 255) * hc2) * wc1 +
                 (((pixel3 >> 24) & 255) * hc1 + ((pixel4 >> 24) & 255) * hc2) * wc2) >> 14;
            g = ((((pixel1 >> 16) & 255) * hc1 + ((pixel2 >> 16) & 255) * hc2) * wc1 +
                 (((pixel3 >> 16) & 255) * hc1 + ((pixel4 >> 16) & 255) * hc2) * wc2) >> 14;
            b = ((((pixel1 >> 8) & 255) * hc1 + ((pixel2 >> 8) & 255) * hc2) * wc1 +
                 (((pixel3 >> 8) & 255) * hc1 + ((pixel4 >> 8) & 255) * hc2) * wc2) >> 14;
            a = ((((pixel1 >> 0) & 255) * hc1 + ((pixel2 >> 0) & 255) * hc2) * wc1 +
                 (((pixel3 >> 0) & 255) * hc1 + ((pixel4 >> 0) & 255) * hc2) * wc2) >> 14;
            
            *bDst++ = (r << 24) + (g << 16) + (b << 8) + (a);
            
            wCoef += wStepFixed16b;
        }
        hCoef += hStepFixed16b;
    }
}//gbImage_Resize_Bilinear_RGBA8888




// ============================================
// gbImage_Resize_Half_Average_NODATA_RGBA8888:
// ============================================
//
// Basic 50% resize for RGBA8888 buffers whose width/height are evenly divisible
// by 2.
//
// Resamples by average.  Provides NODATA protection by not averaging pixels
// whose alpha value is zero.  This produces pixel-perfect output for
// iteratively resampling the previous level of a raster tile pyramid, compared
// to the much slower process of always resampling the base level.
//
// (by "pixel-perfect" it is meant that there is no generational loss which
//  would either make NODATA pixels data, or the opposite.  There is some loss
//  of precision in the exact pixel values, of course.)
//
void gbImage_Resize_Half_AverageNODATA_RGBA8888(const uint8_t*         src,
                                                const size_t     src_width,
                                                const size_t    src_height,
                                                const size_t  src_rowBytes,
                                                uint8_t*              dest,
                                                const size_t    dest_width,
                                                const size_t   dest_height,
                                                const size_t dest_rowBytes,
                                                const int   interpolationTypeId)
{
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
        
        _DownsampleRow2x_NODATA_RGBA8888_scalar(src + src_y0_rowBytes,
                                                src_width,
                                                row0);
        
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
    
    //src_px >>= (src_z - dest_z);
    //return src_px - dest_px;
    
    return src_z > dest_z ?  (src_px >> ( src_z - dest_z)) - dest_px
                          : (dest_px >> (dest_z -  src_z)) -  src_px;
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
                                      const size_t   rowBytes,
                                      const int     interpolationTypeId)
{
    const size_t offset_x = (size_t)_GetPxOffsetForXYZtoXYZ(src_x, src_z, dest_x, dest_z, width);
    const size_t offset_y = (size_t)_GetPxOffsetForXYZtoXYZ(src_y, src_z, dest_y, dest_z, height);
    const size_t dest_off = offset_y * rowBytes + offset_x * Bpp;
    const size_t dest_w   = width  >> (src_z - dest_z);
    const size_t dest_h   = height >> (src_z - dest_z);
    
    int _interpolationTypeId = interpolationTypeId;
    
#ifndef __ACCELERATE__
    _interpolationTypeId  = _interpolationTypeId  == kGB_Image_Interp_Lanczos3x3 || kGB_Image_Interp_Lanczos5x5 ? kGB_Image_Interp_Average;
#endif
    
    if (_interpolationTypeId == kGB_Image_Interp_Average)
    {
        gbImage_Resize_Half_AverageNODATA_RGBA8888(src,
                                                   width, height, rowBytes,
                                                   dest + dest_off,
                                                   dest_w, dest_h, rowBytes,
                                                   _interpolationTypeId);
    }//if
    else if (   _interpolationTypeId == kGB_Image_Interp_Lanczos3x3
             || _interpolationTypeId == kGB_Image_Interp_Lanczos5x5)
    {
        gbImage_Resize_Half_AlphaBitmask_RGBA8888(src,
                                                  width, height, rowBytes,
                                                  dest + dest_off,
                                                  dest_w, dest_h, rowBytes,
                                                  _interpolationTypeId);
    }//else
}//_Downsample2x_RGBA8888












































// ***






static FORCE_INLINE uint32_t _GetDataCount_RGBA8888_Scalar(const uint32_t* v0,
                                                           const size_t    n)
{
    uint32_t _dc = 0;
    uint32_t  _n = (uint32_t)n;
    
    for (uint32_t i = 0; i<_n; i++)
    {
        if (v0[i] >> 24 == 0)
        {
            _dc++;
        }//if
    }//for
    
    _dc = _n - _dc;
    
    return _dc;
}//_GetDataCount_RGBA8888_Scalar


static FORCE_INLINE uint32_t _GetDataCount_RGBA8888_NEON(const uint32_t* v0,
                                                         const size_t    n)
{
    uint32_t  _dc          = 0;
    
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    int32_t*  v0_s32       = (int32_t*)v0;
    int32x4_t NODATA_s32x4 = vdupq_n_u32(0);
    int32x4_t mask_s32x4   = vdupq_n_u32(0xFF000000);
    int32x4_t accum_s32x4  = vdupq_n_s32(0);
    int32x4_t src_s32x4;
    int32x4_t bits_s32x4;

    
    for (size_t i = 0UL; i < n; i += 4UL)
    {
        src_s32x4   = vld1q_s32( &(v0_s32[i]) );            // src   = 0xAABBGGRR
        bits_s32x4  = vandq_s32(src_s32x4, mask_s32x4);     // bits  = 0xFF000000 & src
        bits_s32x4  = vceqq_s32(bits_s32x4, NODATA_s32x4);  // bits  = bits == 0 ? -1 : 0
        accum_s32x4 = vaddq_s32(accum_s32x4, bits_s32x4);   // accum = accum + bits
    }//for
    
    // FINISH HIM
    int32x2_t accum_s32x2 = vpadd_s32( vget_low_s32(accum_s32x4),
                                      vget_high_s32(accum_s32x4));
    
    int32_t   accum_s32x1 = vget_lane_s32(accum_s32x2, 0)
                          + vget_lane_s32(accum_s32x2, 1);
    
    _dc = (uint32_t)((int32_t)n + accum_s32x1); // accumulator will be negated
#endif
    
    return _dc;
}//_GetDataCount_RGBA8888_NEON



static FORCE_INLINE uint32_t _GetDataCount_RGBA8888(const uint32_t* v0,
                                                    const size_t    n)
{
    uint32_t dc             = 0;
    size_t   lastCleanWidth = n;
    bool     useNEON        = false;
    
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    useNEON = true;
#endif
    
    if (useNEON)
    {
        if (n > 4)
        {
            lastCleanWidth = n - n % 4;
            
            dc += _GetDataCount_RGBA8888_NEON(v0, lastCleanWidth);
            
            if (n != lastCleanWidth)
            {
                dc += _GetDataCount_RGBA8888_Scalar(v0 + lastCleanWidth, n - lastCleanWidth);
            }//if
        }//else if
        else
        {
            dc += _GetDataCount_RGBA8888_Scalar(v0, n);
        }//else
    }//if
    else
    {
        dc = _GetDataCount_RGBA8888_Scalar(v0, n);
    }//else
    
    return dc;
}//_GetDataCount

uint32_t gbStats_GetDataCount_RGBA8888(const uint32_t* v0,
                                       const size_t    n)
{
    return _GetDataCount_RGBA8888(v0, n);
}//_gbStats_GetDataCount_RGBA8888

uint32_t gbStats_GetDataCountROI_RGBA8888(const uint32_t* v0,
                                          const size_t    width,
                                          const size_t    rwx0,
                                          const size_t    rwy0,
                                          const size_t    rwx1,
                                          const size_t    rwy1)
{
    uint32_t dc        = 0;
    size_t   procWidth = rwx1 - rwx0 + 1;
    size_t   idx;
    
    for (size_t y=rwy0; y<=rwy1; y++)
    {
        idx = y * width + rwx0;
        
        dc += _GetDataCount_RGBA8888(v0 + idx,
                                     procWidth);
    }//for
    
    return dc;
}//gbStats_GetDataCountROI_RGBA8888

bool gbStats_GetHasAnyData_RGBA8888(const uint32_t* v0,
                                    const size_t    n)
{
    // speculatively, just check the first pixel.
    if (v0[0] >> 24 != 0)
    {
        return true;
    }//if
    
    uint32_t dc        = 0;
    size_t   procWidth = MIN(256, n);
    
    for (size_t i = 0; i < n; i+= procWidth)
    {
        dc += _GetDataCount_RGBA8888(v0 + i,
                                     procWidth);
        
        if (dc > 0)
        {
            break;
        }//if
    }//for
    
    if (dc == 0 && n % procWidth != 0)
    {
        size_t lastCleanWidth = n - n % procWidth;
        
        dc += _GetDataCount_RGBA8888(v0 + lastCleanWidth,
                                     n  - lastCleanWidth);
    }//if
    
    return dc > 0;
}//gbStats_GetHasAnyData_RGBA8888

bool gbStats_GetHasAnyDataROI_RGBA8888(const uint32_t* v0,
                                       const size_t    width,
                                       const size_t    rwx0,
                                       const size_t    rwy0,
                                       const size_t    rwx1,
                                       const size_t    rwy1)
{
    // speculatively, just check the first pixel.
    if (v0[0] >> 24 != 0)
    {
        return true;
    }//if
    
    uint32_t dc        = 0;
    size_t   procWidth = rwx1 - rwx0 + 1;
    size_t   idx;
    
    for (size_t y=rwy0; y<=rwy1; y++)
    {
        idx = y * width + rwx0;
        
        dc += _GetDataCount_RGBA8888(v0 + idx,
                                     procWidth);
        
        if (dc > 0)
        {
            break;
        }//if
    }//for
    
    return dc > 0;
}//gbStats_GetHasAnyData_RGBA8888






















static inline size_t _GetResampleKernelExtent_ForInterpolationTypeId(const int interpolationTypeId)
{
    size_t extent = 0;
    
    switch (interpolationTypeId)
    {
        case kGB_Image_Interp_Lanczos3x3:
            extent = 3;
            break;
        case kGB_Image_Interp_Lanczos5x5:
            extent = 5;
            break;
        case kGB_Image_Interp_Bilinear:
            extent = 2;
            break;
        case kGB_Image_Interp_NN:
            extent = 1;
            break;
        case kGB_Image_Interp_EPX:
            extent = 1; // not really, but...
            break;
        case kGB_Image_Interp_Eagle:
            extent = 1; // not really, but...
            break;
        case kGB_Image_Interp_Average:
            extent = 1; // not really, but...
            break;
    }//switch
    
    return extent;
}//_GetResampleKernelExtent_ForInterpolationTypeId






// ================
// _vfill_u32_NEON:
// ================
//
// Sets n elements of vector dest to scalar x.
//
// This is a workaround for the single-byte limitations of ANSI C memset.
//
// Performs:
//           dest[i] = x;
//
static FORCE_INLINE void _vfill_u32_NEON(const uint32_t  x,
                                         uint32_t*       dest,
                                         const size_t    n)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    uint32x4_t x_u32x4 = vdupq_n_u32(x);
    
    for (size_t i=0; i<n; i+=4)
    {
        vst1q_u32( &(dest[i]), x_u32x4);
    }//for
#endif
}//_vfill_u32_NEON

// ==================
// _vfill_u32_scalar:
// ==================
//
// Sets n elements of vector dest to scalar x.
//
// This is a workaround for the single-byte limitations of ANSI C memset.
//
// Performs:
//           dest[i] = x;
//
static FORCE_INLINE void _vfill_u32_scalar(const uint32_t x,
                                           uint32_t*      dest,
                                           const size_t   n)
{
    for (size_t i=0; i<n; i++)
    {
        dest[i] = x;
    }//for
}//_vfill_u32_scalar

// ===========
// _vfill_u32:
// ===========
//
// Sets n elements of vector dest to scalar x.
//
// This is a workaround for the single-byte limitations of ANSI C memset.
//
// Performs:
//           dest[i] = x;
//
static FORCE_INLINE void _vfill_u32(const uint32_t x,
                                    uint32_t*      dest,
                                    const size_t   n)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    if (n > 4)
    {
        size_t lastCleanWidth = n - n % 4;
        
        _vfill_u32_NEON(x, dest, lastCleanWidth);
        
        if (n % 4 != 0)
        {
            _vfill_u32_scalar(x,
                              dest + lastCleanWidth,
                              n    - lastCleanWidth);
        }//if
    }//if
    else
    {
        _vfill_u32_scalar(x, dest, n);
    }//else
#else
    _vfill_u32_scalar(x, dest, n);
#endif
}//_vfill_u32








// ===================================================
// gbImage_GetZoomedTile_NN_FromCrop_Bitmask_RGBA8888:
// ===================================================
//
// Using alpha == 0 as NODATA, creates a bitmask to use later with a bitwise
// AND for filtering resampling results and removing interpolations into NODATA
// areas.  (which are bad/wrong)
//
void gbImage_GetZoomedTile_NN_FromCrop_Bitmask_RGBA8888(const uint8_t* src,
                                                        const size_t   src_w,
                                                        const size_t   src_h,
                                                        const size_t   src_rb,
                                                        uint8_t*       dest,
                                                        const size_t   dest_w,
                                                        const size_t   dest_h,
                                                        const size_t   dest_rb)
{
    uint32_t*  src_u32 = (uint32_t*)src;
    uint32_t* dest_u32 = (uint32_t*)dest;
    
    const size_t zoomScaleX    = MAX(dest_w / src_w, 1);
    const size_t zoomScaleY    = MAX(dest_h / src_h, 1);
    const size_t srcProcWidth  = src_rb  / 4;
    const size_t destProcWidth = dest_rb / 4;

    size_t srcY = 0;
    size_t destY;
    size_t x;
    size_t srcY_srcProcWidth;
    size_t destY_destProcWidth;
    size_t yCopy;
    
    for (destY = 0; destY <= dest_h - zoomScaleY; destY += zoomScaleY)
    {
        srcY_srcProcWidth   = srcY  * srcProcWidth;
        destY_destProcWidth = destY * destProcWidth;
        
        for (x = 0; x < src_w; x++)
        {
            _vfill_u32(src_u32[srcY_srcProcWidth + x] >> 24 == 0 ? 0 : 0xFFFFFFFF,
                       dest_u32 + destY_destProcWidth + x * zoomScaleX,
                       zoomScaleX);
        }//for
        
        for (yCopy = destY + 1; yCopy < destY + zoomScaleY; yCopy++)
        {
            memcpy(dest_u32 + yCopy * destProcWidth,
                   dest_u32 + destY_destProcWidth,
                   dest_w * 4);
        }//for
        
        srcY++;
    }//for
}//gbImage_GetZoomedTile_NN_FromCrop_Bitmask_RGBA8888

// ==================================================
// gbImage_GetZoomedTile_NN_FromCrop_Normal_RGBA8888:
// ==================================================
//
// Standard boring nearest neighbor resample method.  Will work on any 32-bit
// type or 32-bit interleaved format as it just sets/copies values.
//
void gbImage_GetZoomedTile_NN_FromCrop_Normal_RGBA8888(const uint8_t* src,
                                                       const size_t   src_w,
                                                       const size_t   src_h,
                                                       const size_t   src_rb,
                                                       uint8_t*       dest,
                                                       const size_t   dest_w,
                                                       const size_t   dest_h,
                                                       const size_t   dest_rb)
{
    uint32_t*  src_u32 = (uint32_t*)src;
    uint32_t* dest_u32 = (uint32_t*)dest;
    
    const size_t zoomScaleX    = MAX(dest_w / src_w, 1);
    const size_t zoomScaleY    = MAX(dest_h / src_h, 1);
    const size_t srcProcWidth  = src_rb  / 4;
    const size_t destProcWidth = dest_rb / 4;
    
    size_t srcY = 0;
    size_t destY;
    size_t x;
    size_t srcY_srcProcWidth;
    size_t destY_destProcWidth;
    size_t yCopy;
    
    for (destY = 0; destY <= dest_h - zoomScaleY; destY += zoomScaleY)
    {
        srcY_srcProcWidth   = srcY  * srcProcWidth;
        destY_destProcWidth = destY * destProcWidth;
        
        for (x = 0; x < src_w; x++)
        {
            _vfill_u32(src_u32[srcY_srcProcWidth + x],
                       dest_u32 + destY_destProcWidth + x * zoomScaleX,
                       zoomScaleX);
        }//for
        
        for (yCopy = destY + 1; yCopy < destY + zoomScaleY; yCopy++)
        {
            memcpy(dest_u32 + yCopy * destProcWidth,
                   dest_u32 + destY_destProcWidth,
                   dest_w * sizeof(uint32_t));
        }//for
        
        srcY++;
    }//for
}//gbImage_GetZoomedTile_NN_FromCrop_Normal_RGBA8888






// do not see how to separate kernel
static FORCE_INLINE void _EPX_Core_RGBA8888(const uint32_t A,        // ( 0, -1)
                                            const uint32_t C,        // (-1,  0)
                                            const uint32_t P,        // ( 0,  0)
                                            const uint32_t B,        // ( 0, +1)
                                            const uint32_t D,        // (+1,  0)
                                            uint32_t*      dest1,
                                            uint32_t*      dest2,
                                            uint32_t*      dest3,
                                            uint32_t*      dest4)
{
    if (A != D && C != B)
    {
        *dest1 = C == A ? C : P;
        *dest2 = A == B ? B : P;
        *dest3 = D == C ? C : P;
        *dest4 = B == D ? B : P;
    }//if
    else
    {
        *dest1 = P;
        *dest2 = P;
        *dest3 = P;
        *dest4 = P;
    }//else
}//_EPX_Core_RGBA8888


static FORCE_INLINE void _EPX_ByRow_RGBA8888(const uint32_t* src0,
                                             const uint32_t* src1,
                                             const uint32_t* src2,
                                             uint32_t*       dest0,
                                             uint32_t*       dest1,
                                             const size_t    src_w)
{
    size_t  src_x = 0;
    size_t dest_x = 0;
    
    // clamp this to bounds of src, do not assume padding
    // handle edges out of loop to avoid branching
    
    _EPX_Core_RGBA8888(src0[src_x],
                       src1[src_x],     // normally -1
                       src1[src_x],
                       src1[src_x+1],
                       src2[src_x],
                       &dest0[dest_x],
                       &dest0[dest_x+1],
                       &dest1[dest_x],
                       &dest1[dest_x+1]);
    dest_x += 2;
    
    for (src_x=1; src_x < src_w - 1; src_x++)
    {
        _EPX_Core_RGBA8888(src0[src_x],
                           src1[src_x-1],
                           src1[src_x],
                           src1[src_x+1],
                           src2[src_x],
                           &dest0[dest_x],
                           &dest0[dest_x+1],
                           &dest1[dest_x],
                           &dest1[dest_x+1]);
        dest_x += 2;
    }//for
    
    _EPX_Core_RGBA8888(src0[src_x],
                       src1[src_x-1],
                       src1[src_x],
                       src1[src_x],     // normally +1
                       src2[src_x],
                       &dest0[dest_x],
                       &dest0[dest_x+1],
                       &dest1[dest_x],
                       &dest1[dest_x+1]);
}//_EPX_ByRow_RGBA8888



// ===============================================
// gbImage_GetZoomedTile_NN_FromCrop_EPX_RGBA8888:
// ===============================================
//
// Modified variant of NN with "Eric's Pixel Expansion", aka Scale2x, aka
// AdvMAME2x, a sort of weird branchy edge-detect filter.
//
// This is 2x only.
//
// http://en.wikipedia.org/wiki/Image_scaling
// http://www.mactech.com/articles/mactech/Vol.15/15.06/FastBlitStrategies/index.html
//
void gbImage_GetZoomedTile_NN_FromCrop_EPX_RGBA8888(const uint8_t* src,
                                                    const size_t   src_w,
                                                    const size_t   src_h,
                                                    const size_t   src_rb,
                                                    uint8_t*       dest,
                                                    const size_t   dest_w,
                                                    const size_t   dest_h,
                                                    const size_t   dest_rb)
{
    uint32_t*  src_u32 = (uint32_t*)src;
    uint32_t* dest_u32 = (uint32_t*)dest;
    
    const size_t  srcProcWidth =  src_rb / 4;
    const size_t destProcWidth = dest_rb / 4;
    
    size_t  srcY;
    size_t destY = 0;
    
    size_t  src0_idx;   // y-1
    size_t  src1_idx;   // y
    size_t  src2_idx;   // y+1
    size_t dest0_idx;   // y
    size_t dest1_idx;   // y+1
    
    // clamp this to bounds of src, do not assume padding
    // handle edges out of loop to avoid branching
    
    // <Row_0>
    _EPX_ByRow_RGBA8888(src_u32,
                        src_u32,
                        src_u32  +  srcProcWidth,
                        dest_u32,
                        dest_u32 + destProcWidth,
                        src_w);
    destY += 2;
    // </Row_0>
    
    
    // <Row_1...n-1>
    for (srcY = 1; srcY < src_h - 1; srcY++)
    {
        src0_idx  =  (srcY-1) *  srcProcWidth;
        src1_idx  =   srcY    *  srcProcWidth;
        src2_idx  =  (srcY+1) *  srcProcWidth;
        dest0_idx =  destY    * destProcWidth;
        dest1_idx = (destY+1) * destProcWidth;
        
        _EPX_ByRow_RGBA8888(src_u32  +  src0_idx,
                            src_u32  +  src1_idx,
                            src_u32  +  src2_idx,
                            dest_u32 + dest0_idx,
                            dest_u32 + dest1_idx,
                            src_w);
        destY += 2;
    }//for
    // </Row_1...n-1>
    
    
    // <Row_n>
    src0_idx  = ( srcY-1) *  srcProcWidth;
    src1_idx  =   srcY    *  srcProcWidth;
    src2_idx  =   srcY    *  srcProcWidth;
    dest0_idx =  destY    * destProcWidth;
    dest1_idx = (destY+1) * destProcWidth;

    _EPX_ByRow_RGBA8888(src_u32  +  src0_idx,
                        src_u32  +  src1_idx,
                        src_u32  +  src2_idx,
                        dest_u32 + dest0_idx,
                        dest_u32 + dest1_idx,
                        src_w);
    // </Row_n>
}//gbImage_GetZoomedTile_NN_FromCrop_EPX_RGBA8888





/*
void OLD_gbImage_GetZoomedTile_NN_FromCrop_EPX_RGBA8888(const uint8_t* src,
                                                    const size_t   src_w,
                                                    const size_t   src_h,
                                                    const size_t   src_rb,
                                                    uint8_t*       dest,
                                                    const size_t   dest_w,
                                                    const size_t   dest_h,
                                                    const size_t   dest_rb)
{
    uint32_t*  src_u32 = (uint32_t*)src;
    uint32_t* dest_u32 = (uint32_t*)dest;
    
    const size_t zoomScaleX    = MAX(dest_w / src_w, 1);
    const size_t zoomScaleY    = MAX(dest_h / src_h, 1);
    const size_t srcProcWidth  = src_rb  / 4;
    const size_t destProcWidth = dest_rb / 4;
    
    size_t srcY = 0;
    size_t destY;
    size_t x;
    size_t srcY_srcProcWidth;
    size_t destY_destProcWidth;
    
    uint32_t A;
    uint32_t C;
    uint32_t B;
    uint32_t D;
    uint32_t P;
    
    size_t idx_1;
    size_t idx_2;
    size_t idx_3;
    size_t idx_4;
    
    size_t idx_A;
    size_t idx_B;
    size_t idx_C;
    size_t idx_D;
    size_t idx_P;
    
    for (destY = 0; destY <= dest_h - zoomScaleY; destY += zoomScaleY)
    {
        srcY_srcProcWidth   = srcY  * srcProcWidth;
        destY_destProcWidth = destY * destProcWidth;
        
        for (x = 0; x < src_w; x++)
        {
            //  src
            // -----
            //   A
            // C P B        Normally, only "P" is used for NN.
            //   D
            
            //  dest
            // ------
            //  1  2
            //  3  4
            
            // annoyingly, clamp all the non-(0,0) indices as needed.
            
            idx_P = srcY_srcProcWidth + x;                                      // ( 0,  0)
            idx_A = (srcY - (srcY > 0 ? 1 : 0)) * srcProcWidth + x;             // ( 0, -1)
            idx_B = idx_P + (x + 1 < src_w ? 1 : 0);                            // (+1,  0)
            idx_C = idx_P - (x > 0 ? 1 : 0);                                    // (-1,  0)
            idx_D = (srcY + (srcY + 1 < src_h ? 1 : 0)) * srcProcWidth + x;     // ( 0, +1)
            
            idx_1 = destY_destProcWidth + x * zoomScaleX;                                       // ( 0,  0)
            idx_2 = idx_1 + (x + 1 < dest_w ? 1 : 0);                                           // ( 0, +1)
            idx_3 = (destY + (destY + 1 < dest_h ? 1 : 0)) * destProcWidth + x * zoomScaleX;    // (+1,  0)
            idx_4 = idx_3 + (x + 1 < dest_w ? 1 : 0);                                           // (+1, +1)

            A = src_u32[idx_A]; // order by mem order
            C = src_u32[idx_C];
            P = src_u32[idx_P];
            B = src_u32[idx_B];
            D = src_u32[idx_D];
            
            dest_u32[idx_1] = C == A && C != D && A != B ? A : P;  // latter case = NN
            dest_u32[idx_2] = A == B && A != C && B != D ? B : P;
            dest_u32[idx_3] = D == C && D != B && C != A ? C : P;
            dest_u32[idx_4] = B == D && B != A && D != C ? D : P;
        }//for
        
        srcY++;
    }//for
}//OLD_gbImage_GetZoomedTile_NN_FromCrop_EPX_RGBA8888
*/







void gbImage_GetZoomedTile_NN_FromCrop_Eagle_RGBA8888(const uint8_t* src,
                                                      const size_t   src_w,
                                                      const size_t   src_h,
                                                      const size_t   src_rb,
                                                      uint8_t*       dest,
                                                      const size_t   dest_w,
                                                      const size_t   dest_h,
                                                      const size_t   dest_rb)
{
    uint32_t*  src_u32 = (uint32_t*)src;
    uint32_t* dest_u32 = (uint32_t*)dest;
    
    const size_t zoomScaleX    = MAX(dest_w / src_w, 1);
    const size_t zoomScaleY    = MAX(dest_h / src_h, 1);
    const size_t srcProcWidth  = src_rb  / 4;
    const size_t destProcWidth = dest_rb / 4;
    
    size_t srcY = 0;
    size_t destY;
    size_t x;
    size_t srcY_srcProcWidth;
    size_t destY_destProcWidth;
    
    uint32_t A;
    uint32_t C;
    uint32_t B;
    uint32_t D;
    uint32_t P;
    
    uint32_t E;
    uint32_t F;
    uint32_t G;
    uint32_t H;
    
    size_t idx_1;
    size_t idx_2;
    size_t idx_3;
    size_t idx_4;
    
    size_t idx_A;
    size_t idx_B;
    size_t idx_C;
    size_t idx_D;
    size_t idx_P;
    
    size_t idx_E;
    size_t idx_F;
    size_t idx_G;
    size_t idx_H;
    
    for (destY = 0; destY <= dest_h - zoomScaleY; destY += zoomScaleY)
    {
        srcY_srcProcWidth   = srcY  * srcProcWidth;
        destY_destProcWidth = destY * destProcWidth;
        
        for (x = 0; x < src_w; x++)
        {
            // first:       |Then
            //. . . --\ PP  |E A F  --\ 1 2
            //. P . --/ PP  |C P B  --/ 3 4
            //. . .         |G D H
            //| IF C==E==A => 1=E
            //| IF A==F==B => 2=F
            //| IF C==G==D => 3=G
            //| IF B==H==D => 4=H
            
            // annoyingly, clamp all the non-(0,0) indices as needed.
            
            idx_P = srcY_srcProcWidth + x;                                      // ( 0,  0)
            idx_A = (srcY - (srcY > 0 ? 1 : 0)) * srcProcWidth + x;             // ( 0, -1)
            idx_B = idx_P + (x + 1 < src_w ? 1 : 0);                            // (+1,  0)
            idx_C = idx_P - (x > 0 ? 1 : 0);                                    // (-1,  0)
            idx_D = (srcY + (srcY + 1 < src_h ? 1 : 0)) * srcProcWidth + x;     // ( 0, +1)
            
            idx_E = idx_A - (x > 0 ? 1 : 0);
            idx_F = idx_A + (x + 1 < src_w ? 1 : 0);
            idx_G = idx_D - (x > 0 ? 1 : 0);
            idx_H = idx_D + (x + 1 < src_w ? 1 : 0);
            
            idx_1 = destY_destProcWidth + x * zoomScaleX;                                       // ( 0,  0)
            idx_2 = idx_1 + (x + 1 < dest_w ? 1 : 0);                                           // ( 0, +1)
            idx_3 = (destY + (destY + 1 < dest_h ? 1 : 0)) * destProcWidth + x * zoomScaleX;    // (+1,  0)
            idx_4 = idx_3 + (x + 1 < dest_w ? 1 : 0);                                           // (+1, +1)
            
            // order by mem order
            
            E = src_u32[idx_E];
            A = src_u32[idx_A];
            F = src_u32[idx_F];
            
            C = src_u32[idx_C];
            P = src_u32[idx_P];
            B = src_u32[idx_B];
            
            G = src_u32[idx_G];
            D = src_u32[idx_D];
            H = src_u32[idx_H];
            
            dest_u32[idx_1] = C == E && E == A ? E : P;
            dest_u32[idx_2] = A == F && F == B ? F : P;
            dest_u32[idx_3] = C == G && G == D ? G : P;
            dest_u32[idx_4] = B == H && H == D ? H : P;
        }//for
        
        srcY++;
    }//for
}//gbImage_GetZoomedTile_NN_FromCrop_Eagle_RGBA8888











// ===========================================
// gbImage_GetZoomedTile_NN_FromCrop_RGBA8888:
// ===========================================
//
// Primary entry point for boring NN resmapling methods.
//
void gbImage_GetZoomedTile_NN_FromCrop_RGBA8888(const uint8_t* src,
                                                const size_t   src_w,
                                                const size_t   src_h,
                                                const size_t   src_rb,
                                                uint8_t*       dest,
                                                const size_t   dest_w,
                                                const size_t   dest_h,
                                                const size_t   dest_rb,
                                                const bool     outputAsBitmask)
{
    if (outputAsBitmask)
    {
        gbImage_GetZoomedTile_NN_FromCrop_Bitmask_RGBA8888(src, src_w, src_h, src_rb, dest, dest_w, dest_h, dest_rb);
    }//if
    else
    {
        gbImage_GetZoomedTile_NN_FromCrop_Normal_RGBA8888(src, src_w, src_h, src_rb, dest,  dest_w, dest_h, dest_rb);
    }//else
}//gbImage_GetZoomedTile_NN_FromCrop_RGBA8888


// =============================================
// gbImage_ExtendEdgesRightAndDownOnly_RGBA8888:
// =============================================
//
// For use when enlarging an image with a resampling kernel other than NN, where
// the source data coudn't entirely fill the padded image size. (eg, the bottom
// right corner)
//
// It is expected the empty pixel values that have not been set are zeroed, and
// are not unintialized garbage values.
//
void gbImage_ExtendEdgesRightAndDownOnly_RGBA8888(uint8_t*     src,
                                                  const size_t width,
                                                  const size_t height,
                                                  const size_t rowBytes,
                                                  const size_t dataWidth,
                                                  const size_t dataHeight)
{
    uint32_t* src_u32 = (uint32_t*)src;
    size_t    y;
    size_t    y_height;
    uint32_t  pxVal;
    size_t    x;
    
    // fill right fragment of rows
    if (dataWidth > 0 && width > dataWidth)
    {
        for (y = 0; y < dataHeight; y++)
        {
            y_height = y * height;
            pxVal    = src_u32[y_height + dataWidth - 1];
            
            for (x = dataWidth; x < width; x++)
            {
                if (src_u32[y_height + x] == 0)
                {
                    src_u32[y_height + x] = pxVal;
                }//if
            }//for
        }//for
    }//if
    
    // fill entire rows at bottom
    if (dataHeight > 0 && height > dataHeight)
    {
        size_t ysb1_height;
        
        for (y = dataHeight; y < height; y++)
        {
            y_height    =  y    * height;
            ysb1_height = (y-1) * height;
            
            for (x = 0; x < width; x++)
            {
                if (src_u32[y_height + x] == 0)
                {
                    src_u32[y_height + x] = src_u32[ysb1_height + x];
                }//if
            }//for
        }//for
    }//if
}//gbImage_ExtendEdgesRightAndDownOnly_RGBA8888

// =========================
// _RGBA8888_to_R8_G8_B8_A8:
// =========================
//
// Lazy way of extracting each channel of a 32-bit interleaved pixel.
//
static FORCE_INLINE void _RGBA8888_to_R8_G8_B8_A8(const uint32_t rgba,
                                                  uint8_t*       r,
                                                  uint8_t*       g,
                                                  uint8_t*       b,
                                                  uint8_t*       a)
{
    *r = (rgba & 0x000000FF);
    *g = (rgba & 0x0000FF00) >> 8;
    *b = (rgba & 0x00FF0000) >> 16;
    *a = (rgba             ) >> 24;
}//_RGBA8888_to_R8_G8_B8_A8

// =============================
// _AccumulateRGBA8888_ToChSums:
// =============================
//
// Helper for gbImage_FillNODATA_Neighborhood_Mean_OtherSizes_Core_RGBA8888.
//
// This accumulates pixel values for later calculating the mean for each
// channel.
//
static FORCE_INLINE void _AccumulateRGBA8888_ToChSums(const uint32_t rgba,
                                                      double*        r_sum,
                                                      double*        g_sum,
                                                      double*        b_sum,
                                                      double*        a_sum,
                                                      double*        rgba_n)
{
    uint8_t r_u08;
    uint8_t g_u08;
    uint8_t b_u08;
    uint8_t a_u08;
    
    _RGBA8888_to_R8_G8_B8_A8(rgba, &r_u08, &g_u08, &b_u08, &a_u08);
    
    *r_sum  = *r_sum  + (double)r_u08;
    *g_sum  = *g_sum  + (double)g_u08;
    *b_sum  = *b_sum  + (double)b_u08;
    *a_sum  = *a_sum  + (double)a_u08;
    *rgba_n = *rgba_n + 1.0;
}//_AccumulateRGBA8888_ToChSums


// =======================
// _GetRGBA8888_ForChSums:
// =======================
//
// Helper for gbImage_FillNODATA_Neighborhood_Mean_OtherSizes_Core_RGBA8888.
//
// After the pixel values for a cell neighborhood have been accumulated, this
// returns a single interleaved pixel value representing their mean.
//
static FORCE_INLINE uint32_t _GetRGBA8888_ForChSums(double r_sum,
                                                    double g_sum,
                                                    double b_sum,
                                                    double a_sum,
                                                    double rgba_n)
{
    uint8_t r = r_sum / rgba_n + 0.5;
    uint8_t g = g_sum / rgba_n + 0.5;
    uint8_t b = b_sum / rgba_n + 0.5;
    uint8_t a = a_sum / rgba_n + 0.5;
    
    uint32_t rgba = a << 24 | b << 16 | g << 8 || r;
    
    return rgba;
}//_GetRGBA8888_ForChSums



// =============================================================
// gbImage_FillNODATA_Neighborhood_Mean_OtherSizes_Core_RGBA8888
// =============================================================
//
// For use when enlarging an image with a resampling kernel other than nearest
// neighbor.
//
// Performs a NODATA fill (where NODATA is alpha == 0) on RGBA8888 image src.
//
// If cellSize is even, the neighborhood will be to the right and down only.
//
// This is optimized for sparse data; eg, it scans for non-zero alpha values,
// not alpha values of zero.
//
// "smoothing" will use the results that have already been generated, which
// presumably creates a lot of stalling.  It can also result in visual
// streaking artifacts.  But, when only used temporarily for resample kernel
// fodder, it should be used to fill as much NODATA as possible.
//
void gbImage_FillNODATA_Neighborhood_Mean_OtherSizes_Core_RGBA8888(uint8_t*     src,
                                                                   const int    cellSize,
                                                                   const size_t rwx0,
                                                                   const size_t rwy0,
                                                                   const size_t rwx1,
                                                                   const size_t rwy1,
                                                                   const size_t width,
                                                                   const size_t height,
                                                                   const bool   smoothing,
                                                                   uint8_t*     tempBuffer,
                                                                   const size_t tempBufferBytes)
{
    const size_t n         = width * height;
    size_t       dataFound = 0;
    bool         didMalloc = false;
    
    if (tempBufferBytes == 0)
    {
        didMalloc  = true;
        tempBuffer = malloc(sizeof(uint32_t) * n);
    }//if
    
    uint32_t*  src_u32        = (uint32_t*)src;
    uint32_t*  tempBuffer_u32 = (uint32_t*)tempBuffer;
    size_t     _cellSize      = (size_t)cellSize;
    
    _cellSize = _cellSize > width && _cellSize > height ? MAX(width, height) : _cellSize;
    
    const size_t cellOffsetBack = _cellSize % 2 == 0 ? _cellSize / 2 - 1 : (_cellSize - 1) / 2;
    const size_t cellOffsetFwd  = _cellSize % 2 == 0 ? _cellSize / 2     : (_cellSize - 1) / 2;
    
    memcpy(tempBuffer, src, sizeof(uint32_t) * n);
    
    size_t startAX;
    size_t startAY;
    size_t endAX;
    size_t endAY;
    size_t y_width;
    
    size_t aY_width;
    
    size_t y;
    size_t x;
    size_t aY;
    size_t aX;
    
    uint32_t mean_rgba;
    
    double r_sum;
    double g_sum;
    double b_sum;
    double a_sum;
    double rgba_n;
    
    // moarSmooth: shouldn't be used if this is windowed, or lack of overlap factor will cause
    //             some bad rendering artifacts
    const bool moarSmooth = smoothing && rwx0 == 0 && rwy0 == 0 && rwx1 == (width-1) && rwy1 == (height-1);
    
    for (y = rwy0; y <= rwy1; y++)
    {
        startAY = y >=        cellOffsetBack ? y - cellOffsetBack : rwy0;
        endAY   = y <= rwy1 - cellOffsetFwd  ? y + cellOffsetFwd  : rwy1;
        y_width = y * width;
        
        for (x = rwx0; x <= rwx1; x++)
        {
            if (tempBuffer_u32[y_width+x] >> 24 != 0)
            {
                dataFound++;
                
                startAX = x >=        cellOffsetBack ? x - cellOffsetBack : rwx0;
                endAX   = x <= rwx1 - cellOffsetFwd  ? x + cellOffsetFwd  : rwx1;
                
                r_sum  = 0.0;
                g_sum  = 0.0;
                b_sum  = 0.0;
                a_sum  = 0.0;
                rgba_n = 0.0;
                
                // 1. get mean value
                for (aY = startAY; aY <= endAY; aY++)
                {
                    aY_width = aY * width;
                    
                    for (aX = startAX; aX <= endAX; aX++)
                    {
                        if (tempBuffer_u32[aY_width + aX] >> 24 != 0)
                        {
                            _AccumulateRGBA8888_ToChSums(tempBuffer_u32[aY_width + aX], &r_sum, &g_sum, &b_sum, &a_sum, &rgba_n);
                        }//if
                        else if (moarSmooth && (src_u32[aY_width + aX] >> 24) != 0) // optional smoothing
                        {
                            _AccumulateRGBA8888_ToChSums(tempBuffer_u32[aY_width + aX], &r_sum, &g_sum, &b_sum, &a_sum, &rgba_n);
                        }//else if
                    }//for AX
                }//for AY
                
                mean_rgba = rgba_n == 0.0 ? 0.0 : _GetRGBA8888_ForChSums(r_sum, g_sum, b_sum, a_sum, rgba_n);
                
                // 2. set NODATAs to mean value
                for (aY = startAY; aY <= endAY; aY++)
                {
                    aY_width = aY * width;
                    
                    for (aX = startAX; aX <= endAX; aX++)
                    {
                        if (tempBuffer_u32[aY_width + aX] >> 24 == 0)
                        {
                            src_u32[aY_width + aX] = mean_rgba;
                        }//if
                    }//for AX
                }//for AY
            }//if
        }//for X
    }//for Y
    
    if (didMalloc)
    {
        free(tempBuffer);
        tempBuffer = NULL;
    }//if
}//gbImage_FillNODATA_Neighborhood_Mean_OtherSizes_Core_RGBA8888





// ==================
// _GetROI_ForZtoXYZ:
// ==================
//
// Returns the ROI in pixels for an image at src_z, relative to that tile,
// which will be used to fill a buffer at dest_x, dest_y @ dest_z.
//
// Note this result is only meaningful for nearest neighbor interpolation.
// All other methods have a resampling kernel extent that must be accounted
// for by padding.
//
static inline void _GetROI_ForZtoXYZ(const uint32_t src_z,
                                     const uint32_t dest_x,
                                     const uint32_t dest_y,
                                     const uint32_t dest_z,
                                     const size_t   w,
                                     const size_t   h,
                                     size_t*        roi_x,
                                     size_t*        roi_y,
                                     size_t*        roi_w,
                                     size_t*        roi_h)
{
    const uint32_t w_u32   = (uint32_t)w;
    const uint32_t h_u32   = (uint32_t)h;
    const uint32_t zs      = (dest_z - src_z);
    
    const uint32_t src_x   = dest_x >> zs;
    const uint32_t src_y   = dest_y >> zs;
    
    const uint32_t src_px  = src_x * w_u32;
    const uint32_t src_py  = src_y * h_u32;
    
    const uint32_t dest_px = dest_x * w_u32;
    const uint32_t dest_py = dest_y * h_u32;
    
    uint32_t sx = (dest_px >> zs) - src_px;
    uint32_t sy = (dest_py >> zs) - src_py;
    uint32_t sw = w_u32 >> zs;
    uint32_t sh = h_u32 >> zs;
    
    *roi_x = sx;
    *roi_y = sy;
    *roi_w = sw;
    *roi_h = sh;
}//_GetROI_ForZtoXYZ


// ================================
// _GetAdjustedROI_ForKernelExtent:
// ================================
//
// For use with _GetROI_ForZtoXYZ.
//
// After obtaining the base ROI, this provides the necessary padding for both
// the src and dest image buffers for the given resampling kernel extent.
//
// If the images are not padded, they will ALL have border artifacts.
//
static inline void _GetAdjustedROI_ForKernelExtent(const size_t kExtentPx,
                                                   const size_t w,
                                                   const size_t h,
                                                   const size_t roi_w,
                                                   const size_t roi_h,
                                                   size_t*      padded_w,
                                                   size_t*      padded_h,
                                                   size_t*      padded_roi_w,
                                                   size_t*      padded_roi_h)
{
    const size_t zoomScale   = w / roi_w;
    const size_t src_kReach  = kExtentPx % 2 != 0 ? (kExtentPx >> 1) + 1 : kExtentPx >> 1;
    const size_t dest_kReach = src_kReach * zoomScale;
    
    *padded_roi_w = roi_w + src_kReach;
    *padded_roi_h = roi_h + src_kReach;
    *padded_w     = w     + dest_kReach;
    *padded_h     = h     + dest_kReach;
}//_GetAdjustedROI_ForKernelExtent


// ==========================
// _ClampAdjustedROI_ToSrcWH:
// ==========================
//
// For use with _GetROI_ForZtoXYZ and _GetAdjustedROI_ForKernelExtent.
//
// Padding the src buffer naievely can result in a ROI that outside of the bounds
// of the image, which is bad.
//
// This clamps the ROI such that the ROI is constrainted to the bounds of the
// original image.
//
// Note, however, that this should not be used to change the size of the padded
// src image ultimately being resampled.  The reason is this will result in
// significant tile boundary artficats when the resampling kernel extent hits
// an edge too early.
//
// Thus, this should only be used to control what is copied from the src tile
// into the padded crop tile.  If this does any clamping, then the remainder
// should be filled in one of two ways:
//
// 1. A NODATA fill technique, such as edge extend and/or neighborhood mean
// 2. Loading 1-3 other tiles (if present) to provide actual image data.
//    Note that the tiles may not actually be present, so #1 will still be
//    required even here.
static inline void _ClampAdjustedROI_ToSrcWH(const size_t w,
                                             const size_t h,
                                             const size_t roi_x,
                                             const size_t roi_y,
                                             size_t*      padded_roi_w,
                                             size_t*      padded_roi_h)
{
    if (roi_x + *padded_roi_w > w)
    {
        *padded_roi_w = w - roi_x;
    }//if
    
    if (roi_y + *padded_roi_h > h)
    {
        *padded_roi_h = h - roi_y;
    }//if
}//_ClampAdjustedROI_ToSrcWH


// ====================
// _CopyByRow_RGBA8888:
// ====================
//
// Performs a per-row copy from src to dest.  Assumes origin are the same.
//
// This is used for copying data by row to buffers of dissimilar width.
//
static inline void _CopyByRow_RGBA8888(const uint8_t*  src,
                                       const size_t    src_width,
                                       const size_t    src_height,
                                       const size_t    src_rowBytes,
                                       uint8_t*        dest,
                                       const size_t    dest_width,
                                       const size_t    dest_height,
                                       const size_t    dest_rowBytes)
{
    const size_t copyBytes = MIN(src_width, dest_width) * 4;
    
    for (size_t y = 0UL; y < MIN(src_height, dest_height); y++)
    {
        memcpy(dest + (y * dest_rowBytes),
               src  + (y *  src_rowBytes),
               copyBytes);
    }//if
}//_CopyByRow_RGBA8888


// ===============
// _vand_u32_NEON:
// ===============
//
// Vectorized bitwise AND.
//
// Performs:
//           dest[i] = src0[i] & src1[i];
//
static FORCE_INLINE void _vand_u32_NEON(const uint32_t* src0,
                                        const uint32_t* src1,
                                        uint32_t*       dest,
                                        const size_t    n)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    uint32x4_t src0_u32x4;
    uint32x4_t src1_u32x4;
    uint32x4_t dest_u32x4;
    
    for (size_t i=0; i<n; i+=4)
    {
        src0_u32x4 = vld1q_u32( &(src0[i]) );
        src1_u32x4 = vld1q_u32( &(src1[i]) );
        dest_u32x4 = vandq_u32(src0_u32x4, src1_u32x4);
        
        vst1q_u32( &(dest[i]), dest_u32x4);
    }//for
#endif
}//_vand_u32_NEON

// =================
// _vand_u32_scalar:
// =================
//
// Scalar bitwise AND.
//
// Performs:
//           dest[i] = src0[i] & src1[i];
//
static FORCE_INLINE void _vand_u32_scalar(const uint32_t* src0,
                                          const uint32_t* src1,
                                          uint32_t*       dest,
                                          const size_t    n)
{
    for (size_t i=0; i<n; i++)
    {
        dest[i] = src0[i] & src1[i];
    }//for
}//_vand_u32_scalar

// ==========
// _vand_u32:
// ==========
//
// Vectorized bitwise AND.
//
// Performs:
//           dest[i] = src0[i] & src1[i];
//
static FORCE_INLINE void _vand_u32(const uint32_t* src0,
                                   const uint32_t* src1,
                                   uint32_t*       dest,
                                   const size_t    n)
{
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    if (n > 4)
    {
        size_t lastCleanWidth = n - n % 4;
        
        _vand_u32_NEON(src0, src1, dest, lastCleanWidth);
        
        if (n % 4 != 0)
        {
            _vand_u32_scalar(src0 + lastCleanWidth,
                             src1 + lastCleanWidth,
                             dest + lastCleanWidth,
                             n    - lastCleanWidth);
        }//if
    }//if
    else
    {
        _vand_u32_scalar(src0, src1, dest, n);
    }//else
#else
    _vand_u32_scalar(src0, src1, dest, n);
#endif
}//_vand_u32


// =================
// _FilterByRow_u32:
// =================
//
// Combines a per-row copy of src into dest, gated by mask using bitwise AND.
// This supports src and dest having different widths.
//
// Performs:
//           dest[row] = src[row] & mask[row];
//
static inline void _FilterByRow_u32(const uint32_t* src,
                                    const size_t    src_w,
                                    const uint32_t* mask,
                                    const size_t    mask_w,
                                    uint32_t*       dest,
                                    const size_t    dest_w,
                                    const size_t    dest_h)
{
    const size_t min_width = MIN(MIN(dest_w, src_w), mask_w);
    
    for (size_t y = 0; y < dest_h; y++)
    {
        _vand_u32(src  + (y * src_w),
                  mask + (y * mask_w),
                  dest + (y * dest_w),
                  min_width);
    }//for
}//_FilterByRow_u32


// ============================
// gbImage_FillNODATA_RGBA8888:
// ============================
//
// Fills NODATA (alpha == 0) to mitigate artifacts when enlarging an image.
//
// Combines edge extend with neighborhood mean.
//
//      src: A crop from a ROI of a RGBA8888 buffer, padded for the reach of
//           the resampling kernel being used.
// padded_w: width actual (eg, rowbytes / 4).
// padded_h: height actual.
//   data_w: width of original ROI from image.
//   data_h: height of original ROI from image.
void gbImage_FillNODATA_RGBA8888(uint8_t*     src,
                                 const size_t padded_w,
                                 const size_t padded_h,
                                 const size_t data_w,
                                 const size_t data_h,
                                 const bool   shouldEdgeExtend,
                                 const bool   shouldFillNeighborhoodMean)
{
    if (shouldEdgeExtend)
    {
        gbImage_ExtendEdgesRightAndDownOnly_RGBA8888(src,
                                                     padded_w, padded_h, padded_w * sizeof(uint32_t),
                                                     data_w, data_h);
    }//if
    
    if (shouldFillNeighborhoodMean)
    {
        const size_t largestDiff = MAX(padded_w - data_w, padded_h - data_h);
        const int    cellSize    = (int)MAX(MIN(largestDiff * 2 + 2, padded_w), 3);
        
        gbImage_FillNODATA_Neighborhood_Mean_OtherSizes_Core_RGBA8888(src,
                                                                      cellSize,
                                                                      0, 0, padded_w-1, padded_h-1,
                                                                      padded_w, padded_h,
                                                                      true,
                                                                      NULL, 0);
    }//if
}//gbImage_FillNODATA_RGBA8888







// =======================================
// gbImage_Resize_EnlargeTile_NN_RGBA8888:
// =======================================
//
// Performs a nearest neighbor enlarging resample of a ROI from tile src into
// tile dest, given their tile x/y/z coordinates and width/height.
//
// This is also a wrapper for EPX resizes.
//
void gbImage_Resize_EnlargeTile_NN_RGBA8888(const uint8_t* src,
                                            uint8_t*       dest,
                                            const uint32_t src_z,
                                            const uint32_t dest_x,
                                            const uint32_t dest_y,
                                            const uint32_t dest_z,
                                            const size_t   w,
                                            const size_t   h,
                                            const int      interpolationTypeId,
                                            bool*          roiWasEmpty)
{
    size_t roi_x;
    size_t roi_y;
    size_t roi_w;
    size_t roi_h;
    
    _GetROI_ForZtoXYZ(src_z,
                      dest_x, dest_y, dest_z,
                      w, h,
                      &roi_x, &roi_y, &roi_w, &roi_h);
    
    *roiWasEmpty = !gbStats_GetHasAnyDataROI_RGBA8888((uint32_t*)src, w, roi_x, roi_y, roi_x + roi_w - 1, roi_y + roi_h - 1);
    
    if (*roiWasEmpty)
    {
        return;
    }//if
    
    const size_t rowBytes = w * 4;
    const size_t o        = (roi_y * rowBytes) + roi_x * 4;
    
    if (interpolationTypeId == kGB_Image_Interp_EPX)
    {
        gbImage_GetZoomedTile_NN_FromCrop_EPX_RGBA8888(src + o,
                                                       roi_w, roi_h, rowBytes,
                                                       dest,
                                                       w, h, rowBytes);
    }//if
    else if (interpolationTypeId == kGB_Image_Interp_Eagle)
    {
        gbImage_GetZoomedTile_NN_FromCrop_Eagle_RGBA8888(src + o,
                                                         roi_w, roi_h, rowBytes,
                                                         dest,
                                                         w, h, rowBytes);
    }//else if
    else
    {
        gbImage_GetZoomedTile_NN_FromCrop_Normal_RGBA8888(src + o,
                                                          roi_w, roi_h, rowBytes,
                                                          dest,
                                                          w, h, rowBytes);
    }//else
}//gbImage_Resize_EnlargeTile_NN_RGBA8888






// ============================================
// gbImage_Resize_EnlargeTile_Lanczos_RGBA8888:
// ============================================
//
// Performs a Lanczos enlarging resample of a ROI from tile src into
// tile dest, given their tile x/y/z coordinates and width/height.
//
// This additionally:
// 1. Pads src for the resampling kernel extent, to prevent tile boundary artifacts
// 2. Performs two types of NODATA fills, to both account for when src could not
//    completely fill the resample kernel extent, as well as prevent interpolation
//    into interior enclaves of NODATA.
// 3. Masks the Lanczos results with a NN resample of the alpha channel.  This
//    removes ringing artifacts and interpolations into NODATA regions.
//
// NOTE: this also wraps the bilinear resize as well, as it follows the same code path.
//
void gbImage_Resize_EnlargeTile_Lanczos_RGBA8888(const uint8_t*  src,
                                                 uint8_t*        dest,
                                                 const uint32_t  src_z,
                                                 const uint32_t  dest_x,
                                                 const uint32_t  dest_y,
                                                 const uint32_t  dest_z,
                                                 const size_t    w,
                                                 const size_t    h,
                                                 const int       interpolationTypeId,
                                                 bool*           roiWasEmpty)
{
    uint32_t* mask_rgba = NULL;
    uint32_t* temp_rgba = NULL;
    uint32_t* crop_rgba = NULL;
    
    // Note the filtering here was originally designed for and tested with
    // planar floating point data, not RGBA8888.  While these techniques are
    // more or less mandatory to make Lanczos work well with PlanarF types,
    // they produced much less impressive results with RGBA8888 data.
    //
    // FILTER_OUTPUT_WITH_BITMASK: Antialiasing in the src kills this.
    //                             The ringing artifacts are sadly preferable
    //                             to making the interpolated AA look blocky.
    //
    // NODATA_FILL_NEIGHBORHOOD:   Antialiasing also kills this.  Thanks Obama.
    //
    // NODATA_FILL_EDGE_EXTEND:    This one is still needed to prevent boundary
    //                             artifacts.
    
    const bool   FILTER_OUTPUT_WITH_BITMASK = false;
    const bool   NODATA_FILL_NEIGHBORHOOD   = false;
    const bool   NODATA_FILL_EDGE_EXTEND    = true;
    
    const size_t rowBytes                   = w * 4;
    
    size_t roi_x;
    size_t roi_y;
    size_t roi_w;
    size_t roi_h;
    size_t o;
    
    size_t rsExtent;
    size_t padded_roi_w;
    size_t padded_roi_h;
    size_t padded_w;
    size_t padded_h;
    
    size_t copy_w;
    size_t copy_h;
    
    // --- get ROI to zoom in on ---
    _GetROI_ForZtoXYZ(src_z, dest_x, dest_y, dest_z, w, h, &roi_x, &roi_y, &roi_w, &roi_h);
    o = (roi_y * rowBytes) + roi_x * 4;
    
    
    // --- are there no pixels to actually zoom in on? ---
    *roiWasEmpty = !gbStats_GetHasAnyDataROI_RGBA8888((uint32_t*)src, w, roi_x, roi_y, roi_x + roi_w - 1, roi_y + roi_h - 1);
    if (*roiWasEmpty) { return; }//if
    
    
    // --- create mask if specified, for clamping to original's NODATA cells with NN. ---
    if (FILTER_OUTPUT_WITH_BITMASK)
    {
        mask_rgba = malloc(sizeof(uint32_t) * w * h);
        gbImage_GetZoomedTile_NN_FromCrop_Bitmask_RGBA8888(src + o,
                                                           roi_w, roi_h, rowBytes,
                                                           (uint8_t*)mask_rgba,
                                                           w, h, rowBytes);
    }//if

    
    // --- calculate amount of padding needed for resampling kernel ---
    rsExtent = _GetResampleKernelExtent_ForInterpolationTypeId(interpolationTypeId);
    _GetAdjustedROI_ForKernelExtent(rsExtent, w, h, roi_w, roi_h, &padded_w, &padded_h, &padded_roi_w, &padded_roi_h);
    
    
    // --- clamp padding to actual image buffer size if needed (eg bottom-right corner) ---
    copy_w = padded_roi_w;
    copy_h = padded_roi_h;
    _ClampAdjustedROI_ToSrcWH(w, h, roi_x, roi_y, &copy_w, &copy_h);
    
    
    // --- copy src to a temp crop buffer to (likely) modify the pixels ---
    crop_rgba = malloc(sizeof(uint32_t) * padded_roi_w * padded_roi_h);
    memset(crop_rgba, 0, sizeof(uint32_t) * padded_roi_w * padded_roi_h);
    _CopyByRow_RGBA8888(src + o,
                        copy_w, copy_h, rowBytes,
                        (uint8_t*)crop_rgba,
                        copy_w, copy_h, padded_roi_w * 4);
    
    
    // --- fill any NODATA values as specified ---
    gbImage_FillNODATA_RGBA8888((uint8_t*)crop_rgba,
                                padded_roi_w, padded_roi_h,
                                copy_w, copy_h,
                                NODATA_FILL_EDGE_EXTEND,
                                NODATA_FILL_NEIGHBORHOOD);
    
    
    // --- allocate a temporary resampling output buffer,  ---
    // --- sized proportionately to the padded crop buffer ---
    temp_rgba = malloc(sizeof(uint32_t) * padded_w * padded_h);
    
    
    // --- actual core interpolation, one of several methods ---
    if (interpolationTypeId == kGB_Image_Interp_Lanczos3x3)
    {
        gbImage_Resize_vImage_Lanczos3x3_RGBA8888((uint8_t*)crop_rgba,
                                                  (uint8_t*)temp_rgba,
                                                  padded_roi_w, padded_roi_h, padded_roi_w * 4,
                                                  padded_w,     padded_h,     padded_w     * 4);
    }//if
    else if (interpolationTypeId == kGB_Image_Interp_Lanczos5x5)
    {
        gbImage_Resize_vImage_Lanczos5x5_RGBA8888((uint8_t*)crop_rgba,
                                                  (uint8_t*)temp_rgba,
                                                  padded_roi_w, padded_roi_h, padded_roi_w * 4,
                                                  padded_w,     padded_h,     padded_w     * 4);
    }//else if
    else
    {
        gbImage_Resize_Bilinear_RGBA8888((uint8_t*)crop_rgba,
                                         (uint8_t*)temp_rgba,
                                         padded_roi_w, padded_roi_h,
                                         padded_w,     padded_h);
    }//else
    
    
    // --- resample buffer and dest will be sized differently, ---
    // --- so copy per-row, with a mask if specified           ---
    if (FILTER_OUTPUT_WITH_BITMASK)
    {
        _FilterByRow_u32(temp_rgba, padded_w,
                         mask_rgba, w,
                         (uint32_t*)dest, w, h);
        
        free(mask_rgba);
        mask_rgba = NULL;
    }//if
    else
    {
        _CopyByRow_RGBA8888((uint8_t*)temp_rgba,
                            padded_w, padded_h, padded_w * 4,
                            dest,
                            w, h, rowBytes);
    }//else

    
    // --- cleanup ---
    free(temp_rgba);
    temp_rgba = NULL;
    
    free(crop_rgba);
    crop_rgba = NULL;
}//gbImage_Resize_EnlargeTile_Lanczos_RGBA8888





// ====================================
// gbImage_Resize_EnlargeTile_RGBA8888:
// ====================================
//
// Enlarges a ROI from tile src into tile dest, given their tile x/y/z
// coordinates and width/height.
//
// Performs additional filtering as required for the interpolation type.
//
void gbImage_Resize_EnlargeTile_RGBA8888(const uint8_t*  src,
                                         uint8_t*        dest,
                                         const uint32_t  src_z,
                                         const uint32_t  dest_x,
                                         const uint32_t  dest_y,
                                         const uint32_t  dest_z,
                                         const size_t    w,
                                         const size_t    h,
                                         const int       interpolationTypeId,
                                         bool*           roiWasEmpty)
{
    if (   interpolationTypeId == kGB_Image_Interp_Lanczos3x3
        || interpolationTypeId == kGB_Image_Interp_Lanczos5x5)
    {
        gbImage_Resize_EnlargeTile_Lanczos_RGBA8888(src,
                                                    dest,
                                                    src_z,
                                                    dest_x, dest_y, dest_z,
                                                    w, h,
                                                    interpolationTypeId,
                                                    roiWasEmpty);
    }//if
    else if (   interpolationTypeId == kGB_Image_Interp_NN
             || interpolationTypeId == kGB_Image_Interp_EPX
             || interpolationTypeId == kGB_Image_Interp_Eagle)
    {
        gbImage_Resize_EnlargeTile_NN_RGBA8888(src,
                                               dest,
                                               src_z,
                                               dest_x, dest_y, dest_z,
                                               w, h,
                                               interpolationTypeId,
                                               roiWasEmpty);
    }//else if
    else
    {
        // moved bilinear wrapper to Lanczos wrapper as same code path really
        
        gbImage_Resize_EnlargeTile_Lanczos_RGBA8888(src,
                                                    dest,
                                                    src_z,
                                                    dest_x, dest_y, dest_z,
                                                    w, h,
                                                    interpolationTypeId,
                                                    roiWasEmpty);
    }//else
}//gbImage_Resize_EnlargeTile



// ======= SIMPLE RESIZE CASE =========
//    256                                       256
// +------+                    +--+---+      +------+
// |      |2                   |xx|128|      |      |2
// |      |5     -> (z+1) ->   +--+   |  ->  |      |5
// |      |6                   |128   |      |      |6
// +------+                    +------+      +------+
//
// 1. Find ROI for src
// 2. Resize ROI into dest
// ... problem: lots of tile boundary artifacts
// ... problem: data interpolated with NODATA


// ======= BETTER RESIZE: FASTER =========
//    256                                       260             256
// +------+                    +--+---+      +------+        +------+
// |      |2                   |xx|130|      |      |2       |      |2
// |      |5     -> (z+1) ->   +--+   |  ->  |      |6   ->  |      |5
// |      |6     Lanczos 3x3   |130   |      |      |0       |      |6
// +------+                    +------+      +------+        +------+

// 1. Find ROI for src
// 2. Adjust ROI and add padding for resampling kernel extent
// 3. Copy src into two temporary crop images by row
//      - Crop0: Keep it around for later filtering
//      - Crop1: 1. Extend edges right and down
//               2. NODATA fill
// 4. Make a temporary dest scaled to the padded crop w/h
//

// 2. Resize ROI into dest
// ... but for anything except nearest neighbor, lots of tile boundary artifacts








// "HQ2x" scaler code, attempted port from C++
// Note: it didn't work and is undocumented.

/*
enum {
    diff_offset = (0x440 << 21) + (0x207 << 11) + 0x407,
    diff_mask   = (0x380 << 21) + (0x1f0 << 11) + 0x3f0,
};

uint32_t *yuvTable = NULL;
uint8_t rotate[256];

const uint8_t hqTable[256] = {
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 15, 12, 5,  3, 17, 13,
    4, 4, 6, 18, 4, 4, 6, 18, 5,  3, 12, 12, 5,  3,  1, 12,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 17, 13, 5,  3, 16, 14,
    4, 4, 6, 18, 4, 4, 6, 18, 5,  3, 16, 12, 5,  3,  1, 14,
    4, 4, 6,  2, 4, 4, 6,  2, 5, 19, 12, 12, 5, 19, 16, 12,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 12, 5,  3, 16, 12,
    4, 4, 6,  2, 4, 4, 6,  2, 5, 19,  1, 12, 5, 19,  1, 14,
    4, 4, 6,  2, 4, 4, 6, 18, 5,  3, 16, 12, 5, 19,  1, 14,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 15, 12, 5,  3, 17, 13,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 12, 5,  3, 16, 12,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 17, 13, 5,  3, 16, 14,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 13, 5,  3,  1, 14,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 12, 5,  3, 16, 13,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 12, 5,  3,  1, 12,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 12, 5,  3,  1, 14,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3,  1, 12, 5,  3,  1, 14,
};

static void initialize()
{
 
    //static bool initialized = false;
    //if (initialized == true) return;
    //initialized = true;
    
    if (yuvTable != NULL)
    {
        return;
    }//if
    
//    yuvTable = new uint32_t[32768];
    yuvTable = malloc(sizeof(uint32_t) * 32768);
    
    for (uint32_t i = 0; i < 32768; i++)
    {
        uint8_t R = (i >>  0) & 31;
        uint8_t G = (i >>  5) & 31;
        uint8_t B = (i >> 10) & 31;
        
        //bgr555->bgr888
        double r = (R << 3) | (R >> 2);
        double g = (G << 3) | (G >> 2);
        double b = (B << 3) | (B >> 2);
        
        //bgr888->yuv888
        double y = (r + g + b) * (0.25 * (63.5 / 48.0));
        double u = ((r - b) * 0.25 + 128.0) * (7.5 / 7.0);
        double v = ((g * 2.0 - r - b) * 0.125 + 128.0) * (7.5 / 6.0);
        
        yuvTable[i] = ((unsigned)y << 21) + ((unsigned)u << 11) + ((unsigned)v);
    }
    
    for(unsigned n = 0; n < 256; n++)
    {
        rotate[n] = ((n >> 2) & 0x11) | ((n << 2) & 0x88)
        | ((n & 0x01) << 5) | ((n & 0x08) << 3)
        | ((n & 0x10) >> 3) | ((n & 0x80) >> 5);
    }
}

static void terminate()
{
    //delete[] yuvTable;
    free(yuvTable);
    yuvTable = NULL;
}

static bool same(uint16_t x, uint16_t y) {
    return !((yuvTable[x] - yuvTable[y] + diff_offset) & diff_mask);
}

static bool diff(uint32_t x, uint16_t y) {
    return ((x - yuvTable[y]) & diff_mask);
}

static void grow(uint32_t* n)
{
    *n = *n | (*n << 16);
    *n = *n & 0x03e07c1f;
}

static uint16_t pack(uint32_t n)
{
    n &= 0x03e07c1f;
    return n | (n >> 16);
}

static uint16_t blend1(uint32_t A, uint32_t B)
{
    grow(&A);
    grow(&B);
    
    A = (A * 3 + B) >> 2;
    
    return pack(A);
}

static uint16_t blend2(uint32_t A, uint32_t B, uint32_t C)
{
    grow(&A);
    grow(&B);
    grow(&C);
    
    return pack((A * 2 + B + C) >> 2);
}

static uint16_t blend3(uint32_t A, uint32_t B, uint32_t C)
{
    grow(&A);
    grow(&B);
    grow(&C);
    
    return pack((A * 5 + B * 2 + C) >> 3);
}

static uint16_t blend4(uint32_t A, uint32_t B, uint32_t C)
{
    grow(&A);
    grow(&B);
    grow(&C);
    
    return pack((A * 6 + B + C) >> 3);
}

static uint16_t blend5(uint32_t A, uint32_t B, uint32_t C)
{
    grow(&A);
    grow(&B);
    grow(&C);
    
    return pack((A * 2 + (B + C) * 3) >> 3);
}

static uint16_t blend6(uint32_t A, uint32_t B, uint32_t C)
{
    grow(&A);
    grow(&B);
    grow(&C);
    
    return pack((A * 14 + B + C) >> 4);
}

static uint16_t blend(unsigned rule, uint16_t E, uint16_t A, uint16_t B, uint16_t D, uint16_t F, uint16_t H)
{
    switch(rule) { default:
        case  0: return E;
        case  1: return blend1(E, A);
        case  2: return blend1(E, D);
        case  3: return blend1(E, B);
        case  4: return blend2(E, D, B);
        case  5: return blend2(E, A, B);
        case  6: return blend2(E, A, D);
        case  7: return blend3(E, B, D);
        case  8: return blend3(E, D, B);
        case  9: return blend4(E, D, B);
        case 10: return blend5(E, D, B);
        case 11: return blend6(E, D, B);
        case 12: return same(B, D) ? blend2(E, D, B) : E;
        case 13: return same(B, D) ? blend5(E, D, B) : E;
        case 14: return same(B, D) ? blend6(E, D, B) : E;
        case 15: return same(B, D) ? blend2(E, D, B) : blend1(E, A);
        case 16: return same(B, D) ? blend4(E, D, B) : blend1(E, A);
        case 17: return same(B, D) ? blend5(E, D, B) : blend1(E, A);
        case 18: return same(B, F) ? blend3(E, B, D) : blend1(E, D);
        case 19: return same(D, H) ? blend3(E, D, B) : blend1(E, B);
    }
}

//dllexport
void filter_size(unsigned* width, unsigned* height)
{
    initialize();
    
    *width  = *width * 2;
    *height = *height * 2;
}

//dllexport
void filter_render(uint32_t *colortable,
                   uint32_t *output,
                   unsigned outpitch,
                   const uint16_t *input,
                   unsigned pitch,
                   unsigned width,
                   unsigned height)
{
    initialize();
    pitch >>= 1;
    outpitch >>= 2;
    
    if (colortable == NULL)
    {
        colortable = (uint32_t*)yuvTable;  // probably wrong(?)
    }//if
    
    //#pragma omp parallel for
    
    for(unsigned y = 0; y < height; y++)
    {
        const uint16_t *in = input + y * pitch;
        
        uint32_t *out0 = output + y * outpitch * 2;
        uint32_t *out1 = output + y * outpitch * 2 + outpitch;
        
        int prevline = (y == 0 ? 0 : pitch);
        int nextline = (y == height - 1 ? 0 : pitch);
        
        in++;
        
        *out0++ = 0;
        *out0++ = 0;
        *out1++ = 0;
        *out1++ = 0;
        
        for(unsigned x = 1; x < width - 1; x++)
        {
            uint16_t A = *(in - prevline - 1);
            uint16_t B = *(in - prevline + 0);
            uint16_t C = *(in - prevline + 1);
            uint16_t D = *(in - 1);
            uint16_t E = *(in + 0);
            uint16_t F = *(in + 1);
            uint16_t G = *(in + nextline - 1);
            uint16_t H = *(in + nextline + 0);
            uint16_t I = *(in + nextline + 1);
            uint32_t e = yuvTable[E] + diff_offset;
            
            uint8_t pattern;
            pattern  = diff(e, A) << 0;
            pattern |= diff(e, B) << 1;
            pattern |= diff(e, C) << 2;
            pattern |= diff(e, D) << 3;
            pattern |= diff(e, F) << 4;
            pattern |= diff(e, G) << 5;
            pattern |= diff(e, H) << 6;
            pattern |= diff(e, I) << 7;
            
            *(out0 + 0) = colortable[blend(hqTable[pattern], E, A, B, D, F, H)]; pattern = rotate[pattern];
            *(out0 + 1) = colortable[blend(hqTable[pattern], E, C, F, B, H, D)]; pattern = rotate[pattern];
            *(out1 + 1) = colortable[blend(hqTable[pattern], E, I, H, F, D, B)]; pattern = rotate[pattern];
            *(out1 + 0) = colortable[blend(hqTable[pattern], E, G, D, H, B, F)];
            
            in++;
            out0 += 2;
            out1 += 2;
        }//for x
        
        in++;
        *out0++ = 0; *out0++ = 0;
        *out1++ = 0; *out1++ = 0;
    }//for y
}//filter render
*/





