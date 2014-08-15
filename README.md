Retile
======

Reprocess raster map tiles.

Primarily, Retile works to create additional zoom levels for web map tiles, either zooming in or out.  Sort of like gdal2tiles, only it takes a directory of PNG files as an input, not a huge monolithic raster.

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

##Ok, I'm sold and want to run it but I'm not a developer...?

If you're running OS X, you're in luck.  Just download the current release binary from https://github.com/Safecast/Retile/releases

##Are there any examples of this in use?

Yes.  http://safecast.org/tilemap uses Retile on the webserver to "crush" the Safecast interpolation overlay tiles, as well as create zoom levels 0 - 12, and 14 - 15.  It is otherwise prohibitive to run a new interpolation for every zoom level, so Retile not only saves a great deal of computational time but also removes pngcrush from the workflow.

##Is it fast?

The code is SIMD vectorized and multithreaded, which is to say, yes (it does about 1k tiles per second on an i5/i7).  For downsampling, the single largest performance factor however is the specific manner in which it processes files and performs data clustering, which is extremely efficient.

Also, it cheats.

##It cheats?

Yes.  For downsampling, instead of the slow approach of creating every zoom level from the highest resolution available, instead it resamples each zoom level iteratively, such that scaling is always 50%.  There is additional filtering to prevent this from creating artifacts, so the loss of precision is not significant.

##You said it replaces pngcrush?

In the above workflow, yes.  The PNG writing is highly optimized by default and quite performant.  For example, for the Safecast interpolated tiles, using Apple's ImageIO framework yields a total filesize of 235.8 MB.  This code yields a total filesize of 74.8 MB and runs faster.

##What else does it do?

Currently, it can rewrite tiles from standard URL templates in three formats and move them around, and also reprocess the base zoom level of tiles it is provided.

##But what about JPEGs?

If you want JPEG support, find a way to bring some variant of libjpeg in, sniff the magic bytes, pass the format through and submit a pull request.  Frankly, I found optimized PNG always outperformed JPEG, but note I am not using satellite or aerial imagery.

##Can it create tiles for a higher zoom level?  That are zoomed in?

Yes, Retile can now create enlarged tiles.  It should be noted that Retile's enlarging is a relatively novel implementation.  For tiles which are not photographic imagery, Retile uses resampling methods from emulators to deliver better results than with traditional resampling methods.  And, for photographic imagery, Lanczos etc remain available.

##What kind of resampling does it do?

###### Downsampling

- Default *(OS X)*: Lanczos 3x3 *(filtered for ringing artifacts and undershoot)*
- Default *(other)*: Average *(custom NODATA handling)*
- Optional: Lanczos 5x5 *(OS X)*

###### Downsampling: Notes

Downsampling is relatively straightforward and should not require any configuration.  Good results can be obtained with a simple average, and with a little filtering, Lanczos delivers even better results with any kind of source raster.

The output of either Lanczos or average has a "pixel perfect" alpha channel, even with successive generations, and can be used with bitstore.js for client-side indexing.

###### Enlarging

- Default: EPX
- Optional: Lanczos 3x3 *(filtered)* *(OS X)*, Lanczos 5x5 *(filtered)* *(OS X)*, Nearest Neighbor, Bilinear *(filtered)*, Eagle

###### Enlarging: Notes

For tiles that are not photographic imagery, enlarging tiles is quite different from downsampling them, as common resampling techniques produce unexpectedly disappointing results.  This is why EPX is the default method for enlarging tiles.

This is the same basic problem emulators have when trying to upscale pixel art with a limited color palette (eg, Mario).  So, although somewhat unorthodox, it makes sense to leverage emulator resampling algorithms for GIS use here.  What's good for Mario is good for your average non-photographic imagery tile.

More info: https://en.wikipedia.org/wiki/Image_scaling

On the other hand, for satellite or aerial photographic imagery, Lanczos is probably your best bet.  If I included a comparison of the prototypical Lenna image, Lanczos would win.

Between EPX and Lanczos you should be able to relatively easily extend any tile set by another few zoom levels, even if traditional GIS resampling methods haven't worked for you in the past here. (of course, rendering higher-resolution data in the first place is preferable)



##tl;dr: Resampling Method Comparison

Visual comparison, for enlarging tiles: (click to zoom in)

 GIS Interpolation | Basemap | Basemap | Emulator (reference) 
 ----------------- | ------- | ------- | --------------------
 [![overlay_compare_4x](https://github.com/Safecast/Retile/raw/master/sample/overlay_compare_4x_thumb.png)](https://github.com/Safecast/Retile/raw/master/sample/overlay_compare_4x.png) | [![gmaps_compare_4x](https://github.com/Safecast/Retile/raw/master/sample/osm_compare_4x_thumb.png)](https://github.com/Safecast/Retile/raw/master/sample/osm_compare_4x.png) | [![gmaps_compare_4x](https://github.com/Safecast/Retile/raw/master/sample/gmaps_compare_4x_thumb.png)](https://github.com/Safecast/Retile/raw/master/sample/gmaps_compare_4x.png) | [![mario_compare_4x](https://github.com/Safecast/Retile/raw/master/sample/mario_compare_4x_thumb.png)](https://github.com/Safecast/Retile/raw/master/sample/mario_compare_4x.png) |

- Nearest neighbor (NN): This provides acceptable results for zooming in by about 2x, but not really beyond that.  While NN never creates artifacts, it is blocky.
- Lanczos: For images with limited color depth, Lanczos does not handle the underlying rasterized geometry features terribly well, and actually draws attention to that with ringing artifacts.  Bilinear doesn't have the ringing artifacts, but really isn't any better.
- EPX (Scale2x): EPX is sort of a specialized variant of NN with edge detection.  It outperforms traditional resampling techniques on most rasters that are not photographic imagery.
- xBRZ: xBRZ is similar to, yet significantly better than EPX.  It was the best "pixel art" type resampler I found.  The differences between EPX are subtle on the interpolation sample image, but xBRZ is significantly better on the sample OSM tile. *(xBRZ not implemented in Retile)*




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
1. /{z}/{x}/{y}.png              (OSM-like; default)
2. /{z}/filename_{z}_{x}_{y}.png
3. /{z}/filename_{x}_{y}_{z}.png
```

It is recommended that OSM paths be used for performance, as placing hundreds of thousands of files in the same directory is often problematic.

Usage Example: Downsampling
===========================
Assume in the path /tiles, there are map tiles for zoom level 13.  They use OSM convention for naming.  (eg: /tiles/13/6919/3522.png).

You want to create zoom levels 0 - 12, also in OSM format, and don't want to mess with the originals.

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

Usage Example: Enlarging
========================
Assume in the path /tiles, there are map tiles for zoom level 13.  They use OSM convention for naming.  (eg: /tiles/13/6919/3522.png).

You want users to be able to zoom in further on the map, and want to create zoom levels 14 and 15.

```
Retile /tiles/13 /tiles -zIn
Retile /tiles/14 /tiles -zIn
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

Data Processing Model
=====================
![Retile_Conceptual](https://github.com/Safecast/Retile/raw/master/sample/Retile_Conceptual.png)

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
