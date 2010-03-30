#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/limits.h>
#include <linux/types.h>
#include <stdint.h>
#include <string.h>

#include <wand/MagickWand.h>
#include "mongoose/mongoose.h"
#include "cjson/cJSON.h"

#define CONFIG "apophnia.conf"

cJSON *g_config;
MagickWand *g_magick;

struct {
	char img_root[PATH_MAX],
	     propotion,
	     true_bmp;

	int 	port,
		log_fd,
		log_level;
} g_opts;

struct { 
	char*arg;
	char*string;
	void*param;
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

#define P_SQUASH	0
#define P_CROP		1
#define P_MATTE		2
#define P_SEAM		3
const char * proportion[] = {
	"squash",
	"crop",
	"matte",
	"seamcarve",
	0
};

#define D_RESIZE	'r'
#define D_OFFSET	'o'
#define D_QUALITY	'q'

struct {
	char pfix;
	char *name;
} directives[] = {
	{ D_RESIZE, "Resize" },
	{ D_OFFSET, "Offset" },
	{ D_QUALITY, "Quality" },
	{ 0, 0 }
};

void (*plog0)(const char*t, ...);
void (*plog1)(const char*t, ...);
void (*plog2)(const char*t, ...);
void (*plog3)(const char*t, ...);

void log_real(const char*t, ...) {
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

unsigned char* convert_image(int fd, int height, int width, size_t*sz){
	MagickBooleanType stat;
	FILE *fdesc = fdopen(fd, "rb");

	stat = MagickReadImageFile(g_magick, fdesc);

	if (stat == MagickFalse) {
		return 0;
	}

	MagickResetIterator(g_magick);

	plog3("Converting ...\n");
	while (MagickNextImage(g_magick) != MagickFalse) {
		MagickResizeImage(
				g_magick,
				height,
				width,
				LanczosFilter,
				1.0);
	}

	return MagickGetImageBlob(g_magick, sz);
}

int image_offset(char*ptr){
	return 1;
}
int image_resize(char*ptr, int*height, int*width) {
	char * last;
	*height = -1;
	*width = -1;
	for(last = ptr;;ptr++) {
		if(ptr[0] >= '0' && ptr[0] <= '9') {
			continue;
		}
		if(ptr[0] == 'x') {
			ptr[0] = 0;
			*height = atoi(last);
			ptr[0] = 'x';
			last = ptr + 1;
			continue;
		}
		if(ptr[0] <= 32) {
			*width = atoi(last);
			if(*height == -1) {
				*height = *width;
			}
			ptr++;
			break;
		}
	}
	return 1;
}

#define BUFSIZE	16384
static void show_image(struct mg_connection *conn,
		const struct mg_request_info *request_info,
		void *user_data) {

	int  ret, fd = -1, height, width;

	char fname[PATH_MAX];
	char buf[BUFSIZE];

	char *commandList[12], **pCommand = commandList, **pTmp;

	char *ptr, *last, *ext = 0;
	unsigned char *image;

	struct stat st;
	size_t sz;
      	
	
	// first we try to just blindly open the requested file
	ptr = request_info->uri + 1;
	
	strcpy(fname, ptr);

	do {
		fd = open(ptr, O_RDONLY);
		
		if(fd > 0) {
			break;
		}

		// we start the string parsing routines.
		last = ptr + strlen(ptr);

		// this passes over the string ^^ once and then VV twice

		// get the extension
		for(; (last > ptr) && (*last != '.'); last--);
		
		if(last[0] == '.') {
			ext = last;
		} else {
			break;
		}

		for(;;) {
			// find the first clause to try to dump
			for(; (last > ptr) && (*last != '_'); last--);

			if(last[0] == '_') {
				*pCommand = last + 1;
				pCommand++;
				strcpy(fname + (last - ptr), ext);
				plog3("Trying %s\n", fname);
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
			mg_printf(conn, "%s", "HTTP/1.1 404 NOT FOUND\r\n");
			return;
		}

		mg_printf(conn, "%s", "HTTP/1.1 200 OK\r\n");
		mg_printf(conn, "%s", "Content-Type: image/jpeg\r\n");
		mg_printf(conn, "%s", "Connection: Close\r\n");
		// if this is the case then we have a command string to parse
		if(pCommand != commandList) {
			
			// get the full requested name
			strcpy(fname, ptr);

			// now null out the extension pointer from above
			// we won't need it any more
			ext[0] = 0;
			for(pTmp = commandList; pTmp != pCommand; pTmp++) {
				plog3("Command: [%s]\n", *pTmp);
				switch(*pTmp[0]) {
					case D_RESIZE:
						image_resize(*(pTmp + 1), &height, &width);
						break;

					case D_OFFSET:
						image_offset(*(pTmp + 1));
						break;

					case D_QUALITY:
						break;

					default:
						plog2("Unknown directive: %s", *pTmp);
						break;
				}	

				plog3("height: %d\nwidth: %d\n", height, width);

				image = convert_image(fd, height, width, &sz);
			}
			mg_printf(conn, "Content-Length: %d\r\n\r\n", sz);
			mg_write(conn, image, sz);
			MagickWriteImage(g_magick, fname);
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
		
	} else {
		mg_printf(conn, "%s", "HTTP/1.1 404 NOT FOUND\r\n");
	}
}

int read_config(){
	char 	*start = 0,
		*config = 0;

	cJSON 	*ptr = 0,
		*element = 0;

	int 	ix = 0, 
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
	plog3 = plog2 = plog1 = log_fake;
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

char*itoa(int in) {
	static char ret[12],
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
int main() {
	struct mg_context *ctx;

	plog0 = log_real;
	plog0("Starting Apophnia...");
	if(!read_config()) {
		plog0("Unable to read the config");
	}

       	ctx = mg_start();

	MagickWandGenesis();
	g_magick = NewMagickWand();

	mg_set_option(ctx, "ports", itoa(g_opts.port));
	mg_set_uri_callback(ctx, "/*", &show_image, NULL);

	getchar();
	return 0;
}
