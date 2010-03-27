CFLAGS= `pkg-config --cflags Wand` `pkg-config --cflags glib-2.0` -Wall -O2 -march=pentium4
LDFLAGS= `pkg-config --libs Wand` `pkg-config --libs glib-2.0` -lpthread -lm -ldl -s
images: images.o mongoose/mongoose.o credis/credis.o cjson/cJSON.o 

package:
	make clean
	cd ../ && tar czf images.tgz images
clean:
	rm -rf *.o */*.o images *~ */*.so */*.a
install:
	install image /usr/local/bin/
