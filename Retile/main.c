//
//  main.c
//  Retile
//
//  Created by Nicholas Dolezal on 7/27/14.
//  Copyright (c) 2014 Nicholas Dolezal. All rights reserved.
//
//  This code is released into the public domain.
//

// =====================
// Platform compatiblity
// =====================
//
//
// Mac
// ---
// This is built, tested and used on OS X 10.9.  With a new libpng framework
// binary for ARM it should work on iOS too, although I have no idea why anyone
// would run this on iOS.
//
//
//
// Linux/BSD/etc
// -------------
// With some modification of the build process and header references it
// should run on anything posix-y, though it will lose the Apple frameworks
// Accelerate and libdispatch.  This is untested.
//
//
//
// Windows
// -------
// See Linux, only now you'll need to get rid of the posix stuff.
//
// This is mostly the mkdir function calls.
// "tinydir.h" should provide Windows compatibility for reading, though.
//
// CURRENT_TIMESTAMP: this uses a posix call and will need to be fixed/removed.
// dbFilePath:        /tmp/retile.sqlite probably isn't going to work.
//
// Note that it's possible there may be other issues with the path parsing
// and generation in Windows, though this seems unlikely.
//


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <errno.h>  // for mkdir
#include <libgen.h> // for mkdir

#include <sys/types.h>  // posix - for mkdir
#include <sys/stat.h>   // posix - for mkdir
#include <sys/time.h>   // posix

#include "gbImage_png.h"
#include "gbImage_Geometry.h"
#include "gbDB.h"

#include "tinydir.h"        // https://github.com/cxong/tinydir/blob/master/tinydir.h
#include "sqlite3.h"

#include <Accelerate/Accelerate.h>




typedef int Retile_TemplateFormatType; enum
{
    kRetile_Template_OSM = 0,
    kRetile_Template_ZXY = 1,
    kRetile_Template_XYZ = 2
};

typedef int Retile_OpModeType; enum
{
    kRetile_OpMode_Downsample = 0,
    kRetile_OpMode_Enlarge    = 1,
};



// ==================
// _atoi_single_char:
// ==================
//
// atoi for a single character.
//
// Terrible utility function.  Returns UINT8_MAX if not in set [0-9].
//
static inline uint8_t _atoi_single_char(const char x)
{
    return x == '0' ? 0 : x == '1' ? 1 : x == '2' ? 2 : x == '3' ? 3
         : x == '4' ? 4 : x == '5' ? 5 : x == '6' ? 6 : x == '7' ? 7
         : x == '8' ? 8 : x == '9' ? 9 : UINT8_MAX;
}//atoi_single_char


// =============
// _atoi_by_idx:
// =============
//
// String to unsigned integer for src, with index and length params.
//
// Annoyingly, the basic atoi doesn't have a length parameter.
//
static inline uint32_t _atoi_by_idx(const char*  src,
                                    const size_t startIdx,
                                    const size_t len)
{
    uint32_t dec = 1;
    uint32_t x   = 0;
    uint32_t val;
    
    if (len > 0 && startIdx + len > 0)
    {
        for (size_t i=startIdx+len-1; i>=startIdx; i--)
        {
            val  = _atoi_single_char(src[i]);            
            x   += val != UINT8_MAX ? val * dec : 0;
            dec *= 10;
        }//for
    }//if

    return x;
}//_atoi_by_idx

// ====================
// _GetPathSlashOffset:
// ====================
//
// Finds the zero-based index of the last slash character "/" in src.
//
// skip_n skips the first n result indices.
//
static inline size_t _GetPathSlashOffset(const char*  src,
                                         const size_t skip_n)
{
    size_t offset     = 0;
    size_t skip_count = 0;
    
    for (int i = (int)strnlen(src, 1024) - 1; i >= 0; i--)
    {
        if (src[i] == '/')
        {
            if (skip_count < skip_n)
            {
                skip_count++;
            }//if
            else
            {
                offset = i;
                break;
            }//else
        }//if
    }//for
    
    return offset;
}//_GetPathSlashOffset


// ===============================
// _StringByAppendingPathComponent
// ===============================
//
// Concatenates path and filename together, ignoring trailing slashes in path if present.
//
static inline void _StringByAppendingPathComponent(char*       dest,
                                                   const char* path,
                                                   const char* component)
{
    if (dest != NULL && path != NULL && component != NULL)
    {
        size_t o             = _GetPathSlashOffset(path, 0);
        const size_t pathlen = strnlen(path, 1024);
        const size_t complen = strnlen(component, 1024);
        bool   pathHasSlash  = o == pathlen - 1;
        bool   compHasSlash  = component[0] == '/';
        size_t idx           = 0;
        
        if (pathlen != 1024 && complen != 1024)
        {
            memcpy(dest, path, pathlen);
            
            idx += pathlen;
            
            if (!pathHasSlash && !compHasSlash)
            {
                dest[idx++] = '/';
            }//if
            
            memcpy(dest + idx, component, strnlen(component, 1024));
            
            idx += strnlen(component, 1024);
            
            dest[idx] = '\0';
        }//if
        else
        {
            printf("_StringByAppendingPathComponent: [ERR]: no null termination character found for buffer!\n");
        }//else
    }//if
    else
    {
        printf("_StringByAppendingPathComponent: [ERR]: NULL buffers: [%s]dest [%s]path [%s]component\n",
               dest == NULL ? "x" : "_",
               path == NULL ? "x" : "_",
               component == NULL ? "x" : "_");
    }//else
}//_StringByAppendingPathComponent




// =====================
// _GetPathFromFilepath:
// =====================
//
// Truncates filepath, removing the last path component if present.
//
// The last slash "/" in filepath is ignored, and any characters thereafter.
//
static inline void _GetPathFromFilepath(char*       dest,
                                        const char* filepath)
{
    const size_t o = _GetPathSlashOffset(filepath, 0);
    
    strncpy(dest, filepath, o);
    
    dest[o] = '\0';
}//_GetPathFromFilepath





// ==========================
// _TempParseFilenameXYZ_OSM:
// ==========================
//
// Parses tile XYZ from a fixed format:
// */{z}/{x}/{y}.*
//
// This is the standard OSM slippy map tilename template.
//
// Relies on chars: "_", "_", "_" and "."
// Offset is used to ignore irrelevant prefix path.
//
void _TempParseFilenameXYZ_OSM(const char*  src,
                               const size_t offset,
                               uint32_t*    x,
                               uint32_t*    y,
                               uint32_t*    z)
{
    //printf("Parsing filename [%s] o=%zu\n", src, offset);
    
    uint32_t _x = UINT32_MAX;
    uint32_t _y = UINT32_MAX;
    uint32_t _z = UINT32_MAX;
    size_t dot  = UINT32_MAX;
    size_t us[3];
    
    for (size_t i=0; i<3; i++)
    {
        us[i] = UINT32_MAX;
    }//for
    
    size_t uc = 0;
    
    //printf("offset=%zu, srclen=%zu\n", offset, strlen(src));
    
    for (size_t i=offset; i<strnlen(src, 1024); i++)
    {
        if      (src[i] == '/') { us[uc++] = i; }//if
        else if (src[i] == '.') {      dot = i; }//if
    }//for
    
    if (   us[0] != UINT32_MAX
        && us[1] != UINT32_MAX
        && us[2] != UINT32_MAX
        &&   dot != UINT32_MAX
        &&   dot  > us[2])
    {
        //printf("us[0] = %zu, us[1] = %zu, us[2] =%zu, dot=%zu, max=%zu\n", us[0], us[1], us[2], dot, (size_t)UINT32_MAX);
        
        size_t z_start = us[0] + 1;
        size_t z_len   = us[1] - z_start;
        
        size_t x_start = us[1] + 1;
        size_t x_len   = us[2] - x_start;
        
        size_t y_start = us[2] + 1;
        size_t y_len   = dot - y_start;

        _z = _atoi_by_idx(src, z_start, z_len);
        _x = _atoi_by_idx(src, x_start, x_len);
        _y = _atoi_by_idx(src, y_start, y_len);
    }//if
    
    //printf("Parsed x=%d, y=%d, z=%d\n", (int)_x, (int)_y, (int)_z);
    
    *x = _x;
    *y = _y;
    *z = _z;
}//_TempParseFilenameXYZ_OSM


// ======================
// _TempParseFilenameXYZ:
// ======================
//
// Parses tile XYZ from a fixed format:
// *_{x}_{y}_{z}.*
//
// This is a depricated proprietary legacy template.
//
// Relies on chars: "_", "_", "_" and "."
// Offset is used to ignore irrelevant prefix path.
//
void _TempParseFilenameXYZ(const char*  src,
                           const size_t offset,
                           uint32_t*    x,
                           uint32_t*    y,
                           uint32_t*    z)
{
    uint32_t _x = UINT32_MAX;
    uint32_t _y = UINT32_MAX;
    uint32_t _z = UINT32_MAX;
    size_t dot  = UINT32_MAX;
    size_t us[3];
    for (size_t i=0; i<3; i++)
    {
        us[i] = UINT32_MAX;
    }//for
    size_t uc = 0;
    
    //printf("offset=%zu, srclen=%zu\n", offset, strlen(src));
    
    for (size_t i=offset; i<strnlen(src, 1024); i++)
    {
             if (src[i] == '_') { us[uc++] = i; }//if
        else if (src[i] == '.') {      dot = i; }//if
    }//for
    
    if (   us[0] != UINT32_MAX
        && us[1] != UINT32_MAX
        && us[2] != UINT32_MAX
        &&   dot != UINT32_MAX
        &&   dot  > us[2])
    {
        //printf("us[0] = %zu, us[1] = %zu, us[2] =%zu, dot=%zu, max=%zu\n", us[0], us[1], us[2], dot, (size_t)UINT32_MAX);
        
        size_t x_start = us[0] + 1;
        size_t x_len   = us[1] - x_start;
        
        size_t y_start = us[1] + 1;
        size_t y_len   = us[2] - y_start;
        
        size_t z_start = us[2] + 1;
        size_t z_len   = dot - z_start;

        _x = _atoi_by_idx(src, x_start, x_len);
        _y = _atoi_by_idx(src, y_start, y_len);
        _z = _atoi_by_idx(src, z_start, z_len);
    }//if
    
    *x = _x;
    *y = _y;
    *z = _z;
}//_TempParseFilenameXYZ


// ======================
// _TempParseFilenameZXY:
// ======================
//
// Parses tile XYZ from a fixed format:
// *_{z}_{x}_{y}.*
//
// This is a depricated proprietary legacy template.
//
// Relies on chars: "_", "_", "_" and "."
// Offset is used to ignore irrelevant prefix path.
//
void _TempParseFilenameZXY(const char*  src,
                           const size_t offset,
                           uint32_t*    x,
                           uint32_t*    y,
                           uint32_t*    z)
{
    _TempParseFilenameXYZ(src, offset, z, x, y);
}//_TempParseFilenameZXY





// ======================
// _ParseXYZ_FromTemplate
// ======================
//
// Parses tile x/y/z from a filename with path.  Not from a template.  At all.
//
// This only currently handles three formats, and template is not used.
// Initially, it was hoped to have a universal parse and write template,
// but that failed.
//
void _ParseXYZ_FromTemplate(const char* src,
                            uint32_t*   x,
                            uint32_t*   y,
                            uint32_t*   z,
                            const int   urlTemplateId)
{
    switch (urlTemplateId)
    {
        case kRetile_Template_OSM:
            _TempParseFilenameXYZ_OSM(src, _GetPathSlashOffset(src, 3) + 1, x, y, z);;
            break;
        case kRetile_Template_ZXY:
            _TempParseFilenameZXY(src, _GetPathSlashOffset(src, 0), x, y, z);
            break;
        case kRetile_Template_XYZ:
            _TempParseFilenameXYZ(src, _GetPathSlashOffset(src, 0), x, y, z);
            break;
    }//switch
}//_ParseXYZ_FromTemplate


//Parsing filename [/Users/ndolezal/Downloads/TileGriddata/test/13/6/3520.png] o=39

// /Users/ndolezal/Downloads/TileGriddata/test/13/6/3520.png
//           1         2         3        |
// 0123456789012345678901234567890123456789

// =========================
// _ParseAnddAddFileXYZ_ToDB
// =========================
//
// Attempts to parse tile x/y/z from the filename, and writes a row to db
// if successful.  Waits for insert to complete.
//
// Not thread safe.
//
static inline void _ParseAnddAddFileXYZ_ToDB(const char*    filename,
                                             const char*    srcPath,
                                             sqlite3*       db,
                                             sqlite3_stmt*  insertStmt,
                                             size_t*        n,
                                             const int      urlTemplateId)
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
    char     filepath[1024];
    int      result;
    
    if (urlTemplateId == kRetile_Template_OSM)
    {
        _StringByAppendingPathComponent(filepath, srcPath, filename);
        _ParseXYZ_FromTemplate(filepath, &x, &y, &z, urlTemplateId);
    }//if
    else
    {
        _ParseXYZ_FromTemplate(filename, &x, &y, &z, urlTemplateId); // not sure why legacy weren't doing the above
    }//else
    
    if (x != UINT32_MAX && y != UINT32_MAX && z != UINT32_MAX
        && z > 0)
    {
        _StringByAppendingPathComponent(filepath, srcPath, filename);
        
        sqlite3_bind_int (insertStmt, 1, x);
        sqlite3_bind_int (insertStmt, 2, y);
        sqlite3_bind_int (insertStmt, 3, z);
        sqlite3_bind_text(insertStmt, 4, filepath, (int)strnlen(filepath, 1024), SQLITE_STATIC);
        
        // wait for DB write, since insert statement will be reused
        while (true)
        {
            result = sqlite3_step(insertStmt);
            
            if (result == SQLITE_DONE)
            {
                break;
            }//if
            else if (result != SQLITE_BUSY)
            {
                printf("gbDB_CompressTileAndWriteSync_ReusingDatabase: DB error writing tile (%d, %d, %d): %s\n", x, y, z, sqlite3_errmsg(db));
                break;
            }//if
        }//while
        
        sqlite3_clear_bindings(insertStmt);
        sqlite3_reset(insertStmt);
        
        *n = *n + 1;
    }//if
}//_ParseAnddAddFileXYZ_ToDB


// ==============
// _ParsePathToDB
// ==============
//
// Primary filesystem directory read function, which then invokes
// _ParseAndAddFileXYZ_ToDB to do something with the results.
//
// Uses tinydir.h in an attempt to be multiplatform, but this is untested.
//
// Allows for recursive scans.
//
static inline void _ParsePathToDB(const char*    srcPath,
                                  sqlite3*       db,
                                  sqlite3_stmt*  insertStmt,
                                  size_t*        n,
                                  const int      urlTemplateId,
                                  const bool     isRecursive)
{
    tinydir_dir dir;
    tinydir_open(&dir, srcPath);
    
    while (dir.has_next)
    {
        tinydir_file file;
        tinydir_readfile(&dir, &file);
        
        if (!file.is_dir
            && file.name[0] != '.') // ignore annoying things like .DS_Store (or, even worse, unpacked .filename metadata copying HFS -> FAT32)
        {
            //printf("Found file: [%s] ... path: [%s]\n", file.name, srcPath);
            
            _ParseAnddAddFileXYZ_ToDB(file.name, srcPath, db, insertStmt, n, urlTemplateId);
        }//if
        else if (isRecursive
                  && (strnlen(file.name, 1024) != 1 || strcmp(file.name, ".")  != 0)
                  && (strnlen(file.name, 1024) != 2 || strcmp(file.name, "..") != 0)
                  && file.name[0] != '.')
        {
            char* subPath = malloc(sizeof(char) * strnlen(srcPath, 1024) + 1UL); // heap to reduce stack pressure from multiple recursions
            
            _StringByAppendingPathComponent(subPath, srcPath, file.name);
            
            //printf("Found subpath: [%s]\n", subPath);
            
            _ParsePathToDB(subPath, db, insertStmt, n, urlTemplateId, isRecursive);
            
            free(subPath);
            subPath = NULL;
        }//if
        
        tinydir_next(&dir);
    }//while
    
    tinydir_close(&dir);
}//_ParsePathToDB



// =============
// _ReadPathToDB
// =============
//
// Reads files in srcPath recurisvely into a SQLite3 database at dbFilePath.
// The tile x/y/z is parsed from the filename and stored into INT columns
// for later data clustering.
//
// This is to avoid something like attemping to load every tile for that zoom
// level, which would be absurdly inefficient.
//
size_t _ReadPathToDB(const char* srcPath,
                     const char* dbFilePath,
                     const int   urlTemplateId)
{
    size_t        n = 0;
    sqlite3*      db;
    sqlite3_stmt* insertStmt;
    
    printf("Creating temporary DB (if needed)...\n");
    
    gbDB_CreateIfNeededDB(dbFilePath);
    gbDB_ExecSQL_Generic(dbFilePath, "CREATE TABLE IF NOT EXISTS TileRef(ID INTEGER PRIMARY KEY, X INT, Y INT, Z INT, FilePath VARCHAR(1024));");
    gbDB_ExecSQL_Generic(dbFilePath, "DELETE FROM TileRef;");
    
    gbDB_PrepConn_DBPath_CString(dbFilePath, "INSERT INTO TileRef(X, Y, Z, FilePath) VALUES (?, ?, ?, ?);", &db, &insertStmt);
    
    gbDB_BeginOrCommitTransactionReusingDB(db, true);
 
    _ParsePathToDB(srcPath, db, insertStmt, &n, urlTemplateId, true);
    
    printf("Closing DB and transaction...\n");
    
    gbDB_BeginOrCommitTransactionReusingDB(db, false);
    gbDB_CloseDBConnAndQueryStmt(db, insertStmt);
    
    if (n == 0 && srcPath)
    {
        printf("_ReadPathToDB: [ERR]  No files found at path [%s].\n", srcPath != NULL ? srcPath : "<NULL>");
    }//if
    
    return n;
}//_ReadPathToDB




// ===============================
// _GetFilenameForZXY_FromTemplate
// ===============================
//
// Terrible failed attempt at smart universal string parsing.
// This does not actually use template at all.
//
static inline void _GetFilenameForZXY_FromTemplate(const uint32_t x,
                                                   const uint32_t y,
                                                   const uint32_t z,
                                                   const char*    template,
                                                   char*          dest)
{
    sprintf(dest, "%d/TileExport_%d_%d_%d.png", z, z, x, y);
}//_GetFilenameForZXY_FromTemplate




// ===============================
// _GetFilenameForXYZ_FromTemplate
// ===============================
//
// Terrible failed attempt at smart universal string parsing.
// This does not actually use template at all.
//
static inline void _GetFilenameForXYZ_FromTemplate(const uint32_t x,
                                                   const uint32_t y,
                                                   const uint32_t z,
                                                   const char*    template,
                                                   char*          dest)
{
    sprintf(dest, "%d/griddata_%d_%d_%d.png", z, x, y, z);
}//_GetFilenameForXYZ_FromTemplate



// ===================================
// _GetFilenameForXYZ_FromTemplate_OSM
// ===================================
//
// Terrible failed attempt at smart universal string parsing.
// This does not actually use template at all.
//
static inline void _GetFilenameForXYZ_FromTemplate_OSM(const uint32_t x,
                                                       const uint32_t y,
                                                       const uint32_t z,
                                                       const char*    template,
                                                       char*          dest)
{
    sprintf(dest, "%d/%d/%d.png", z, x, y);
}//_GetFilenameForXYZ_FromTemplate



// ==========================
// _FixDestTileBufferIfNeeded
// ==========================
//
// Automatically fixes dest buffer size if initial guess is incorrect (256x256).
// This is not intended to support source tiles of varying sizes, which would
// be absurd anyway.
//
static inline void _FixDestTileBufferIfNeeded(uint32_t**   dest_tile,
                                              size_t*      dest_tile_width, 
                                              size_t*      dest_tile_height, 
                                              size_t*      dest_tile_rowBytes,
                                              const size_t width,
                                              const size_t height,
                                              const size_t rowBytes)
{
            if (width > *dest_tile_width || height > *dest_tile_height || rowBytes > *dest_tile_rowBytes)
            {
                printf("Fixing dest_tile buffer size. (%zu x %zu (%zu)) -> (%zu x %zu (%zu))\n",
                       *dest_tile_height, *dest_tile_width, *dest_tile_rowBytes, height, width, rowBytes);
                
                free(*dest_tile);
                
                *dest_tile           = NULL;
                *dest_tile_width     = width;
                *dest_tile_height    = height;
                *dest_tile_rowBytes  = rowBytes;
                uint32_t* _dest_tile = malloc(sizeof(uint8_t) * height * rowBytes);
                memset(_dest_tile, 0, sizeof(uint8_t) * height * rowBytes);
                
                *dest_tile          = _dest_tile;
            }//if
}//_FixDestTileBufferIfNeeded





// ==========================
// _IncDecrementWithSemaphore
// ==========================
//
// Terrible mutex boilerplate code for wrapping read, increment and decrement ops for x.
//
static inline int _IncDecrementWithSemaphore(int*                 x,
                                             dispatch_semaphore_t sema,
                                             const bool           isInc,
                                             const bool           isDec)
{
    int retVal = -1;
    
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
    
    if (isInc)
    {
        *x = *x + 1;
    }//if
    else if (isDec)
    {
        *x = *x - 1;
    }//else if
    
    retVal = *x;

    dispatch_semaphore_signal(sema);
    
    return retVal;
}//_DecrementWithSemaphore








// =============
// Retile_Buffer
// =============
//
// Simple internal struct for holding tile metadata + buffer data.
//
// Bad stuff will happen if data and filename aren't initialized with NULL.
//
typedef struct Retile_Buffer
{
    uint32_t* data;
    size_t    width;
    size_t    height;
    size_t    rowBytes;
    uint32_t  x;
    uint32_t  y;
    uint32_t  z;
    char*     filename;
    char*     dest_filename;
} Retile_Buffer;




// ==================
// _CopyRetileBuffers
// ==================
//
// Copies a collection of n Retile_Buffers to dest from src.
// Any non-null data/filenames will be malloc'd and copied.
//
static inline void _CopyRetileBuffers(Retile_Buffer* dest,
                                      Retile_Buffer* src,
                                      const size_t   n)
{
    for (size_t i = 0; i < n; i++)
    {
        dest[i].data          = NULL;
        dest[i].width         = src[i].width;
        dest[i].height        = src[i].height;
        dest[i].rowBytes      = src[i].rowBytes;
        dest[i].x             = src[i].x;
        dest[i].y             = src[i].y;
        dest[i].z             = src[i].z;
        dest[i].filename      = NULL;
        dest[i].dest_filename = NULL;
        
        if (src[i].data != NULL)
        {
            dest[i].data = malloc(sizeof(uint8_t) * dest[i].height * dest[i].rowBytes);
            
            memcpy( (dest[i].data), (src[i].data), sizeof(uint8_t) * dest[i].height * dest[i].rowBytes);
        }//if
        
        if (src[i].filename != NULL)
        {
            dest[i].filename = malloc(sizeof(char) * 1024);
            
            memcpy( (dest[i].filename), (src[i].filename), sizeof(char) * 1024);
        }//if
        
        if (src[i].dest_filename != NULL)
        {
            dest[i].dest_filename = malloc(sizeof(char) * 1024);
            
            memcpy( (dest[i].dest_filename), (src[i].dest_filename), sizeof(char) * 1024);
        }//if
    }//for
}//_CopyRetileBuffers



// ======================
// _FreeRetileBuffersData
// ======================
//
// Free anything that was malloc'd for a group of
// n retile buffers in array src.
//
static inline void _FreeRetileBuffersData(Retile_Buffer* src,
                                          const size_t   n)
{
    for (size_t i=0; i<n; i++)
    {
        if (src[i].data != NULL)
        {
            free(src[i].data);
                 src[i].data = NULL;
        }//if
        
        if (src[i].filename != NULL)
        {
            free(src[i].filename);
                 src[i].filename = NULL;
        }//if
        
        if (src[i].dest_filename != NULL)
        {
            free(src[i].dest_filename);
                 src[i].dest_filename = NULL;
        }//if
    }//for
}//_FreeRetileBuffersData



// ==================================================
// _DownsampleCompressAndWrite_RetileBuffers_RGBA8888
// ==================================================
//
// For references to a group of tiles in rt_bufs,
// read, decompess, resample, composite, compress and write a PNG file.
//
static inline void _DownsampleCompressAndWrite_RetileBuffers_RGBA8888(const char*    filepath,
                                                                      Retile_Buffer* rt_bufs,
                                                                      const size_t   rt_buf_n,
                                                                      const bool     alsoReprocessSrc,
                                                                      const int interpolationTypeId)
{
    size_t   local_width    = 256;
    size_t   local_height   = 256;
    size_t   local_rowBytes = 1024;
    uint32_t local_x        = 0;
    uint32_t local_y        = 0;
    uint32_t local_z        = 0;
    size_t   valid_n        = 0;
    
    uint32_t* local_rgba = malloc(sizeof(uint8_t) * local_height * local_rowBytes);
    
    memset(local_rgba, 0, sizeof(uint32_t) * local_height * local_width);
    
    for (size_t i = 0; i < rt_buf_n; i++)
    {
        if (rt_bufs[i].filename != NULL)
        {
            gbImage_PNG_Read_RGBA8888(  rt_bufs[i].filename,
                                      &(rt_bufs[i].data),
                                      &(rt_bufs[i].width),
                                      &(rt_bufs[i].height),
                                      &(rt_bufs[i].rowBytes));
            
            if (rt_bufs[i].data != NULL)
            {
                _FixDestTileBufferIfNeeded(&local_rgba,
                                           &local_width,     &local_height,     &local_rowBytes,
                                           rt_bufs[i].width, rt_bufs[i].height, rt_bufs[i].rowBytes);
                
                local_x = rt_bufs[i].x >> 1;
                local_y = rt_bufs[i].y >> 1;
                local_z = rt_bufs[i].z  - 1;
                
                gbImage_Resize_HalfTile_RGBA8888((uint8_t*)(rt_bufs[i].data),
                                                 rt_bufs[i].x, rt_bufs[i].y, rt_bufs[i].z,
                                                 (uint8_t*)local_rgba,
                                                 local_x, local_y, local_z,
                                                 rt_bufs[i].rowBytes / rt_bufs[i].width,
                                                 rt_bufs[i].width, rt_bufs[i].height, rt_bufs[i].rowBytes,
                                                 interpolationTypeId);
                
                if (alsoReprocessSrc)
                {
                    gbImage_PNG_Write_RGBA8888(rt_bufs[i].dest_filename, rt_bufs[i].width, rt_bufs[i].height, (uint8_t*)rt_bufs[i].data);
                    
                    if (strcmp(rt_bufs[i].filename, rt_bufs[i].dest_filename) != 0)
                    {
                        remove(rt_bufs[i].filename); // delete src if different formats
                    }//if
                }//if
                
                valid_n++;
            }//if
        }//if
    }//for
    
    if (valid_n > 0)
    {
        gbImage_PNG_Write_RGBA8888(filepath, local_width, local_height, (uint8_t*)local_rgba);
    }//if
    
    free(local_rgba);
    local_rgba = NULL;
}//_DownsampleCompressAndWriteTile_RetileBuffers_RGBA8888


// ==================================================================
// _DownsampleCompressAndWrite_RetileBuffers_DispatchWrapper_RGBA8888
// ==================================================================
//
// Wrapper for _ResampleCompressAndWrite_RetileBuffers_RGBA8888 that uses Apple's
// GCD thread pooling.
//
// 1. Copies all src data from the reader.
// 2. Waits for parallelism limit semaphore. (sema_write) (blocking)
// 3. Locks and decrements queue_n when done (sema_idx)
//
static inline void _DownsampleCompressAndWrite_RetileBuffers_DispatchWrapper_RGBA8888(const char*          filepath,
                                                                                      Retile_Buffer*       rt_bufs,
                                                                                      const size_t         rt_buf_n,
                                                                                      dispatch_semaphore_t sema_write,
                                                                                      dispatch_semaphore_t sema_idx,
                                                                                      int*                 queue_n,
                                                                                      const bool           alsoReprocessSrc,
                                                                                      const int            interpolationTypeId)
{
#ifdef __ACCELERATE__
    // copy filenames and refs synchronously, as they are stack alloc / reused
    
    const size_t   local_rt_buf_n = rt_buf_n;
    char*          local_filepath = malloc(sizeof(char) * 1024);
    Retile_Buffer* local_rt_bufs  = malloc(sizeof(Retile_Buffer) * local_rt_buf_n);
    
    memcpy(local_filepath, filepath, 1024);
    
    _CopyRetileBuffers(local_rt_bufs, rt_bufs, local_rt_buf_n);
    
    dispatch_semaphore_wait(sema_write, DISPATCH_TIME_FOREVER);
    
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        
        _DownsampleCompressAndWrite_RetileBuffers_RGBA8888(local_filepath, local_rt_bufs, local_rt_buf_n, alsoReprocessSrc, interpolationTypeId);
        
        _FreeRetileBuffersData(local_rt_bufs, local_rt_buf_n);
        free(local_filepath);
        free(local_rt_bufs);
        
        dispatch_semaphore_signal(sema_write);
        
        _IncDecrementWithSemaphore(queue_n, sema_idx, false, true);
    });
#endif
}//_DownsampleCompressAndWriteTile_RetileBuffers_DispatchWrapper_RGBA8888



static inline void _GetFilepathAndCreateIntermediatePathsIfNeeded(char*          dest_filepath,
                                                                  const char*    basePath,
                                                                  const uint32_t x,
                                                                  const uint32_t y,
                                                                  const uint32_t z,
                                                                  uint32_t*      last_path_created_x,
                                                                  uint32_t*      last_path_created_z,
                                                                  const int      urlTemplateId)
{
    char dest_filename[1024] __attribute__ ((aligned(16)));
    
    if (urlTemplateId == 0)
    {
        _GetFilenameForXYZ_FromTemplate_OSM(x, y, z, NULL, dest_filename);
        _StringByAppendingPathComponent(dest_filepath, basePath, dest_filename);
        
        // not particularly elegant way of creating the intermediate paths.
        
        if (z != *last_path_created_z)
        {
            char sub_dest_path[1024] __attribute__ ((aligned(16)));
            char temp_char_comp[1024] __attribute__ ((aligned(16)));
            
            sprintf(temp_char_comp, "%d", (int)z);
            _StringByAppendingPathComponent(sub_dest_path, basePath, temp_char_comp);
            mkdir(sub_dest_path, 0777);
            *last_path_created_z = z;
        }//if
        
        if (x != *last_path_created_x)
        {
            char sub_dest_path[1024] __attribute__ ((aligned(16)));
            char temp_char_comp[1024] __attribute__ ((aligned(16)));
            
            sprintf(temp_char_comp, "%d/%d", (int)z, (int)x);
            _StringByAppendingPathComponent(sub_dest_path, basePath, temp_char_comp);
            
            mkdir(sub_dest_path, 0777);
            *last_path_created_x = x;
        }//if
    }//if
    else
    {
        if (urlTemplateId == 1)
        {
            _GetFilenameForZXY_FromTemplate(x, y, z, NULL, dest_filename);
        }//if
        else
        {
            _GetFilenameForXYZ_FromTemplate(x, y, z, NULL, dest_filename);
        }//else
        
        _StringByAppendingPathComponent(dest_filepath, basePath, dest_filename);
        
        if (z != *last_path_created_z)
        {
            char sub_dest_path[1024] __attribute__ ((aligned(16)));
            
            _GetPathFromFilepath(sub_dest_path, dest_filepath);
            mkdir(sub_dest_path, 0777);
            
            *last_path_created_z = z;
        }//if
    }//else
}//_GetFilepathAndCreateIntermediatePathsIfNeeded






// =======================
// _QueueDownsampleFromDB:
// =======================
//
// Main raster tile pyramid level downsampling (z -> z-1) function.
//
// For a database at dbFilePath populated by _ReadPathToDB, this uses limited
// data clustering based on Microsoft's QuadKey to make sure each "quad" of
// tiles to be downsampled (which can be 1-4 tiles) are read consecutively.
// This clustering allows for a performant, scalable approach to the problem.
//
// (more info: http://msdn.microsoft.com/en-us/library/bb259689.aspx )
//
// Detecting changes in the destination tile xyz allows the accumulated quad
// to be processed and output to a new shiny z-1 PNG.
//
// If Apple's GCD is present (inferred via the Accelerate framework), this
// will be multithreaded and use asynchronous processing.
//
// (even in single threaded mode, this function does no processing, it merely
//  queues the work up and accumulates references.)
//
void _QueueDownsampleFromDB(const char* destPath,
                            const char* dbFilePath,
                            const int   urlTemplateId,
                            const bool  alsoReprocessSrc,
                            const int   interpolationTypeId)
{
    int       row      = 0;
    int       queue_n  = 0;
    const int rowCount = gbDB_ExecSQL_Scalar(dbFilePath, "SELECT COUNT(*) FROM TileRef;");
    const int modCount = ceil((double)rowCount / 10.0);
    
#ifdef __ACCELERATE__
    queue_n = gbDB_ExecSQL_Scalar(dbFilePath, "SELECT COUNT (DISTINCT (Y/2 * (1 << (Z-1)) + X/2)) FROM TileRef;");    // no point tracking this if using single thread
    
    dispatch_semaphore_t _sema_idx   = dispatch_semaphore_create(1);
    dispatch_semaphore_t _sema_write = dispatch_semaphore_create(32);
#endif
    
    if (rowCount == 0)
    {
        printf("No tiles were found to read.  Aborting.\n");
        return;
    }//if
    
    sqlite3*      db;
    sqlite3_stmt* selectStmt;
    
    gbDB_PrepConn_DBPath_CString(dbFilePath, "SELECT X, Y, Z, FilePath FROM TileRef ORDER BY Y/2 * (1 << (Z-1)) + X/2, Y * (1 << Z) + X;", &db, &selectStmt);
    
    // <multiread>
    size_t        rt_buf_i = 0;
    const size_t  rt_buf_n = 4;
    
    Retile_Buffer rt_bufs[rt_buf_n] __attribute__ ((aligned(16)));
    
    for (size_t i = 0; i < rt_buf_n; i++)
    {
        rt_bufs[i].data          = NULL;
        rt_bufs[i].width         = 0;
        rt_bufs[i].height        = 0;
        rt_bufs[i].rowBytes      = 0;
        rt_bufs[i].x             = 0;
        rt_bufs[i].y             = 0;
        rt_bufs[i].z             = 0;
        rt_bufs[i].filename      = NULL;
        rt_bufs[i].dest_filename = NULL;
    }//for
    // </multiread>
    
    uint32_t _reproc_last_path_created_z = UINT32_MAX;
    uint32_t _reproc_last_path_created_x = UINT32_MAX;
    
    uint32_t _last_path_created_z = UINT32_MAX;
    uint32_t _last_path_created_x = UINT32_MAX;
    
    uint32_t src_x  = 0;
    uint32_t src_y  = 0;
    uint32_t src_z  = 0;
    
    uint32_t dest_x = 0;
    uint32_t dest_y = 0;
    uint32_t dest_z = 0;
    uint32_t _last_dest_x = UINT32_MAX;
    uint32_t _last_dest_y = UINT32_MAX;
    
    //char dest_filename[1024] __attribute__ ((aligned(16)));
    char dest_filepath[1024] __attribute__ ((aligned(16)));
    //char sub_dest_path[1024] __attribute__ ((aligned(16)));
    //char temp_char_comp[1024] __attribute__ ((aligned(16)));
    
    int sqliteStepResult = SQLITE_ROW;
    
    mkdir(destPath, 0777);
    
    printf("Resampling and writing files (src n=[%d])...\n", rowCount);
    
    while (sqliteStepResult == SQLITE_ROW || sqliteStepResult == SQLITE_DONE)
    {
        char* filename = NULL;
        
        if (sqliteStepResult != SQLITE_DONE)
        {
            sqliteStepResult = sqlite3_step(selectStmt);
        }//if
        
        if (sqliteStepResult != SQLITE_DONE)
        {
            src_x    = sqlite3_column_int(selectStmt, 0);
            src_y    = sqlite3_column_int(selectStmt, 1);
            src_z    = sqlite3_column_int(selectStmt, 2);
            filename = (char*)sqlite3_column_text(selectStmt, 3);
            
            dest_z   = src_z - 1;
            dest_x   = src_x >> (src_z - dest_z);
            dest_y   = src_y >> (src_z - dest_z);
        }//if

        
        if ((  (    ((dest_x != _last_dest_x && _last_dest_x != UINT32_MAX)
                ||   (dest_y != _last_dest_y && _last_dest_y != UINT32_MAX)) && src_z > 0)
             || sqliteStepResult == SQLITE_DONE))
        {
            _GetFilepathAndCreateIntermediatePathsIfNeeded(dest_filepath, destPath,
                                                           _last_dest_x, _last_dest_y, dest_z,
                                                           &_last_path_created_x, &_last_path_created_z,
                                                           urlTemplateId);

#ifdef __ACCELERATE__
            _DownsampleCompressAndWrite_RetileBuffers_DispatchWrapper_RGBA8888(dest_filepath, rt_bufs, rt_buf_n,
                                                                               _sema_write, _sema_idx, &queue_n,
                                                                               alsoReprocessSrc, interpolationTypeId);
#else
            _DownsampleCompressAndWrite_RetileBuffers_RGBA8888(dest_filepath, rt_bufs, rt_buf_n,
                                                               alsoReprocessSrc, interpolationTypeId);
#endif
            
            _FreeRetileBuffersData(rt_bufs, rt_buf_n);
            rt_buf_i = 0;
        }//if
        
        if (sqliteStepResult != SQLITE_DONE)
        {
            if (filename != NULL)
            {
                if (rt_bufs[rt_buf_i].filename != NULL)
                {
                    printf("ERR: rt_bufs[%zu] filename was non-null! [%s]\n", rt_buf_i, rt_bufs[rt_buf_i].filename);
                }//if
                
                rt_bufs[rt_buf_i].filename = malloc(sizeof(char) * 1024);
                memcpy(rt_bufs[rt_buf_i].filename, filename, sizeof(char) * 1024);
            }//if
            
            rt_bufs[rt_buf_i].x = src_x;
            rt_bufs[rt_buf_i].y = src_y;
            rt_bufs[rt_buf_i].z = src_z;
            
            if (rt_bufs[rt_buf_i].filename != NULL)
            {
                if (alsoReprocessSrc)
                {
                    rt_bufs[rt_buf_i].dest_filename = malloc(sizeof(char) * 1024);
                    
                    _GetFilepathAndCreateIntermediatePathsIfNeeded(rt_bufs[rt_buf_i].dest_filename, destPath,
                                                                   rt_bufs[rt_buf_i].x, rt_bufs[rt_buf_i].y, rt_bufs[rt_buf_i].z,
                                                                   &_reproc_last_path_created_x, &_reproc_last_path_created_z,
                                                                   urlTemplateId);
                }//if
                
                rt_buf_i     = rt_buf_i < rt_buf_n - 1 ? rt_buf_i + 1 : 0;
                _last_dest_x = dest_x;
                _last_dest_y = dest_y;
            }//if
        }//if
        else
        {
            sqliteStepResult = -1;
            break;
        }//else
        
        if (row % modCount == 0)
        {
            printf("[z=%d]: %1.0f%%\n", (int)dest_z, (double)row / (double)rowCount * 100.0);
        }//if
        
        row++;
    }//while
    

#ifdef __ACCELERATE__
    // Wait for worker queue to empty, but use a long failsafe timeout in case
    // the initial COUNT DISTINCT did not predict things correctly.
    // (eg, bad input PNGs, etc)
    //
    
    int polled_n = _IncDecrementWithSemaphore(&queue_n, _sema_idx, false, false);

    if (polled_n > 0)
    {
        printf("[z=%d]: Waiting on compress and write queue: %d\n", (int)dest_z, polled_n);
        
        uint32_t        currentSleepMS  = 0;
        const uint32_t kSleepIntervalMS = 100;                      // 100ms
        const uint32_t kSleepIntervalUS = kSleepIntervalMS * 1000;  // 100ms -> us
        const uint32_t kMaxSleepMS      = 60 * 1000;                // 60s   -> ms
        
        while (polled_n > 0)
        {
            if (polled_n <= 0 || currentSleepMS > kMaxSleepMS)
            {
                break;
            }//if
            
            polled_n = _IncDecrementWithSemaphore(&queue_n, _sema_idx, false, false);
            
            usleep(kSleepIntervalUS);
            currentSleepMS += kSleepIntervalMS;
        }//while
        
        if (currentSleepMS > kMaxSleepMS)
        {
            printf("[z=%d]: [WARN] Timeout after %d ms while waiting on queue: %d\n", (int)dest_z, (int)kMaxSleepMS, polled_n);
        }//if
    }//if
    
    dispatch_release(_sema_idx);
    dispatch_release(_sema_write);
#endif
    
    printf("[z=%d]: 100%%\n", (int)dest_z);
    
    printf("[z=%d]: Closing DB and transaction...\n", (int)dest_z);
    
    gbDB_CloseDBConnAndQueryStmt(db, selectStmt);
    
    printf("[z=%d]: Done.\n", (int)dest_z);
}//_QueueDownsampleFromDB





















// ===============================================
// _EnlargeCompressAndWrite_RetileBuffers_RGBA8888
// ===============================================
//
// For references to a group of tiles in rt_bufs,
// read, decompess, resample, composite, compress and write a PNG file.
//
static inline void _EnlargeCompressAndWrite_RetileBuffers_RGBA8888(const char*    rootPath,
                                                                   Retile_Buffer* rt_bufs,
                                                                   const size_t   rt_buf_n,
                                                                   const bool     alsoReprocessSrc,
                                                                   const uint32_t dest_z,
                                                                   const int      urlTemplateId,
                                                                   const int      interpolationTypeId)
{
    size_t   local_width    = 256;
    size_t   local_height   = 256;
    size_t   local_rowBytes = 1024;
    uint32_t y;
    uint32_t x;
    size_t   valid_n        = 0;
    uint32_t _last_path_created_x = UINT32_MAX;
    uint32_t _last_path_created_z = UINT32_MAX;
    
    uint32_t* local_rgba = malloc(sizeof(uint8_t) * local_height * local_rowBytes);
    
    memset(local_rgba, 0, sizeof(uint32_t) * local_height * local_width);
    
    char dest_filepath[1024] __attribute__ ((aligned(16)));
    
    bool roiWasEmpty;
    
    uint32_t start_y;
    uint32_t end_y;
    uint32_t start_x;
    uint32_t end_x;
    uint32_t zs;
    
    for (size_t i = 0; i < rt_buf_n; i++)
    {
        if (rt_bufs[i].filename != NULL)
        {
            gbImage_PNG_Read_RGBA8888(  rt_bufs[i].filename,
                                      &(rt_bufs[i].data),
                                      &(rt_bufs[i].width),
                                      &(rt_bufs[i].height),
                                      &(rt_bufs[i].rowBytes));
            
            if (rt_bufs[i].data != NULL)
            {
                _FixDestTileBufferIfNeeded(&local_rgba,
                                           &local_width,     &local_height,     &local_rowBytes,
                                           rt_bufs[i].width, rt_bufs[i].height, rt_bufs[i].rowBytes);
                
                zs      = dest_z - rt_bufs[i].z;
                
                start_y = rt_bufs[i].y << zs;
                end_y   = start_y            + (1 << zs);
                start_x = rt_bufs[i].x << zs;
                end_x   = start_x            + (1 << zs);
                
                for (y = start_y; y < end_y; y++)
                {
                    for (x = start_x; x < end_x; x++)
                    {
                        gbImage_Resize_EnlargeTile_RGBA8888((uint8_t*)(rt_bufs[i].data),
                                                            (uint8_t*)local_rgba,
                                                            rt_bufs[i].z,
                                                            x, y, dest_z,
                                                            rt_bufs[i].width, rt_bufs[i].height,
                                                            interpolationTypeId,
                                                            &roiWasEmpty);
                        
                        if (!roiWasEmpty) // quite possible to zoom into nothingness on a tile, don't write those.
                        {
                            _GetFilepathAndCreateIntermediatePathsIfNeeded(dest_filepath, rootPath,
                                                                           x, y, dest_z,
                                                                           &_last_path_created_x, &_last_path_created_z,
                                                                           urlTemplateId);
                            
                            gbImage_PNG_Write_RGBA8888(dest_filepath, local_width, local_height, (uint8_t*)local_rgba);
                        }//if
                    }//for
                }//for
                
                
                if (alsoReprocessSrc)
                {
                    gbImage_PNG_Write_RGBA8888(rt_bufs[i].dest_filename, rt_bufs[i].width, rt_bufs[i].height, (uint8_t*)rt_bufs[i].data);
                    
                    if (strcmp(rt_bufs[i].filename, rt_bufs[i].dest_filename) != 0)
                    {
                        remove(rt_bufs[i].filename); // delete src if different formats
                    }//if
                }//if
                
                valid_n++;
            }//if
        }//if
    }//for
    
    free(local_rgba);
    local_rgba = NULL;
}//_EnlargeCompressAndWriteTile_RetileBuffers_RGBA8888


// ===============================================================
// _EnlargeCompressAndWrite_RetileBuffers_DispatchWrapper_RGBA8888
// ===============================================================
//
// Wrapper for _EnlargeCompressAndWrite_RetileBuffers_RGBA8888 that uses Apple's
// GCD thread pooling.
//
// 1. Copies all src data from the reader.
// 2. Waits for parallelism limit semaphore. (sema_write) (blocking)
// 3. Locks and decrements queue_n when done (sema_idx)
//
static inline void _EnlargeCompressAndWrite_RetileBuffers_DispatchWrapper_RGBA8888(const char*          rootPath,
                                                                                   Retile_Buffer*       rt_bufs,
                                                                                   const size_t         rt_buf_n,
                                                                                   dispatch_semaphore_t sema_write,
                                                                                   dispatch_semaphore_t sema_idx,
                                                                                   int*                 queue_n,
                                                                                   const bool           alsoReprocessSrc,
                                                                                   const uint32_t       dest_z,
                                                                                   const int            urlTemplateId,
                                                                                   const int            interpolationTypeId)
{
#ifdef __ACCELERATE__
    // copy filenames and refs synchronously, as they are stack alloc / reused
    
    const size_t   local_rt_buf_n = rt_buf_n;
    char*          local_rootpath = malloc(sizeof(char) * 1024);
    Retile_Buffer* local_rt_bufs  = malloc(sizeof(Retile_Buffer) * local_rt_buf_n);
    
    memcpy(local_rootpath, rootPath, 1024);
    
    _CopyRetileBuffers(local_rt_bufs, rt_bufs, local_rt_buf_n);
    
    dispatch_semaphore_wait(sema_write, DISPATCH_TIME_FOREVER);
    
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        
        _EnlargeCompressAndWrite_RetileBuffers_RGBA8888(local_rootpath, local_rt_bufs, local_rt_buf_n, alsoReprocessSrc, dest_z, urlTemplateId, interpolationTypeId);
        
        _FreeRetileBuffersData(local_rt_bufs, local_rt_buf_n);
        free(local_rootpath);
        free(local_rt_bufs);
        
        dispatch_semaphore_signal(sema_write);
        
        _IncDecrementWithSemaphore(queue_n, sema_idx, false, true);
    });
#endif
}//_EnlargeCompressAndWriteTile_RetileBuffers_DispatchWrapper_RGBA8888


// ====================
// _QueueEnlargeFromDB:
// ====================
//
// Main raster tile pyramid level enlarge (z -> z+x) function.
//
// This is actually much less tricksy than the downsampling function, no fancy
// data clustering.
//
// If Apple's GCD is present (inferred via the Accelerate framework), this
// will be multithreaded and use asynchronous processing.
//
// (even in single threaded mode, this function does no processing, it merely
//  queues the work up and accumulates references.)
//
void _QueueEnlargeFromDB(const char*    destPath,
                         const char*    dbFilePath,
                         const int      urlTemplateId,
                         const bool     alsoReprocessSrc,
                         const int      interpolationTypeId,
                         const uint32_t dest_z_shift)
{
    int       row      = 0;
    int       queue_n  = 0;
    const int rowCount = gbDB_ExecSQL_Scalar(dbFilePath, "SELECT COUNT(*) FROM TileRef;");
    const int modCount = ceil((double)rowCount / 10.0);
    
#ifdef __ACCELERATE__
    queue_n = rowCount;
    
    dispatch_semaphore_t _sema_idx   = dispatch_semaphore_create(1);
    dispatch_semaphore_t _sema_write = dispatch_semaphore_create(32);
#endif
    
    if (rowCount == 0)
    {
        printf("No tiles were found to read.  Aborting.\n");
        return;
    }//if
    
    sqlite3*      db;
    sqlite3_stmt* selectStmt;
    
    gbDB_PrepConn_DBPath_CString(dbFilePath, "SELECT X, Y, Z, FilePath FROM TileRef ORDER BY Y/2 * (1 << (Z-1)) + X/2, Y * (1 << Z) + X;", &db, &selectStmt);
    
    // <multiread>
    size_t        rt_buf_i = 0;
    const size_t  rt_buf_n = 1;
    
    Retile_Buffer rt_bufs[rt_buf_n] __attribute__ ((aligned(16)));
    
    for (size_t i = 0; i < rt_buf_n; i++)
    {
        rt_bufs[i].data          = NULL;
        rt_bufs[i].width         = 0;
        rt_bufs[i].height        = 0;
        rt_bufs[i].rowBytes      = 0;
        rt_bufs[i].x             = 0;
        rt_bufs[i].y             = 0;
        rt_bufs[i].z             = 0;
        rt_bufs[i].filename      = NULL;
        rt_bufs[i].dest_filename = NULL;
    }//for
    // </multiread>
    
    uint32_t _reproc_last_path_created_z = UINT32_MAX;
    uint32_t _reproc_last_path_created_x = UINT32_MAX;
    
    uint32_t src_x  = 0;
    uint32_t src_y  = 0;
    uint32_t src_z  = 0;
    
    uint32_t dest_x = 0;
    uint32_t dest_y = 0;
    uint32_t dest_z = 0;
    
    int sqliteStepResult = SQLITE_ROW;
    
    mkdir(destPath, 0777);
    
    printf("Resampling and writing files (src n=[%d])...\n", rowCount);
    
    while (sqliteStepResult == SQLITE_ROW || sqliteStepResult == SQLITE_DONE)
    {
        char* filename = NULL;
        
        if (sqliteStepResult != SQLITE_DONE)
        {
            sqliteStepResult = sqlite3_step(selectStmt);
        }//if
        
        if (sqliteStepResult != SQLITE_DONE)
        {
            src_x    = sqlite3_column_int(selectStmt, 0);
            src_y    = sqlite3_column_int(selectStmt, 1);
            src_z    = sqlite3_column_int(selectStmt, 2);
            filename = (char*)sqlite3_column_text(selectStmt, 3);
            
            dest_z   = src_z + dest_z_shift;
            dest_x   = src_x >> (src_z - dest_z);
            dest_y   = src_y >> (src_z - dest_z);
            
            if (filename != NULL)
            {
                if (rt_bufs[rt_buf_i].filename != NULL)
                {
                    printf("ERR: rt_bufs[%zu] filename was non-null! [%s]\n", rt_buf_i, rt_bufs[rt_buf_i].filename);
                }//if
                
                rt_bufs[rt_buf_i].filename = malloc(sizeof(char) * 1024);
                memcpy(rt_bufs[rt_buf_i].filename, filename, sizeof(char) * 1024);
            }//if
            
            rt_bufs[rt_buf_i].x = src_x;
            rt_bufs[rt_buf_i].y = src_y;
            rt_bufs[rt_buf_i].z = src_z;
            
            if (rt_bufs[rt_buf_i].filename != NULL)
            {
                if (alsoReprocessSrc)
                {
                    rt_bufs[rt_buf_i].dest_filename = malloc(sizeof(char) * 1024);
                    
                    _GetFilepathAndCreateIntermediatePathsIfNeeded(rt_bufs[rt_buf_i].dest_filename, destPath,
                                                                   rt_bufs[rt_buf_i].x, rt_bufs[rt_buf_i].y, rt_bufs[rt_buf_i].z,
                                                                   &_reproc_last_path_created_x, &_reproc_last_path_created_z,
                                                                   urlTemplateId);
                }//if
                
                rt_buf_i     = rt_buf_i < rt_buf_n - 1 ? rt_buf_i + 1 : 0;
            }//if
        }//if
        
        
        if (src_z > 0 && sqliteStepResult != SQLITE_DONE)
        {
#ifdef __ACCELERATE__
            _EnlargeCompressAndWrite_RetileBuffers_DispatchWrapper_RGBA8888(destPath, rt_bufs, rt_buf_n,
                                                                            _sema_write, _sema_idx, &queue_n,
                                                                            alsoReprocessSrc, dest_z, urlTemplateId, interpolationTypeId);
#else
            _EnlargeCompressAndWrite_RetileBuffers_RGBA8888(destPath, rt_bufs, rt_buf_n,
                                                            alsoReprocessSrc, dest_z, urlTemplateId, interpolationTypeId);
#endif
            
            _FreeRetileBuffersData(rt_bufs, rt_buf_n);
            rt_buf_i = 0;
        }//if

        if (sqliteStepResult == SQLITE_DONE)
        {
            sqliteStepResult = -1;
            break;
        }//if
        
        if (row % modCount == 0)
        {
            printf("[z=%d]: %1.0f%%\n", (int)dest_z, (double)row / (double)rowCount * 100.0);
        }//if
        
        row++;
    }//while
    
    
#ifdef __ACCELERATE__
    // Wait for worker queue to empty, but use a long failsafe timeout in case
    // the initial COUNT DISTINCT did not predict things correctly.
    // (eg, bad input PNGs, etc)
    //
    
    int polled_n = _IncDecrementWithSemaphore(&queue_n, _sema_idx, false, false);
    
    if (polled_n > 0)
    {
        printf("[z=%d]: Waiting on compress and write queue: %d\n", (int)dest_z, polled_n);
        
        uint32_t        currentSleepMS  = 0;
        const uint32_t kSleepIntervalMS = 100;                      // 100ms
        const uint32_t kSleepIntervalUS = kSleepIntervalMS * 1000;  // 100ms -> us
        const uint32_t kMaxSleepMS      = 60 * 1000;                // 60s   -> ms
        
        while (polled_n > 0)
        {
            if (polled_n <= 0 || currentSleepMS > kMaxSleepMS)
            {
                break;
            }//if
            
            polled_n = _IncDecrementWithSemaphore(&queue_n, _sema_idx, false, false);
            
            usleep(kSleepIntervalUS);
            currentSleepMS += kSleepIntervalMS;
        }//while
        
        if (currentSleepMS > kMaxSleepMS)
        {
            printf("[z=%d]: [WARN] Timeout after %d ms while waiting on queue: %d\n", (int)dest_z, (int)kMaxSleepMS, polled_n);
        }//if
    }//if
    
    dispatch_release(_sema_idx);
    dispatch_release(_sema_write);
#endif
    
    printf("[z=%d]: 100%%\n", (int)dest_z);
    
    printf("[z=%d]: Closing DB and transaction...\n", (int)dest_z);
    
    gbDB_CloseDBConnAndQueryStmt(db, selectStmt);
    
    printf("[z=%d]: Done.\n", (int)dest_z);
}//_QueueEnlargeFromDB




























static inline void _ReprocessTile(const char* filename)
{
    uint32_t* rgba = NULL;
    size_t w;
    size_t h;
    size_t rb;
    
    gbImage_PNG_Read_RGBA8888(filename, &rgba, &w, &h, &rb);
    
    if (rgba != NULL)
    {
        gbImage_PNG_Write_RGBA8888(filename, w, h, (uint8_t*)rgba);
    }//if
    else
    {
        printf("_ReprocessTile: [ERR] rgba was NULL.\n");
    }//else
    
    free(rgba);
    rgba = NULL;
}//_ReprocessTile

static inline void _ReprocessTile_DispatchWrapper(const char*          filename,
                                                  dispatch_semaphore_t sema_write,
                                                  dispatch_semaphore_t sema_idx,
                                                  int*                 queue_n)
{
#ifdef __ACCELERATE__
    char* local_filename = malloc(sizeof(char) * 1024);
    memcpy(local_filename, filename, 1024);
    
    dispatch_semaphore_wait(sema_write, DISPATCH_TIME_FOREVER);
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^{
        _ReprocessTile(local_filename);
        
        free(local_filename);

        dispatch_semaphore_signal(sema_write);
        
        _IncDecrementWithSemaphore(queue_n, sema_idx, false, true);
    });
#endif
}//_ReprocessTile_DispatchWrapper




// =====================
// _GetFileCountForPath:
// =====================
//
// Counts the number of files in a directory by iterating through them all.
//
// Probably not the most efficient method, and may have problems scaling.
//
static inline size_t _GetFileCountForPath(const char* srcPath,
                                          const bool  isRecursive)
{
    size_t file_n = 0;
    
    tinydir_dir dir;
    tinydir_open(&dir, srcPath);
    
    while (dir.has_next)
    {
        tinydir_file file;
        tinydir_readfile(&dir, &file);
        
        if (!file.is_dir)
        {
            file_n++;
        }//if
        
         else if (isRecursive
                  && (strnlen(file.name, 1024) != 1 || strcmp(file.name, ".")  != 0)
                  && (strnlen(file.name, 1024) != 2 || strcmp(file.name, "..") != 0))
         {
             char* subPath = malloc(sizeof(char) * strnlen(srcPath, 1024) + 1UL); // heap to reduce stack pressure from multiple recursions
         
             _StringByAppendingPathComponent(subPath, srcPath, file.name);
         
             file_n += _GetFileCountForPath(subPath, isRecursive);
             
             free(subPath);
             subPath = NULL;
         }//if
        
        tinydir_next(&dir);
    }//while
    
    tinydir_close(&dir);
    
    return file_n;
}//_GetFileCountForPath



// ===============
// _ReprocessTiles
// ===============
//
// Read and recompress all the PNG tiles at srcPath, overwriting existing files.
//
// Similar to pngcrush, optipng, etc.
//
// For effeciency, you should not use this.  Instead, reprocessing (if it is
// to be done at all) should be done in _ProcessTilesFromDB by passing true for
// "alsoReprocessSrc", as it will have already done the disk read and
// decompression anyway.
//
// Should produce results similar to OptiPNG and superior to pngcrush for
// limited color palettes while being much faster.
//
// Allows for recursive scans. (not yet implemented)
//
static inline void _ReprocessTiles(const char* srcPath,
                                   const bool  isRecursive)
{
    size_t max_n = _GetFileCountForPath(srcPath, isRecursive);
    size_t n     = 0;
    int  queue_n = (int)max_n;
    size_t mod_c = ceil((double)max_n / 10.0);
    
    char dest_filepath[1024] __attribute__ ((aligned(16)));
    
    printf("Retile: Reprocessing tiles at path %s, please wait... (n = %zu)\n",
           srcPath, max_n);
    
#ifdef __ACCELERATE__
    dispatch_semaphore_t _sema_idx   = dispatch_semaphore_create(1);
    dispatch_semaphore_t _sema_write = dispatch_semaphore_create(32);
#endif
    
    tinydir_dir dir;
    tinydir_open(&dir, srcPath);
    
    while (dir.has_next)
    {
        tinydir_file file;
        tinydir_readfile(&dir, &file);
        
        if (!file.is_dir)
        {
            _StringByAppendingPathComponent(dest_filepath, srcPath, file.name);
            
#ifdef __ACCELERATE__
            _ReprocessTile_DispatchWrapper(dest_filepath, _sema_write, _sema_idx, &queue_n);
#else
            _ReprocessTile(dest_filepath);
#endif
        }//if
        /*
        else if (isRecursive
                 && (strnlen(file.name, 1024) != 1 || strcmp(file.name, ".")  != 0)
                 && (strnlen(file.name, 1024) != 2 || strcmp(file.name, "..") != 0))
        {
            char* subPath = malloc(sizeof(char) * strnlen(srcPath, 1024) + 1UL); // heap to reduce stack pressure from multiple recursions
            
            _StringByAppendingPathComponent(subPath, srcPath, file.name);
            
            _ParsePathToDB(subPath, db, insertStmt, n, isPathOSM, isRecursive);
            
            free(subPath);
            subPath = NULL;
        }//if
        */
        
        if (n % mod_c == 0)
        {
            printf("[Reprocess]: %1.0f%%\n", (double)n / (double)max_n * 100.0);
        }//if
        
        n++;
        
        tinydir_next(&dir);
    }//while
    
    tinydir_close(&dir);
    
#ifdef __ACCELERATE__
    // Wait for worker queue to empty, but use a long failsafe timeout in case
    // of whatever.
    
    int polled_n = _IncDecrementWithSemaphore(&queue_n, _sema_idx, false, false);
    
    if (polled_n > 0)
    {
        printf("Waiting on compress and write queue: %d\n", polled_n);
        
        uint32_t        currentSleepMS  = 0;
        const uint32_t kSleepIntervalMS = 100;                      // 100ms
        const uint32_t kSleepIntervalUS = kSleepIntervalMS * 1000;  // 100ms -> us
        const uint32_t kMaxSleepMS      = 60 * 1000;                // 60s   -> ms
        
        while (polled_n > 0)
        {
            if (polled_n <= 0 || currentSleepMS > kMaxSleepMS)
            {
                break;
            }//if
            
            polled_n = _IncDecrementWithSemaphore(&queue_n, _sema_idx, false, false);
            
            usleep(kSleepIntervalUS);
            currentSleepMS += kSleepIntervalMS;
        }//while
        
        if (currentSleepMS > kMaxSleepMS)
        {
            printf("[WARN] Timeout after %d ms while waiting on queue: %d\n", (int)kMaxSleepMS, polled_n);
        }//if
    }//if
    
    dispatch_release(_sema_idx);
    dispatch_release(_sema_write);
#endif
    
    printf("[Reprocess]: 100%%\n");
    
    printf("Retile: Reprocessing complete.\n");
}//_ParsePathToDB








// ==================
// CURRENT_TIMESTAMP:
// ==================
//
// Returns time in milliseconds since Unix epoch.
//
// Requires Posix.
//
int64_t CURRENT_TIMESTAMP()
{
    struct timeval te;
    int64_t        ms;

    gettimeofday(&te, NULL);
    
    ms = te.tv_sec * 1000LL + te.tv_usec / 1000LL;
    
    return ms;
}//CURRENT_TIMESTAMP



// =================
// _IterativeRetile:
// =================
//
// Invokes the functions to generate z-1 of a raster tile pyramid repeatedly,
// for as many levels of the raster pyramid as needed.
//
// Expects to work in-place on a path structure.
//
// (downsamples only)
//
static inline void _IterativeRetile(const char* dbFilePath,
                                    const char* rootPath,
                                    const int   dest_min_z,
                                    const int   dest_max_z,
                                    const int   srcUrlTemplateId,
                                    const int   destUrlTemplateId,
                                    const bool  alsoReprocessSrc,
                                    const int   interpolationTypeId)
{
    char* _src_path = malloc(sizeof(char) * 1024);
    char* _comp     = malloc(sizeof(char) * 1024);
    
    for (int z = dest_max_z; z >= dest_min_z; z--)
    {
        sprintf(_comp, "%d", z + 1);
        _StringByAppendingPathComponent(_src_path, rootPath, _comp);
        
        _ReadPathToDB(_src_path, dbFilePath,
                      z == dest_max_z ? srcUrlTemplateId
                                      : destUrlTemplateId);
        
        _QueueDownsampleFromDB(rootPath, dbFilePath, destUrlTemplateId, alsoReprocessSrc && z == dest_max_z, interpolationTypeId);
    }//for
    
    free(_src_path);
    free(_comp);
}//_IterativeRetile





// these are lazy / test functions.

static inline void _ProductionNoParamRun(const char* dbFilePath, const bool alsoReprocessSrc, const int interpolationTypeId)
{    
    char* rootPath = "/Library/WebServer/Documents/tilemap/TileGriddata/";
    
    _IterativeRetile(dbFilePath, rootPath, 0, 12, kRetile_Template_XYZ, kRetile_Template_XYZ, alsoReprocessSrc, interpolationTypeId);
    
    char* src  = "/Library/WebServer/Documents/tilemap/TileGriddata/13";
    char* dest = "/Library/WebServer/Documents/tilemap/TileGriddata";
    
    size_t n = _ReadPathToDB(src, dbFilePath, kRetile_Template_XYZ);
    
    if (n > 0)
    {
        _QueueEnlargeFromDB(dest, dbFilePath, kRetile_Template_XYZ, alsoReprocessSrc, kGB_Image_Interp_EPX, 1);
    }//if
    
    
    src = "/Library/WebServer/Documents/tilemap/TileGriddata/14";
    n   = _ReadPathToDB(src, dbFilePath, kRetile_Template_XYZ);
    
    if (n > 0)
    {
        _QueueEnlargeFromDB(dest, dbFilePath, kRetile_Template_XYZ, alsoReprocessSrc, kGB_Image_Interp_EPX, 1);
    }//if
    
    //printf("Prod bypass disabled for test.  Abort.\n");
}//_ProductionNoParamRun


static inline void _LocalTestRun(const char* dbFilePath, const bool alsoReprocessSrc, const int interpolationTypeId)
{
    
    /*
    char* rootPath = "/Users/ndolezal/Downloads/TileGriddata/";
    
    _IterativeRetile(dbFilePath, rootPath, 0, 12, kRetile_Template_OSM, kRetile_Template_OSM, alsoReprocessSrc, interpolationTypeId);
    
    char* src  = "/Users/ndolezal/Downloads/TileGriddata/13";
    char* dest = "/Users/ndolezal/Downloads/TileGriddata";
    
    size_t n = _ReadPathToDB(src, dbFilePath, kRetile_Template_OSM);
    
    if (n > 0)
    {
        _QueueEnlargeFromDB(dest, dbFilePath, kRetile_Template_OSM, alsoReprocessSrc, kGB_Image_Interp_EPX, 1);
    }//if
    
    src = "/Users/ndolezal/Downloads/TileGriddata/14";
    n   = _ReadPathToDB(src, dbFilePath, kRetile_Template_OSM);
    
    if (n > 0)
    {
        _QueueEnlargeFromDB(dest, dbFilePath, kRetile_Template_OSM, alsoReprocessSrc, kGB_Image_Interp_EPX, 1);
    }//if
    */
    
    /*
    uint32_t* src = NULL;
    size_t src_w;
    size_t src_h;
    size_t src_rb;
    
    gbImage_PNG_Read_RGBA8888("/Users/ndolezal/Downloads/lenna.png", &src, &src_w, &src_h, &src_rb);
    
    size_t dest_w  = src_w  * 2;
    size_t dest_h  = src_h  * 2;
    size_t dest_rb = src_rb * 2;
    
    uint32_t* dest = malloc(sizeof(uint32_t) * dest_w * dest_h);
    
    
    //gbImage_Resize_vImage_Lanczos5x5_RGBA8888((uint8_t*)src, (uint8_t*)dest, src_w, src_h, src_rb, dest_w, dest_h, dest_rb);
    gbImage_GetZoomedTile_NN_FromCrop_EPX_RGBA8888((uint8_t*)src, src_w, src_h, src_rb, (uint8_t*)dest, dest_w, dest_h, dest_rb);
    //gbImage_GetZoomedTile_NN_FromCrop_Eagle_RGBA8888((uint8_t*)src, src_w, src_h, src_rb, (uint8_t*)dest, dest_w, dest_h, dest_rb);
    
    gbImage_PNG_Write_RGBA8888("/Users/ndolezal/Downloads/lenna_epx2x.png", dest_w, dest_h, (uint8_t*)dest);
    
    free(src);
    free(dest);
    */
    
    printf("Local bypass disabled for test.  Abort.\n");
}//_LocalTestRun








// z=13 -> z=0:
// ===========
//
// Lanczos 3x3:
// ------------
//
// Lc3: 257.4 mb -- RLE
// Lc3:  68.1 mb -- default 9 + avg
// Lc3:  65.9 mb -- default 9 + paeth
// Lc3:  64.3 mb -- default 9 + up
// Lc3:  63.1 mb -- default 9 + filter all
// Lc3:  62.9 mb -- default 9 + sub
// Lc3:  52.8 mb -- default 9 + no filters
//
// Average:
// --------
//
// Avg: 118.9 mb -- RLE
// Avg:  32.4 mb -- default 9 + filter all
// Avg:  25.4 mb -- default 9 + no filters
//
//
// Conclusion: don't use PNG filters unless you have a simple gradiant.
//
// (although most gradiants would be 1px or wouldn't be linear in RGB
//  colorspace?  do not see use of filters.)



// z=13 -> z=0: no   -reprocess: 21s overall (z=13: 47 MB)
// z=13 -> z=0: with -reprocess: 43s overall (z=13: 22 MB)

// ImageIO: 235.8 MB
// libpng:   74.8 MB

int main(int argc, const char * argv[])
{
    const char* dbFilePath            = "/tmp/retile.sqlite";
    bool        showRunTime           = true;
    int64_t     st                    = CURRENT_TIMESTAMP();

    // lazy overrides
    const bool  LOCAL_NO_PARAM_BYPASS = false;
    const bool  PROD_NO_PARAM_BYPASS  = true;  // overrides local no param bypass
    const bool  REPROC_SRC_BYPASS     = true;  // crushes src in-place for no param modes
    
    
    // stuff the user types in
    char*       srcPath               = NULL;
    char*       destPath              = NULL;
    bool        alsoReprocessSrc      = false;
    bool        showHelp              = false;
    int         srcFormatId           = kRetile_Template_OSM;
    int         destFormatId          = kRetile_Template_OSM;
    int         interpolationTypeId   = -9000;
    int         opMode                = kRetile_OpMode_Downsample;
    
#ifdef __ACCELERATE__
    printf("Retile: Accelerate framework enabled. Lanczos is available.\n");
    // if Accelerate, then GCD is also availabe...
    printf("Retile: Multithreading via libdispatch thread pooling enabled.\n");
#endif
    
    for (int i=0; i<argc; i++)
    {
        if (strncmp(argv[i], "-help", 5) == 0 || strncmp(argv[i], "--help", 6) == 0)
        {
            showHelp = true;
        }//if
        else if (strncmp(argv[i], "-reprocess", 10) == 0)
        {
            alsoReprocessSrc = true;
        }//else if
        else if (strncmp(argv[i], "-inOSM", 4) == 0)
        {
            srcFormatId = kRetile_Template_OSM;
        }//else if
        else if (strncmp(argv[i], "-inZXY", 4) == 0)
        {
            srcFormatId = kRetile_Template_ZXY;
        }//else if
        else if (strncmp(argv[i], "-inXYZ", 4) == 0)
        {
            srcFormatId = kRetile_Template_XYZ;
        }//else if
        else if (strncmp(argv[i], "-outOSM", 5) == 0)
        {
            destFormatId = kRetile_Template_OSM;
        }//else if
        else if (strncmp(argv[i], "-outZXY", 5) == 0)
        {
            destFormatId = kRetile_Template_ZXY;
        }//else if
        else if (strncmp(argv[i], "-outXYZ", 5) == 0)
        {
            destFormatId = kRetile_Template_XYZ;
        }//else if
        else if (strncmp(argv[i], "-zIn", 4) == 0)
        {
            opMode = kRetile_OpMode_Enlarge;
        }//else if
        else if (strncmp(argv[i], "-zOut", 5) == 0)
        {
            opMode = kRetile_OpMode_Downsample;
        }//else if
        else if (strncmp(argv[i], "-interpEX", 9) == 0)
        {
            interpolationTypeId = kGB_Image_Interp_EPX;
        }//else if
        else if (strncmp(argv[i], "-interpL3", 9) == 0)
        {
            interpolationTypeId = kGB_Image_Interp_Lanczos3x3;
        }//else if
        else if (strncmp(argv[i], "-interpL5", 9) == 0)
        {
            interpolationTypeId = kGB_Image_Interp_Lanczos5x5;
        }//else if
        else if (strncmp(argv[i], "-interpNN", 9) == 0)
        {
            interpolationTypeId = kGB_Image_Interp_NN;
        }//else if
        else if (strncmp(argv[i], "-interpBI", 9) == 0)
        {
            interpolationTypeId = kGB_Image_Interp_Bilinear;
        }//else if
        else if (strncmp(argv[i], "-interpAV", 9) == 0)
        {
            interpolationTypeId = kGB_Image_Interp_Average;
        }//else if
    }//for
    
    // set interp default for op mode
    if (interpolationTypeId == -9000)
    {
        interpolationTypeId = opMode == kRetile_OpMode_Enlarge ? kGB_Image_Interp_EPX : kGB_Image_Interp_Lanczos3x3;
    }//if

    
    printf("argc:       %d\n", argc);
    printf("-help:      %d\n", showHelp ? 1 : 0);
    printf("-reprocess: %d\n", alsoReprocessSrc ? 1 : 0);
    printf("-srcFmt:    %s\n", srcFormatId  == 0 ? "OSM" : srcFormatId  == 1 ? "ZXY" : "XYZ");
    printf("-destFmt:   %s\n", destFormatId == 0 ? "OSM" : destFormatId == 1 ? "ZXY" : "XYZ");
    printf("-interp:    %s\n", interpolationTypeId == kGB_Image_Interp_Average    ? "AV"
                             : interpolationTypeId == kGB_Image_Interp_Bilinear   ? "BI"
                             : interpolationTypeId == kGB_Image_Interp_Eagle      ? "EA"
                             : interpolationTypeId == kGB_Image_Interp_EPX        ? "EX"
                             : interpolationTypeId == kGB_Image_Interp_Lanczos3x3 ? "L3"
                             : interpolationTypeId == kGB_Image_Interp_Lanczos5x5 ? "L5"
                             :                                                      "NN");
    printf("-zdir:      %s\n", opMode == kRetile_OpMode_Downsample ? "Out" : "In");
    
    if (showHelp || (argc <= 1 && !PROD_NO_PARAM_BYPASS && !LOCAL_NO_PARAM_BYPASS))
    {
        //      01234567890123456789012345678901234567890123456789012345678901234567890123456789
        printf("+--------+--------------------------------------------------------+----------+\n");
        printf("| Retile |     Retiles a single zoom level of tiles to z+/-1.     | PNG Only |\n");
        printf("+--------+--------------------------------------------------------+----------+\n");
        printf("\n");
        printf("Use: retile <in_path> <out_path> -reprocess <in_fmt> <out_fmt> <interp> <zdir>\n");
        printf("\n");
        printf("out_path will get /{z}/ appended to it automatically.\n");
        printf("\n");
        printf("Example: retile /tiles/13 /tiles\n");
        printf("Example: retile /tiles/13 /tiles -reprocess\n");
        printf("Example: retile /tiles/13 /tiles -reprocess -inXYZ -outOSM\n");
        printf("\n");
        printf("-reprocess: Optional.  Overwrites src files in-place and recompresses them,\n");
        printf("            similar to pngcrush.  Will be moved if inFMT and outFMT differ.\n");
        printf("            ATTENTION! -reprocess destroys the original!\n");
        printf("\n");
        printf("<in_fmt>:   Optional.  A URL template, one of: { -inOSM, -inZXY, -inXYZ }.\n");
        printf("            Default is [-inOSM].\n");
        printf("\n");
        printf("<out_fmt>:  Optional.  A URL template, one of: { -outOSM, -outZXY, -outXYZ }.\n");
        printf("            Default is [-outOSM].\n");
        printf("\n");
        printf("<interp>:   Optional.  Interpolation type, one of:\n");
        printf("            Zoom In:  { -interpEX, -interpL3, -interpL5, -interpNN, -interpBI }\n");
        printf("            Zoom Out: { -interpAV, -interpL3, -interpL5                       }\n");
        printf("            Default is [-interpEX] (in) and [-interpL3] (out).\n");
        printf("\n");
        printf("<zdir>:     Optional. Direction of zoom, one of: { -zIn, -zOut }.\n");
        printf("            [-zOut] creates tiles for zoom level -1, downsampling them.\n");
        printf("            [-zIn]  creates tiles for zoom level +1, enlarging them.\n");
        printf("            Default is [-zOut].\n");
        printf("\n");
        printf("Format info:\n");
        printf("------------\n");
        printf("-inOSM, -outOSM: /{z}/{x}/{y}.png       (OpenStreetMaps convention)\n");
        printf("-inZXY, -outZXY: /{z}/*_{z}_{x}_{y}.png\n");
        printf("-inXYZ, -outXYZ: /{z}/*_{x}_{y}_{z}.png\n");
        printf("\n");
        printf("Interpolation info:\n");
        printf("-------------------\n");
        printf("-interpNN: Nearest Neighbor             (enlarge only)\n");
        printf("-interpBI: Bilinear\n");
        printf("-interpL3: Lanczos 3x3*                 (*only on OS X)\n");
        printf("-interpL5: Lanczos 5x5*                 (*only on OS X)\n");
        printf("-interpEX: EPX                          (enlarge only)\n");
        printf("-interpAV: Average                      (downsample only)\n");
        printf("\n");
        printf("EPX aka Scale2x is an edge-detecting variant of NN for resizing pixel art\n");
        printf("and images of limited color depth.  It is significantly superior for those\n");
        printf("cases, but Lanczos is preferable for photographic imagery.\n");
        printf("\n");
        printf("(Only tested with 256x256 tiles.  Google Maps y-axis convention only.)\n");
        
        showRunTime = false;
    }//if
    else if (argc < 3 && PROD_NO_PARAM_BYPASS)
    {
        _ProductionNoParamRun(dbFilePath, REPROC_SRC_BYPASS, interpolationTypeId);
    }//if
    else if (argc < 3 && LOCAL_NO_PARAM_BYPASS)
    {
        _LocalTestRun(dbFilePath, REPROC_SRC_BYPASS, interpolationTypeId);
    }//else if
    else if (argc >= 2)
    {
        srcPath  = (char*)argv[1];
        destPath = (char*)argv[2];
        
        size_t n = _ReadPathToDB(srcPath, dbFilePath, srcFormatId);
        
        if (n > 0)
        {
            if (opMode == kRetile_OpMode_Downsample)
            {
                _QueueDownsampleFromDB(destPath, dbFilePath, destFormatId, alsoReprocessSrc, interpolationTypeId);
            }//if
            else
            {
                _QueueEnlargeFromDB(destPath, dbFilePath, destFormatId, alsoReprocessSrc, interpolationTypeId, 1);
            }//else
        }//if
        else
        {
            printf("Retile: [ERR]  No files found in src path: [%s].\n", srcPath);
        }//else
    }//if
    
    if (showRunTime)
    {
        printf("Retile: Processing time: %lld mi %lld ss\n",   (CURRENT_TIMESTAMP() - st)/1000/60,
                                                              ((CURRENT_TIMESTAMP() - st)/1000      )
                                                            - ((CURRENT_TIMESTAMP() - st)/1000/60*60) );
    }//if
    
    return 0;
}//main










