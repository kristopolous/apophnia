/*
 * Apophnia - An image server - https://github.com/kristopolous/apophnia
 * 
 * Copyright (c) 2011, Chris McKenzie
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this list 
 * of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this 
 * list of conditions and the following disclaimer in the documentation and/or other 
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY 
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 */

#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#ifdef __linux__ // {
  #include <sys/inotify.h>
  #include <linux/limits.h>
  #include <linux/types.h>
  #define NOTIFY_INIT  inotify_init()
  #define EVENT_SIZE  (sizeof (struct inotify_event))
  #define BUF_LEN      (1024 * (EVENT_SIZE + 16))
#elif defined __OpenBSD__ || __FreeBSD__ || __NetBSD__ || __APPLE__ 
  #include <sys/event.h>
  #define NOTIFY_INIT  kqueue()
#endif

#include <wand/MagickWand.h>
#include "mongoose/mongoose.h"
#include "cjson/cJSON.h"

#define CONFIG        "apophnia.conf"
#define BUFSIZE       16384
#define MAX_DIRECTIVES 16
#define ASSERT_CHAR(ptr, chr) ((ptr[0] == chr) && ptr++)

cJSON *g_config;

int 
  g_notify_handle, 
  g_notify,
  g_stat_check = 0;

struct {
  char 
    img_root[PATH_MAX],
    propotion,
    true_bmp;

  int 
    port,
    log_fd,
    log_level;
} g_opts;

struct { 
  char
    *arg,
    *string;

  void
    *param;

  int type;

} args[] = {
  { "port", "Mongoose Port", &g_opts.port, cJSON_Number },
  { "img_root", "Image Root", &g_opts.img_root, cJSON_String },
  { "propotion", "Proportion", &g_opts.propotion, cJSON_String },
  { "true_bmp", "True BMP", &g_opts.true_bmp, cJSON_Number },
  { "no_support", "Proportion", &g_opts.img_root, cJSON_String },
  { "log_level", "Log Level", &g_opts.log_level, cJSON_Number },
  { "log_file", "Log File", &g_opts.log_fd, cJSON_String },
  { 0, 0, 0, 0 }
};

#define P_SQUASH  0
#define P_CROP    1
#define P_MATTE   2
#define P_SEAM    3
const char *proportion[] = {
  "squash",
  "crop",
  "matte",
  "seamcarve",
  0
};

#define D_RESIZE  'r'
#define D_OFFSET  'o'
#define D_QUALITY  'q'

struct {
  char pfix;
  char *name;
} directives[] = {
  { D_RESIZE, "Resize" },
  { D_OFFSET, "Offset" },
  { D_QUALITY, "Quality" },
  { 0, 0 }
};

struct {
  char 
    *extension,
    *fallbacks[8];

} formatCheck[] = {
  { "jpg", { "jpeg", "png", "bmp", "gif", "tga", "tiff", 0 } },
  { "png", { "gif", "bmp", "jpg", "jpeg", "tiff", 0 } },
  { "gif", { "png", "bmp", "jpg", "jpeg", 0 } },
  { "jpeg", { "jpg", "png", "bmp", "gif", "tga", "tiff", 0 } },
  { "bmp", { "png", "jpg", "gif", "jpeg", 0 } },
  { 0, { 0 } }
};

void (*plog0)(const char*t, ...);
void (*plog1)(const char*t, ...);
void (*plog2)(const char*t, ...);
void (*plog3)(const char*t, ...);

void log_real(const char*t, ...) {
  va_list ap;
  char *s;
  va_start(ap, t);

  while(*t) {
    if(*t == '%') {
      t++;
      if(*t == 's') {
        s = (char*) va_arg(ap, char *);
        if(!s) {
          printf("<null>");
        } else {
          printf("%s", s);
        }
      } else if(*t == 'd') {
        printf("%d", (int) va_arg(ap, int));
      }
    } else {
      putchar(t[0]);
    }

    t++;
  }

  va_end(ap);
  printf ("\n");
}

void log_fake(const char*t, ...) {
  return;
}

void fatal(const char*t, ...) {
  va_list ap;
  char *s;

  printf ("Fatal: ");

  va_start(ap, t);
  while(*t) {
    if(*t == '%') {
      t++;
      if(*t == 's') {
        s = (char*) va_arg(ap, char *);
        if(!s) {
          printf("<null>");
        } else {
          printf("%s", s);
        }
      } else if(*t == 'd') {
        printf("%d", (int) va_arg(ap, int));
      }
    } else {
      putchar(t[0]);
    }
    t++;
  }
  va_end(ap);

  printf ("\n");
  exit(0);
}

int atoi_ptr(char**ptr) {
  char *local;
  int out = 0, mult = 1;

  local = *ptr;
  if(local[0] == 'm') {
    local++;
    mult = -1;
  } else if (local[0] == 'p') {
    local++;
  }
  for(;; local++) {
    if(local[0] >= '0' && local[0] <= '9') {
      out *= 10;
      out += local[0] - '0';
    } else {
      break;
    }
  }
  *ptr = local;
  return out * mult;
}

char *itoa(int in) {
  static char 
    ret[12],
    *ptr;

  memset(ret,0,12);
  ptr = ret + 11;

  while(in > 0) {
    ptr--;
    *ptr = (in % 10) + '0';
    in /= 10;
  }

  return ptr;
}

char check_for_change(int fd, char*ptr) {
  size_t 
    sz,
    ext_len;

  char 
    *pFlat,
    *pFile,
    *end = ptr + strlen(ptr),
    *ext;

  struct 
    stat   
    original,
    converted,
    test;

  short 
    ctr, 
    f_sz = end - ptr;

  pFlat = (char*)(malloc(f_sz * sizeof(char) * PATH_MAX * MAX_DIRECTIVES));

  // find the extension
  while(*end != '.' && end > ptr) { end --; }
  ext = end;
  ext_len = strlen(ext);
  
  fstat(fd, &converted);

  for(ctr = 0; ctr < MAX_DIRECTIVES; ctr++) {
    pFile = pFlat + (ctr * f_sz);

    // truncate the directive
    while(*end != '_' && end > ptr) { end --; }
    if(end == ptr) {
      break;
    }
    // copy up to this point in the name
    sz = end - ptr;
    memcpy(pFile, ptr, sz);
    memcpy(pFile + sz, ext, ext_len);
    pFile[sz + ext_len] = 0;

    fflush(0);

    if(stat(pFile, &test)) { 
      break;
    } else {
      memcpy(&original, &test, sizeof(struct stat));
    }

    end --;
  }

  // we found the base, it's the stat of test
  if(  test.st_mtime > converted.st_mtime || 
    test.st_ctime > converted.st_ctime
  ) {
    plog3("Found out of date files... removing");
    
    // invalidate the incoming image
    unlink(ptr);
    plog3(ptr);

    // invalidate the chained images
    for( ctr -= 2; ctr >= 0; ctr--) {
      pFile = pFlat + (ctr * f_sz);
      plog3(pFile);
      unlink(pFile);
    }

    return 0;
  }

  free(pFlat);

  return 1;
}

int image_start(MagickWand *wand, int fd) {
  MagickBooleanType stat;
  FILE *fdesc = fdopen(fd, "rb");

  stat = MagickReadImageFile(wand, fdesc);
  if (stat == MagickFalse) {
    return 0;
  }

  MagickResetIterator(wand);

  while (MagickNextImage(wand) != MagickFalse);

  return 1;
}

unsigned char* image_end(MagickWand *wand, size_t *sz) {
  return MagickGetImageBlob(wand, sz);
}

int image_offset(MagickWand *wand, char*ptr){
  int 
    offsetY, 
    offsetX, 
    height, 
    width;

  height = atoi_ptr(&ptr);
  if(!ASSERT_CHAR(ptr, 'x')) {
    plog0("%s\n", ptr);
    return 0;
  }
  width = atoi_ptr(&ptr);

  offsetY = atoi_ptr(&ptr);
  offsetX = atoi_ptr(&ptr);

  return MagickCropImage(wand, width, height, offsetX, offsetY);
}

int image_quality(MagickWand *wand, char*ptr) {
  int quality = atoi(ptr);

  return MagickSetImageCompressionQuality(wand, quality);
}

int image_resize(MagickWand *wand, char*ptr) {
  char *last;

  int 
    height = -1, 
    width = -1;

  for(last = ptr;;ptr++) {
    if(ptr[0] >= '0' && ptr[0] <= '9') {
      continue;
    }
    if(ptr[0] == 'x') {
      ptr[0] = 0;
      height = atoi(last);
      ptr[0] = 'x';
      last = ptr + 1;
      continue;
    }
    if(ptr[0] <= 32) {
      width = atoi(last);
      if(height == -1) {
        height = width;
      }
      ptr++;
      break;
    }
  }

  MagickResizeImage(
    wand,
    height,
    width,
    LanczosFilter,
    1.0
  );

  return 1;
}

void *do404(struct mg_connection *conn) {
  mg_printf(conn, "%s", "HTTP/1.1 404 Not Found\r\n");
  mg_printf(conn, "%s", "Content-Type: text/plain\r\n");
  mg_printf(conn, "%s", "Connection: Close\r\n");
  return (void*)1;
}

void *show_image(
    enum mg_event event,
    struct mg_connection *conn,
    const struct mg_request_info *request_info
  ) {

  int 
    ret,
    fd = -1; 

  char 
    fname[PATH_MAX],
    butcher[PATH_MAX],
    buf[BUFSIZE],

    *commandList[12], 
    **pCommand = commandList, 
    **pTmp,

    *ptr, 
    *last, 
    *ext = 0;

  unsigned char *image;

  struct stat st;
  size_t sz;
        
  MagickWand *wand = NewMagickWand();
  
  // first we try to just blindly open the requested file
  ptr = request_info->uri + 1;
  
  strcpy(fname, ptr);
  strcpy(butcher, ptr);

  ptr = butcher;
  do {
    fd = open(ptr, O_RDONLY);
    
    // If the source image changes, then we have to change the converted images
    // But because we don't want a bunch of inotifies and we want to make this
    // rather kernel-neutral, we just do an occational stat on the base file
    // to see if it has a different mtime or ctime.
    //
    // Even though this isn't atomically incremented, it doesn't matter.   
    // The point is that we wish to do *occasional* checks just so we aren't
    // way out of sync.
    g_stat_check++;

    if(fd > 0) {
      if (! (g_stat_check & 0x7F)) {
        // get the stat of the open file handle
        if(check_for_change(fd, ptr)) {
          close(fd);
        } else {
          break;
        }
      }
      break;
    }

    // we start the string parsing routines.
    last = ptr + strlen(ptr);

    // this passes over the string ^^ once and then VV twice

    // get the extension
    for(; (last > ptr) && (*last != '.'); last--);
    
    if(last[0] == '.') {
      last[0] = 0;
      ext = last + 1;
    } else {
      break;
    }

    for(;;) {
      // find the first clause to try to dump
      for(; (last > ptr) && (*last != '_'); last--);

      if(last[0] == '_') {
        last[0] = 0;

        // add this as a command
        *pCommand = (last - ptr) + 1 + butcher;
        pCommand++;
        fname[last-ptr] = '.';

        strcpy(fname + (last - ptr + 1), ext);
        plog3("Trying %s", fname);
        fd = open(fname, O_RDONLY);

        if(fd > 0) {
          break;
        }
      } else {
        // we must give up eventually
        break;
      }
    } 
  } while(0);

  // we have a file handle
  if(fd > 0) {
    if(fstat(fd, &st)) {
      return do404(conn);
    }

    mg_printf(conn, "%s", "HTTP/1.1 200 OK\r\n");
    mg_printf(conn, "%s", "Content-Type: image/jpeg\r\n");
    mg_printf(conn, "%s", "Connection: Keep-Alive\r\n");
    // if this is the case then we have a command string to parse
    if(pCommand != commandList) {
      
      // get the full requested name
      strcpy(fname, ptr);

      // now null out the extension pointer from above
      // we won't need it any more
      ext[0] = 0;
      image_start(wand, fd);
      for(pTmp = pCommand - 1; (pTmp + 1) != commandList; pTmp--) {
        plog3("Command: [%s]", *pTmp);

        switch(*pTmp[0]) {
          case D_RESIZE:
            image_resize(wand, *pTmp + 1);
            break;

          case D_OFFSET:
            image_offset(wand, *pTmp + 1);
            break;

          case D_QUALITY:
            image_quality(wand, *pTmp + 1);
            break;

          default:
            plog2("Unknown directive: %s", *pTmp);
            break;
        }  

      }
      image = image_end(wand, &sz);
      mg_printf(conn, "Content-Length: %d\r\n\r\n", sz);
      mg_write(conn, image, sz);
      MagickWriteImage(wand, request_info->uri + 1);
      MagickRelinquishMemory(image);
    } else {
      mg_printf(conn, "Content-Length: %d\r\n\r\n", st.st_size);
      for(;;) {  
        ret = read(fd, buf, BUFSIZE);
        if(!ret) {
          break;
        }
        mg_write(conn, buf, ret);
      }
    }
    close(fd);
    
  } else {
    return do404(conn);
  }

  DestroyMagickWand(wand);

  return 0;
}

int read_config(){
  char 
    *start = 0,
    *config = 0;

  cJSON 
    *ptr = 0,
    *element = 0;

  int 
    ix = 0, 
    fd = -1;

  struct stat st;
        
  fd = open(CONFIG, O_RDONLY);
  if(fd == -1) {
    fatal("Couldn't open %s", CONFIG);
  }

  if(fstat(fd, &st)) {
    fatal("fstat failure");
  }

  config = (char*) mmap((void*) start, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

  if(config == MAP_FAILED) {
    fatal("mmap failure");
  }

  g_config = ptr = cJSON_Parse((const char*)config);

  g_opts.port = 2345;
  g_opts.log_level = 0;

  strcpy(g_opts.img_root, "./");

  plog3 = plog2 = plog1 = log_fake;
  for(ix = 0; args[ix].arg; ix ++) {
    element = cJSON_GetObjectItem(ptr, args[ix].arg);
    if(element) {
      if(element->type == args[ix].type) {
        switch(args[ix].type) {
          case cJSON_String:
            if(!strcmp(args[ix].arg, "proportion")) {
              for(ix = 0; proportion[ix]; ix ++) {
                if(!strcmp(proportion[ix], element->valuestring)) {
                  ((char*)args[ix].param)[0] = ix;
                  break;
                }
              }
            } else if(!strcmp(args[ix].arg, "log_file")) {
              g_opts.log_fd = open(element->valuestring, O_WRONLY);
              if(!g_opts.log_fd) {
                g_opts.log_fd = open("/dev/stdout", O_WRONLY);
                plog0("Couldn't open log file");
              }
            } else {
              strncpy((char*)args[ix].param, element->valuestring, PATH_MAX);
              plog3(" %s: %s\n", args[ix].string, element->valuestring);
            }
            break;

          case cJSON_Number:
            ((int*)args[ix].param)[0] = element->valueint;
            plog3(" %s: %d\n", args[ix].string, element->valueint);
            break;
        }
      }
    }
  }

  // set up the logs
  switch(g_opts.log_level) {
    case 3:
      plog3 = log_real;
    case 2:
      plog2 = log_real;
    case 1:
      plog1 = log_real;
  }

  munmap(start, st.st_size);
  close(fd);

  if(chdir(g_opts.img_root)) {
    fatal("Couldn't change directories to %s",g_opts.img_root);
  }

  return 1;
}

void main_loop(){
#if !defined __linux__
  #error KQUEUE needs to be written.  Exiting.
#else
  fd_set rfds;

  int 
    ret,
    i,
    len;

  char buf[BUF_LEN];

  g_notify = inotify_add_watch (g_notify_handle,
    g_opts.img_root,
    IN_MODIFY | IN_CREATE | IN_DELETE
  );

  for(;;) {
    FD_ZERO (&rfds);
    FD_SET (g_notify_handle, &rfds);
    ret = select (g_notify_handle + 1, &rfds, NULL, NULL, NULL);

    if (FD_ISSET (g_notify_handle, &rfds)) {

      len = read(g_notify_handle, buf, BUF_LEN);

      while (i < len) {
        struct inotify_event *event;

        event = (struct inotify_event *) &buf[i];

        printf ("wd=%d mask=%u cookie=%u len=%u\n",
          event->wd, event->mask,
          event->cookie, event->len);

        if (event->len)
          printf ("name=%s\n", event->name);

        i += EVENT_SIZE + event->len;
      }
    }
  }
#endif
}

int main() {
  struct mg_context *ctx;

  plog0 = log_real;
  plog0("Starting Apophnia...");
 
  if(!read_config()) {
    plog0("Unable to read the config");
  }

  g_notify_handle = NOTIFY_INIT;

  MagickWandGenesis();

  {
    const char *options[] = {
      "listening_ports", itoa(g_opts.port),
      NULL
    };

    plog3("Listening on port %d", g_opts.port);
    ctx = mg_start(&show_image, NULL, options);
  }

  main_loop();
  return 0;
}
