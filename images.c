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
#include <glib.h>

#include <wand/MagickWand.h>
#include "mongoose/mongoose.h"
#include "credis/credis.h"
#include "cjson/cJSON.h"

#define CONFIG "images.conf"

REDIS g_redis; 
cJSON *g_config;
MagickWand *g_magick;

struct {
	char img_root[PATH_MAX];
	char redis_host[PATH_MAX];
	int redis_port;
	unsigned int port;

	int ttl;	// in sec ... max ttl = 68.0961 yrs
	int maxmem;	// in KiB ... max mem = 2 TiB
} g_opts;

struct { char*arg;
	void*param;
	int type;
} args[] = {
	{ "redis_host", &g_opts.redis_host, cJSON_String },
	{ "redis_port", &g_opts.redis_port, cJSON_Number },
	{ "img_root", &g_opts.img_root, cJSON_String },
	{ "ttl", &g_opts.ttl, cJSON_Number },
	{ "port", &g_opts.port, cJSON_Number },
	{ "maxmem", &g_opts.maxmem, cJSON_Number }
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

unsigned char* convert_image(char* file, int height, int width, size_t*sz){
	MagickBooleanType stat;

	printf("Opening %s\n", file);
	stat = MagickReadImage(g_magick, file);

	if (stat == MagickFalse) {
		return 0;
	}

	MagickResetIterator(g_magick);

	printf("Converting %s\n", file);
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

static void show_image(struct mg_connection *conn,
		const struct mg_request_info *request_info,
		void *user_data) {

	int height = -1, width = -1, ret;	
	char *file, *ptr, *last, *key;
	unsigned char *image;
	char *b64;
	size_t sz;

	key = ptr = request_info->uri + 1;
	ret = credis_get(g_redis, key, &b64);
	if(ret != -1) {
		image = g_base64_decode(b64, &sz);
	} else {
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
			if(ptr[0] == ':') {
				ptr[0] = 0;
				width = atoi(last);
				ptr[0] = ':';
				if(height == -1) {
					height = width;
				}
				ptr++;
				break;
			}
			if(ptr[0] <= 32) {
				break;
			}
		}
		file = ptr;
		
		image = convert_image(file, height, width, &sz);
		if(!image) {
			mg_printf(conn, "%s", "HTTP/1.1 404 NOT FOUND\r\n");
		}
		printf("Saving %s in Redis\n", key);
		b64 = g_base64_encode(image, sz);
		credis_set(g_redis, key, b64);
		credis_expire(g_redis, key, g_opts.ttl);
	}

	mg_printf(conn, "%s", "HTTP/1.1 200 OK\r\n");
	mg_printf(conn, "%s", "Content-Type: image/jpeg\r\n");
	mg_printf(conn, "Content-Length: %d\r\n", sz);
	mg_printf(conn, "%s", "Connection: Close\r\n\r\n");
	mg_write(conn, image, sz);
	if(ret == -1) {
		MagickRelinquishMemory(image);
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
	g_opts.redis_port = 6379;
	g_opts.ttl = 60;
	g_opts.maxmem = 1000 * 1000;
	strcpy(g_opts.redis_host, "localhost");
	strcpy(g_opts.img_root, "./");

	len = sizeof(args);
	for(ix = 0; ix < len; ix ++) {
		element = cJSON_GetObjectItem(ptr, args[ix].arg);
		if(element) {
			if(element->type == args[ix].type) {
				switch(args[ix].type) {
					case cJSON_String:
						strncpy((char*)args[ix].param, element->valuestring, PATH_MAX);
						break;

					case cJSON_Number:
						((int*)args[ix].param)[0] = element->valueint;
						break;
				}
			}
		}
	}
	printf(" Port: %d \n", g_opts.port);
	printf(" Maximum memory: %d KiB\n", g_opts.maxmem);
	printf(" TTL: %d seconds\n", g_opts.ttl);
	printf(" Image Root: %s\n", g_opts.img_root);

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

	printf("Reading Configuration\n");
	if(!read_config()) {
		fatal("Unable to read the config");
	}

	printf("Starting Mongoose\n");
       	ctx = mg_start();

	printf ("Connecting to Redis @ %s:%d...", g_opts.redis_host, g_opts.redis_port);
	g_redis = credis_connect(g_opts.redis_host, g_opts.redis_port, 10000);
	if(!g_redis) {
		fatal ("Couldn't connect to Redis");
	}
	printf("OK\n");

	printf("Initializing ImageMagick\n");
	MagickWandGenesis();
	g_magick = NewMagickWand();

	mg_set_option(ctx, "ports", itoa(g_opts.port));
	mg_set_uri_callback(ctx, "/*", &show_image, NULL);

	printf("[Ready!]\n");

	getchar();
	return 0;
}
