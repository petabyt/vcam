SO_CFLAGS=-g -I. "-DVCAMERADIR=\"sd\"" $(shell pkg-config --cflags libusb-1.0)
SO_FILES=log.o libusb.o vcamera.o libgphoto2_port/gphoto2-port-portability.o

CFLAGS=-I../lib/ -L.
LDFLAGS=-L. -Wl,-rpath=.

$(SO_FILES): CFLAGS=$(SO_CFLAGS)

main: main.o libusb.so
	$(CC) main.o -lcamlib $(CFLAGS) -o main $(LDFLAGS) -lusb

bins:
	xxd -i bin/* > data.h

vcamera.o: canon.c

libusb.so: $(SO_FILES)
	$(CC) -g -ggdb $(SO_FILES) $(SO_CFLAGS) -fPIC -shared -o libusb.so

clean:
	$(RM) main *.o *.so libgphoto2_port/*.o gphoto2/*.o
