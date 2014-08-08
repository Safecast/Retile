#include "gbImage_png.h"


// BEST FRAMEWORK EVAR
static inline bool _AccelerateFuckYeah()
{
    bool accelerateFuckYeah = false;
    
#ifdef VIMAGE_CONVERSION_H
    accelerateFuckYeah = true;
#endif
    
    return accelerateFuckYeah;
}//_AccelerateFuckYeah()



// returns true if using ARM's neon.h or Intel's NEONvsSSE_5.h which has macros redefining NEON intrins to SSE/AVX
static inline bool _CanUseNEON()
{
    bool gotNEON = false;
    
#if defined (__ARM_NEON__) || defined(NEON2SSE_H)
    gotNEON = true;
#endif
    
    return gotNEON;
}//_CanUseNEON



// scalar-vector compare equality and find index
static inline size_t _svceqqi_u32_scalar(const uint32_t* src,
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




// vector-scalar uint16_t -> uint8_t narrowing move with conditional subtract (NEON version)
static inline void _vSIMD_vscgtsubcvtu8_NEON(const uint16_t* src,
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
}//_vSIMD_vscgtsubcvtu8_NEON

// vector-scalar uint16_t -> uint8_t narrowing move with conditional subtract (scalar version)
static inline void _vSIMD_vscgtsubcvtu8_scalar(const uint16_t* src,
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
}//_vSIMD_vscgtsubcvtu8_scalar

// vector-scalar uint16_t -> uint8_t narrowing move with conditional subtract (main entry point)
static inline void _vSIMD_vscgtsubcvtu8(const uint16_t* src,
                                        const uint16_t  thres,
                                        const uint16_t  x,
                                        uint8_t*        dest,
                                        const size_t    n)
{
    if (n >= 8 && _CanUseNEON())
    {
        size_t lastCleanWidth = n - n % 8;
        
        _vSIMD_vscgtsubcvtu8_NEON(src, thres, x, dest, lastCleanWidth);
        
        if (n % 8 != 0)
        {
            _vSIMD_vscgtsubcvtu8_scalar(src  + lastCleanWidth,
                                        thres,
                                        x,
                                        dest + lastCleanWidth,
                                        n - lastCleanWidth);
        }//if
    }//if
    else
    {
        _vSIMD_vscgtsubcvtu8_scalar(src, thres, x, dest, n);
    }//else
}//_vSIMD_vscgtsubcvtu8


// meh.
static inline void _RGBA8888_to_RGB888_InPlace_scalar(uint8_t*     src,
                                                      const size_t width,
                                                      const size_t height)
{
    if (width * height < 4)
    {
        return;
    }//if
    
    size_t rgbIdx = 3;
    
    for (size_t i = 4; i < width * height * 4; i += 4)
    {
        src[rgbIdx  ] = src[i  ];
        src[rgbIdx+1] = src[i+1];
        src[rgbIdx+2] = src[i+2];
        
        rgbIdx += 3;
    }//for
}//_RGBA8888_to_RGB888_InPlace_scalar


// LSB
// the following can be vectorized, should gain ~2x with AVX intrins, but requires 128-bit aligned src and dest.
// vectorization would also assume the png_color struct would always be packed and 3 bytes.
static inline void _RGB888_u32_to_png_color_a08(const uint32_t* src,
                                                png_color*      dest_rgb,
                                                const size_t    n)
{
    if (sizeof(png_color) == 3 && _AccelerateFuckYeah() && n > 0)
    {
        size_t width;
        size_t height;
        
        if (n % 64 == 0) // help out vImage's cache blocking if possible
        {
            width  = 64;
            height = n / width;
        }//if
        else
        {
            width  = n;
            height = 1;
        }//else
        
#ifdef VIMAGE_CONVERSION_H
        vImage_Buffer viDataSrc  = { (void*)src,      height, width, width * 4 };
        vImage_Buffer viDataDest = { (void*)dest_rgb, height, width, width * 3 };
        
        vImageConvert_RGBA8888toRGB888(&viDataSrc, &viDataDest, kvImageNoFlags);
#endif
    }//if
    else if (n > 0)
    {
        for (size_t i = 0; i < n; i++)
        {
            dest_rgb[i].red   = ((src[i]) & 0x000000FF);
            dest_rgb[i].green = ((src[i]) & 0x0000FF00) >> 8;
            dest_rgb[i].blue  = ((src[i]) & 0x00FF0000) >> 16;
        }//for
    }//else
}//_RGBA8888_u32_to_png_color_a08


// LSB
// the following can be vectorized, the simpler ->RGB case should gain ~2x with AVX intrins, but requires 128-bit aligned src and dest.
// vectorization would also assume the png_color struct would always be packed and 3 bytes.
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
        _RGB888_u32_to_png_color_a08(src, dest_rgb, n);
    }//else
}//_RGBA8888_u32_to_png_color_a08


// pushes RGBA value onto palette stack, updates corresponding value idat_idx, and increments pal_idx.
// if RGBA value was already on stack, just updates idat_idx.
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


// converts RGBA8888 src to palette rgbOut, alpha channel aOut (non-opaque first sorting), and image data indices idxOut if <= 256 distinct colors.
// note this is a lossless palletization ONLY.  no median cuts, no dithering.
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
        _vSIMD_vscgtsubcvtu8(idxs_u16,
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
		fprintf(stderr, "Can't write NULL src buffer for %s.\n", filename);
		code        = 1;
		shouldWrite = false;
    }//if
    
	if (fp == NULL)
    {
		fprintf(stderr, "Could not open file %s for writing\n", filename);
		code        = 1;
		shouldWrite = false;
	}//if
    
    if (shouldWrite)
    {
        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        
        if (png_ptr == NULL)
        {
            fprintf(stderr, "Could not allocate write struct\n");
            code        = 1;
            shouldWrite = false;
        }//if
    }//if

    if (shouldWrite)
    {
       	info_ptr = png_create_info_struct(png_ptr);
        
        if (info_ptr == NULL)
        {
            fprintf(stderr, "Could not allocate info struct\n");
            code        = 1;
            shouldWrite = false;
        }//if
    }//if

    if (shouldWrite)
    {
        result = setjmp(png_jmpbuf(png_ptr));
        
        if (result)
        {
            fprintf(stderr, "Error during png creation\n");
            code        = 1;
            shouldWrite = false;
        }//if
    }//if

    if (shouldWrite)
    {
        // <zlib>
        png_set_compression_level(png_ptr, 9);          // RLE=1
        png_set_compression_strategy(png_ptr, 0);       // RLE=3
        png_set_filter(png_ptr, 0, PNG_FILTER_NONE);    // filters are only useful for webpage gradients
        // </zlib>
        
        png_init_io(png_ptr, fp);
        
        int    _png_color_type = PNG_COLOR_TYPE_RGBA;
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
            
            png_set_PLTE(png_ptr, info_ptr, _p, (int)color_n);
            
            if (!isOpaque)
            {
                png_set_tRNS(png_ptr, info_ptr, &(_a[0]), (int)_nopIdx, NULL);
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
            // there can be a false positive at this point, but not a false negative.
            
            isOpaque = _IsOpaqueRGBA8888(src + 4 * _last_i,
                                         width * height - _last_i);
            
            if (isOpaque)
            {
                _png_color_type = PNG_COLOR_TYPE_RGB;
            }//if
        }//else if
        // </indexedColorQuant>
        
        const int    bitsPerComp = 8;
        const int    writeBPP    = _png_color_type == PNG_COLOR_TYPE_RGBA ? 32 : _png_color_type == PNG_COLOR_TYPE_RGB ? 24 : 8;
        const size_t rowBytes    = (width * writeBPP) >> 3;
        
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
        if (_png_color_type == PNG_COLOR_TYPE_PALETTE)
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
#ifdef VIMAGE_CONVERSION_H
            vImage_Buffer viDataSrc  = { src, height, width, width *  4              };
            vImage_Buffer viDataDest = { src, height, width, width * (writeBPP >> 3) };
            
            vImageConvert_RGBA8888toRGB888(&viDataSrc, &viDataDest, kvImageDoNotTile);
#else
            _RGBA8888_to_RGB888_InPlace_scalar(src, width, height);
#endif
            
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
        printf("read png: can't open file [%s]\n", filename);
        shouldRead = false;
    }//if
    
    if (shouldRead)
    {
        fread(header, 1, 8, fp);
        
        result = png_sig_cmp(header, 0, 8);
        
        if (result)
        {
            printf("read png: not PNG: %s.  hdr:[%02x %02x %02x %02x %02x %02x %02x %02x]\n",
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
            printf("read png: can't make read struct\n");
            shouldRead = false;
        }//if
    }//if

    
    if (shouldRead)
    {
        info_ptr = png_create_info_struct(png_ptr);
        
        if (!info_ptr)
        {
            printf("read png: can't make info struct\n");
            shouldRead = false;
        }//if
    }//if

    if (shouldRead)
    {
        result = setjmp(png_jmpbuf(png_ptr));
        
        if (result)
        {
            printf("read png: err during init_io\n");
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
        
        if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)     // Planar1/2/4 -> Planar8
        {
            png_set_expand_gray_1_2_4_to_8(png_ptr);
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
            printf("read png: err during read image\n");
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

















