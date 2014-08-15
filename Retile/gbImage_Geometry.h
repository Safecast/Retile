#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sqlite3.h"
#include "unistd.h"
#include <Accelerate/Accelerate.h>
#include "NEONvsSSE_5.h" // https://software.intel.com/en-us/blogs/2012/12/12/from-arm-neon-to-intel-mmxsse-automatic-porting-solution-tips-and-tricks

#ifndef gbImage_Geometry_h
#define gbImage_Geometry_h

#if defined (__cplusplus)
extern "C" {
#endif
    
typedef int GB_Image_InterpolationType; enum
{
    kGB_Image_Interp_NN         = 0,
    kGB_Image_Interp_Bilinear   = 1,
    kGB_Image_Interp_Lanczos3x3 = 2,
    kGB_Image_Interp_Lanczos5x5 = 3,
    kGB_Image_Interp_Average    = 4,
    kGB_Image_Interp_EPX        = 5,
    kGB_Image_Interp_Eagle      = 6,
    kGB_Image_Interp_XBR        = 7
};

void gbImage_Resize_HalfTile_RGBA8888(const uint8_t* src,
                                      const uint32_t src_x,
                                      const uint32_t src_y,
                                      const uint32_t src_z,
                                      uint8_t*       dest,
                                      const uint32_t dest_x,
                                      const uint32_t dest_y,
                                      const uint32_t dest_z,
                                      const size_t   Bpp,
                                      const size_t   width,
                                      const size_t   height,
                                      const size_t   rowBytes,
                                      const int      interpolationTypeId);

void gbImage_Resize_Half_AverageNODATA_RGBA8888(const uint8_t* src,
                                                const size_t   src_width,
                                                const size_t   src_height,
                                                const size_t   src_rowBytes,
                                                uint8_t*       dest,
                                                const size_t   dest_width,
                                                const size_t   dest_height,
                                                const size_t   dest_rowBytes,
                                                const int      interpolationTypeId);
    
void gbImage_Resize_EnlargeTile_RGBA8888(const uint8_t*  src,
                                         uint8_t*        dest,
                                         const uint32_t  src_z,
                                         const uint32_t  dest_x,
                                         const uint32_t  dest_y,
                                         const uint32_t  dest_z,
                                         const size_t    w,
                                         const size_t    h,
                                         const int       interpolationTypeId,
                                         bool*           roiWasEmpty);

    
    
// the following should not be used directly, they are exposed for testing.
    
void gbImage_Resize_vImage_Lanczos3x3_RGBA8888(const uint8_t* src,
                                               uint8_t*       dest,
                                               const size_t   src_w,
                                               const size_t   src_h,
                                               const size_t   src_rowBytes,
                                               const size_t   dest_w,
                                               const size_t   dest_h,
                                               const size_t   dest_rowBytes);

void gbImage_Resize_vImage_Lanczos5x5_RGBA8888(const uint8_t* src,
                                               uint8_t*       dest,
                                               const size_t   src_w,
                                               const size_t   src_h,
                                               const size_t   src_rowBytes,
                                               const size_t   dest_w,
                                               const size_t   dest_h,
                                               const size_t   dest_rowBytes);
    
void gbImage_Resize_Bilinear_RGBA8888(const uint8_t* src,
                                      uint8_t*       dest,
                                      const size_t   src_w,
                                      const size_t   src_h,
                                      const size_t   dest_w,
                                      const size_t   dest_h);
    
void gbImage_GetZoomedTile_NN_FromCrop_EPX_RGBA8888(const uint8_t* src,
                                                    const size_t   src_w,
                                                    const size_t   src_h,
                                                    const size_t   src_rb,
                                                    uint8_t*       dest,
                                                    const size_t   dest_w,
                                                    const size_t   dest_h,
                                                    const size_t   dest_rb);
    
void gbImage_GetZoomedTile_NN_FromCrop_Eagle_RGBA8888(const uint8_t* src,
                                                      const size_t   src_w,
                                                      const size_t   src_h,
                                                      const size_t   src_rb,
                                                      uint8_t*       dest,
                                                      const size_t   dest_w,
                                                      const size_t   dest_h,
                                                      const size_t   dest_rb);

void gbImage_GetZoomedTile_NN_FromCrop_Normal_RGBA8888(const uint8_t* src,
                                                       const size_t   src_w,
                                                       const size_t   src_h,
                                                       const size_t   src_rb,
                                                       uint8_t*       dest,
                                                       const size_t   dest_w,
                                                       const size_t   dest_h,
                                                       const size_t   dest_rb);
    
void gbImage_Resize_EnlargeTile_NN_RGBA8888(const uint8_t* src,
                                            uint8_t*       dest,
                                            const uint32_t src_z,
                                            const uint32_t dest_x,
                                            const uint32_t dest_y,
                                            const uint32_t dest_z,
                                            const size_t   w,
                                            const size_t   h,
                                            const int      interpolationTypeId,
                                            bool*          roiWasEmpty);

void gbImage_GetZoomedTile_NN_FromCrop_XBR_RGBA8888(const uint8_t* src,
                                                    const size_t   src_w,
                                                    const size_t   src_h,
                                                    const size_t   src_rb,
                                                    uint8_t*       dest,
                                                    const size_t   dest_w,
                                                    const size_t   dest_h,
                                                    const size_t   dest_rb);


#if defined (__cplusplus)
}
#endif

#endif