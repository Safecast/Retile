#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <Accelerate/Accelerate.h>
#include "sqlite3.h"
#include "unistd.h"
#include <libpng15/png.h> // http://ethan.tira-thompson.com/Mac_OS_X_Ports.html
#include "NEONvsSSE_5.h" // https://software.intel.com/en-us/blogs/2012/12/12/from-arm-neon-to-intel-mmxsse-automatic-porting-solution-tips-and-tricks

#ifndef gbImage_png_h
#define gbImage_png_h

#if defined (__cplusplus)
extern "C" {
#endif

int gbImage_PNG_Write_RGBA8888(const char*  filename,
                               const size_t width,
                               const size_t height,
                               uint8_t*     src);
    
void gbImage_PNG_Read_RGBA8888(const char* filename,
                               uint32_t**  dest,
                               size_t*     width,
                               size_t*     height,
                               size_t*     rowBytes);
    
#if defined (__cplusplus)
}
#endif

#endif