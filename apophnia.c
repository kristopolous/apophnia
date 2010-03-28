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
	char img_root[PATH_MAX];
	char squash;
	unsigned int port;
} g_opts;

struct { 
	char*arg;
	char*string;
	void*param;
	int type;
} args[] = {
	{ "img_root", "Image Root", &g_opts.img_root, cJSON_String },
	{ "squash", "Squash images", &g_opts.squash, cJSON_Number },
	{ "port", "Mongoose Port", &g_opts.port, cJSON_Number },
	{ 0, 0, 0, 0}
};

struct {
	char *pfix;
	char *name;
} directives[] = {
	{ "r", "Resize" },
	{ "o", "Offset" },
	{ "f", "Format Change" },
	{ "q", "Quality" },
	{ 0, 0 }
};

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

	printf("Converting ...\n");
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

int parse_dimensions(char*ptr, int*height, int*width) {
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
			ptr[0] = 0;
			*width = atoi(last);
			ptr[0] = ':';
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
				printf("Trying %s\n", fname);
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
				printf("Command: [%s]\n", *pTmp);
				parse_dimensions(*pTmp, &height, &width);
				printf("height: %d\nwidth: %d\n", height, width);

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
		len = 0,
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
	strcpy(g_opts.img_root, "./");

	for(ix = 0; args[ix].arg; ix ++) {
		element = cJSON_GetObjectItem(ptr, args[ix].arg);
		if(element) {
			if(element->type == args[ix].type) {
				switch(args[ix].type) {
					case cJSON_String:
						strncpy((char*)args[ix].param, element->valuestring, PATH_MAX);
						printf(" %s: %s\n", args[ix].string, element->valuestring);
						break;

					case cJSON_Number:
						((int*)args[ix].param)[0] = element->valueint;
						printf(" %s: %d\n", args[ix].string, element->valueint);
						break;
				}
			}
		}
	}

	munmap(start, st.st_size);
	close(fd);

	if(chdir(g_opts.img_root)) {
		fatal("Couldn't change directories to %s",g_opts.img_root);
	}

	printf ("Directives:\n");
	for(ix = 0; directives[ix].pfix; ix ++) {
		printf(" [%s] %s\n", directives[ix].pfix, directives[ix].name);
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

	printf ("Starting Apophnia...\n");
	if(!read_config()) {
		fatal("Unable to read the config");
	}

       	ctx = mg_start();

	MagickWandGenesis();
	g_magick = NewMagickWand();

	mg_set_option(ctx, "ports", itoa(g_opts.port));
	mg_set_uri_callback(ctx, "/*", &show_image, NULL);

	printf("Ready\n");

	getchar();
	return 0;
}
