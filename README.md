## What is apophnia

Apophnia is a dedicated image server protocol.  This is designed to solve all of the common image serving problems that are a pain to deal with for anyone that has to deal with a lot of images.

**This is not intended to serve HTML, CSS, JAVASCRIPT, or any other kind of document. It just serves images.**

# Image Serving Problems

  * Various sizes of images are needed for various purposes.
  * Various miscellaneous transformations of images are needed for special purposes.
  * Image serving must be fast. An ideal web page will probably serve dozens of images, even with 1 compacted html file.
  * Images should have their own caching rules because how they change is different from the text content.
  * Images need to be dealt with in a way that doesn't break file systems because of their massive volume.
  * Images should have restful URLs to be saved to disk easily and ready for SEO.

# What apophnia tries to achieve:

  * Be able to use new resolutions on the fly.
  * Have these dynamically created images cached.
  * Incur at most a one-time overhead in the process.
  * Have a dedicated image web server or a web server module to do it.

# What an apophnia request looks like

  * Look for myimage_r500x500.jpg.
  * If not found, back up, try myimage.jpg
  * See that (_r500x500) is a resize directive
  * Dynamically resize myimage.jpg to 500x500, serve that image
  * Save a new file to disk myimage_r500x500.jpg so that when it is requested again ... it's easy

# Directives

### Supported Directives

* RESIZE `r[HEIGHT(xWIDTH)]`
 _example_ `myfile_r1000x800.jpg` `myfile_r400.jpg` (creates a 400x400)
* OFFSET `o[HEIGHTxWIDTH [ [p|m] VERTICAL ( [p|m] HORIZONTAL) ]`
 *Note the syntax is "p" and "m", not "+" and "-" because of HTML escape sequences*
 _example_ `myfile_o100x100p100p50.jpg` `myfile_o400x400m10m40.jpg`.  It mattes white.
* QUALITY `qINTERGER` (0 lowest, 99 highest)
 _example_  `myfile_q60.png` `myfile_q54.jpg`
* NOP `_`
 _example_ `myfile________q60.png` `myfile__q54.jpg`
* FORMAT if *x* is specified and doesn't exist, then seek out other images in the order of *y*
 * GIF: png, bmp, jpg, jpeg, *fail*
 * JPG: jpeg, png, bmp, gif, tga, tiff, *fail*
 * PNG: gif, bmp, jpg, jpeg, tiff, *fail*
 * JPEG: jpg, png, bmp, gif, tga, tiff, *fail*
 * BMP: png, jpg, gif, jpeg, *fail*
BMP Note: DIB v.5 (Win98/2K+) "supports BMP being a container format for both PNG and JPEG images":http://en.wikipedia.org/wiki/BMP_file_format#Bitmap_Information_.28DIB_header.29 and still being a valid BMP.  Since JPEG has no alpha channel and BMP's alpha channel is the same engine as PNG's in IE 6, when a BMP is requested it will be a v.5 DIB encapsulated PNG to preserve the space, unless otherwise specified by the *true_bmp* configuration option.

### Proposed Directives

### Notes

h4. Chaining

Directives can of course be chained.  If you have a file, say, a 2000x2000 file, myfile.bmp then you can do
`myfile_r1000x1000_o250x250p250p250_q50.png`
Here's the steps:

  * myfile_r1000x1000_o250x250p250p250_q50.png is sought out, fails. _quality 50_ is pushed on the stack. IOCount = 1
  * myfile_r1000x1000_o250x250p250p250.png fails. _250x250 at offset 250x250_ is pushed on the stack. IOCount = 2
  * myfile_r1000x1000.png fails. _resize as 1000x1000_ is pushed on the stack. IOCount = 3
  * myfile.png fails. _emit as png_ is flagged. IOCount = 4
  * myfile.gif fails ... myfile.bmp succeeds. IOCount = 5
  * myfile.bmp is opened. IOCount = 6
  * myfile.bmp is resized to 1000x1000
  * a 250x250 image is extracted at offset 250x250
  * it is encoded as png with an aggressiveness level of 4 (0-9 is png)
  * It is served to the client and asynchronously written to disk at myfile_r1000x1000_o250x250p250p250_q50.png IOCount = 7

As you can see, the first time the image is served, it is quite expensive.  But now another client will request the same image:
`GET /myfile_r1000x1000_o250x250p250p250_q50.png  HTTP/1.1`

 * myfile_r1000x1000_o250x250p250p250_q50.png is sought out, found.  Served to client.

Much better the second time around, eh?

# Configuration File

The config file is called apophnia.conf and is in "JSON":http://www.json.org/ format. 

# Implementations

There is a protocol (discussed above) and implementations (discussed below).  The following implementations exist:

  * C/ImageMagick/Mongoose
<!--  * PHP/ImageMagick/(Your choice)-->

Implementations intend to achieve the following goals:

  * Manage the request to convert images
  * Convert source images to destination format
  * Cache images for future use
  * Update cache when necessary
  * Discard old images from cache

## C Implementation
### Supported Options
* `"port": INTEGER` - default: 2345
  The port to run apophnia on.
* `"img_root": STRING` - default: "./"
  The root directory of images to serve
* `"proportion": ["squash", "crop", "matte", "seamcarve"]` - default: squash. If a 200x1000 image is requested at 200x200, then you can either
 * squash: Squash the image disproportionally
 * crop: Center the content and crop the excess pixels
 * matte: Take the 200x1000 image, resize it to 40x200, center it, and matte it on a 200x200 white background
 * seamcarve: See the "wikipedia article":http://en.wikipedia.org/wiki/Seam_carving
* `"true_bmp:" INTEGER (0/1)` - default: 0
  Whether to serve a true, uncompressed bmp, or to encapsulate it in a DIB png
* `"log_level": [0 ... 3]` - default: 0

 * 0 - log only crashing conditions.
 * 1 - log file creations and updates
 * 2 - log all requests
 * 3 - log as if it's not a performance hit

* `"log_file": STRING` - default: /dev/stdout
  Where the log files go...

* `"404": STRING` - default: empty
  The image to serve (if any) when no image is found.

* `"disk": BOOLEAN` - default: 1 (true) 
  Whether or not to write the converted files to disk

### Proposed Options
* `"no_support": Array("DIRECTIVE1", "DIRECTIVE2")` - default: empty/everything supported
  Example:  To disable the quality and resizing directives, you can use `"no_support": ["resize", "quality"]` 
