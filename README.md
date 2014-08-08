Retile
======

Reprocess raster map tiles.

Primarily, Retile works to create downsampled zoom levels for web map tiles.  Sort of like gdal2tiles, only it takes a directory of PNG files as an input, not a huge monolithic raster.

Contarary to the github language stats, it is 100% C.

####Example

######Command

```
Retile /sample/6 /sample
```

######Diagram

```
         z=6
+---------+---------+
|         |         |                       z=5
| (56,24) | (57,24) |                  +---------+
|         |         |                  |         |
+---------+---------+   -> Retile ->   | (28,12) |
|         |         |                  |         |
| (56,25) | (57,25) |                  +---------+
|         |         |
+---------+---------+

    /6/56/24.png                      /5/28/12.png
    /6/57/24.png
    /6/56/25.png
    /6/57/25.png
```

######Actual Input (z=6) and Output (z=5)

z=6  | 56   |   57
---- | ---- | ----
24   | ![56,24@6](https://github.com/Safecast/Retile/raw/master/sample/6/56/24.png) | ![57,24@6](https://github.com/Safecast/Retile/raw/master/sample/6/57/24.png)
25   | ![56,25@6](https://github.com/Safecast/Retile/raw/master/sample/6/56/25.png) | ![57,25@6](https://github.com/Safecast/Retile/raw/master/sample/6/57/25.png)

z=5  | 28   
---- | ----
12    | ![28,12@5](https://github.com/Safecast/Retile/raw/master/sample/5/28/12.png)


##Why would I run this?

If you had a single zoom level of tiles for a web map, and wanted to create other zoom levels, but doing so from the source was either not possible or practical.

##Wait, that didn't exist already?

Tell me about it.

##Are there any examples of this in use?

Yes.  http://safecast.org/tilemap uses Retile on the webserver to "crush" the Safecast interpolation overlay tiles, as well as create zoom levels 0 - 12.  It is otherwise prohibitive to run a new interpolation for every zoom level, so Retile not only saves a great deal of computational time but also removes pngcrush from the workflow.

##Is it fast?

The code is SIMD vectorized and multithreaded, which is to say, yes (it does about 1k tiles per second on an i5/i7).  The single largest performance factor however is the specific manner in which it processes files and performs data clustering, which is extremely efficient.

Also, it cheats.

##It cheats?

Yes.  Instead of the slow approach of creating every zoom level from the highest resolution available, instead it resamples each zoom level iteratively, such that the downscaling is always 2x.  There is additional filtering to prevent this from creating artifacts, so the loss of precision is not significant.

This is important for my own purposes, as the alpha channel had to be perfect after multiple generations of resampling to work with bitstore.js for client-side indexing.

##You said it replaces pngcrush?

In the above workflow, yes.  The PNG writing is highly optimized by default and quite performant.  For example, for the Safecast interpolated tiles, using Apple's ImageIO framework yields a total filesize of 235.8 MB.  This code yields a total filesize of 74.8 MB and runs faster.

##Can it create tiles for a larger zoom level?  That are zoomed in?

No.  But adding this wouldn't be terribly difficult as zooming in is a lot like zooming out.  Only in the other direction.

##What else does it do?

Currently, it can rewrite tiles from standard URL templates in three formats and move them around, and also reprocess the base zoom level of tiles it is provided.

##But what about JPEGs?

If you want JPEG support, find a way to bring some variant of libjpeg in, sniff the magic bytes, pass the format through and submit a pull request.  Frankly, I found aggressively optimized PNG always outperformed JPEG, but note I am not using satellite or aerial imagery.

##What kind of resampling does it do?

By default when built for OS X, it uses Lanczos 3x3 with filtering to control ringing and overshoot / undershoot artifacts.  The core Lanczos resampling is provided by vImage in the Accelerate framework.

If the Accelerate framework is not available, it falls back upon a modified average algorithm.  The modification being that it will not average pixels whose alpha channel is 0, as this indicates NODATA.

In both cases, the processing is specific to GIS rasters.

Requirements
============

1. A directory of EPSG:3857 "slippy" web map tiles, where the filename and/or path contain parseable tile x, y and z coordinates.
2. File format: PNG
3. Google Maps y-axis convention (upper-left origin, C memory order)
4. Mac OS X (shouldn't be terribly difficult to get it to build in *nix/BSD. Windows will take slightly more effort.)
5. Write permissions to /tmp/

Supported Path "Templates"
==========================
```
1. /{z}/{x}/{y}.png (OSM-like; default)
2. /{z}/filename_{z}_{x}_{y}.png
3. /{z}/filename_{x}_{y}_{z}.png
```

It is recommended that OSM paths be used for performance, as placing hundreds of thousands of files in the same directory is often problematic.

Usage Example
=============
Assume in the path /tiles, there are map tiles for zoom level 13.  They use OSM convention for naming.  (eg: /tiles/13/6919/3522.png).  You want to create zoom levels 0 - 12, also in OSM format, and don't want to mess with the originals.

```
Retile /tiles/13 /tiles
Retile /tiles/12 /tiles
Retile /tiles/11 /tiles
Retile /tiles/10 /tiles
Retile /tiles/9 /tiles
Retile /tiles/8 /tiles
Retile /tiles/7 /tiles
Retile /tiles/6 /tiles
Retile /tiles/5 /tiles
Retile /tiles/4 /tiles
Retile /tiles/3 /tiles
Retile /tiles/2 /tiles
Retile /tiles/1 /tiles
```

Deployment Note
===============
After building Retile, you'll get a "Retile" executable and a libpng15.framework in the same directory.  Which is great, except Xcode is hardcoded to depend on frameworks being in ../Frameworks/.  So you have to fix this.

```
/Retile
/libpng15.framework
```

Move these files each to their own subdirectory as such:

```
/app/Retile
/Frameworks/libpng15.framework
```

It doesn't matter what you name "app".

My assumption as to the why of it is that deploying frameworks as part of the build is not expected for anything but a Cocoa ".app" bundle.  If there's a way to fix this with some build setting, I don't know what it is.

About/Licensing
===============
Created by Nick Dolezal. This code is released into the public domain.  (or CC0 if you like)

Porting Considerations
======================
See comments at the top of main.c.  Retile has only been tested and run on OS X 10.9 and 10.10.  It is likely porting it to *nix/BSD would mostly be a matter of creating a makefile and commenting out the Apple framework includes.  As noted in main.c, Windows will require replacing the posix function calls, most notably mkdir.

If you do port this to another platform I'm certainly willing to include and integrate that.

Third Party Components Used
===========================

##libpng
PNG read/write. 
http://ethan.tira-thompson.com/Mac_OS_X_Ports.html

##zlib
Compression/decompression for libpng. 
(reference only)

##NEONvsSSE_5 (Intel)
Macros that redefine ARM NEON SIMD intrinsics to Intel SSE/2/3/4 equivalents. Improves image filtering and PNG write performance. 
https://software.intel.com/en-us/blogs/2012/12/12/from-arm-neon-to-intel-mmxsse-automatic-porting-solution-tips-and-tricks

##tinydir
Multiplatform C-level directory reads. 
https://github.com/cxong/tinydir/blob/master/tinydir.h

##sqlite3
File database for data clustering. 
(reference only)

##Apple's Accelerate framework
Vectorized Lanczos image resize, vectorized RGB<->RGBA conversion. 
(reference only)

##Apple's libdispatch (GCD) framework
Thread pooling (multithreading). 
(reference only)
