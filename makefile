CFLAGS= `pkg-config --cflags Wand` -O3
LDFLAGS= `pkg-config --libs Wand` -lpthread -lm -ldl -s
apophnia: apophnia.o mongoose/mongoose.o cjson/cJSON.o 

package:
	make clean
	cd ../ && tar czf apophnia.tgz apophnia
clean:
	rm -rf *.o */*.o apophnia *~ */*.so */*.a
install:
	install apophnia /usr/local/bin/
