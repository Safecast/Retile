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
                                      const size_t   rowBytes);

void gbImage_Resize_Half_RGBA8888(const uint8_t*         src,
                                  const size_t     src_width,
                                  const size_t    src_height,
                                  const size_t  src_rowBytes,
                                  uint8_t*              dest,
                                  const size_t    dest_width,
                                  const size_t   dest_height,
                                  const size_t dest_rowBytes);

void gbImage_Resize_Half_vImage_Lanczos3x3_RGBA8888(const uint8_t* src,
                                                    uint8_t*       dest,
                                                    const size_t   src_w,
                                                    const size_t   src_h,
                                                    const size_t   src_rowBytes,
                                                    const size_t   dest_w,
                                                    const size_t   dest_h,
                                                    const size_t   dest_rowBytes);

#if defined (__cplusplus)
}
#endif

#endif