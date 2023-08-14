SO_CFLAGS=-g "-DVCAMERADIR=\"sd\"" $(shell pkg-config --cflags libusb-1.0)
SO_FILES=src/log.o src/libusb.o src/vcamera.o src/data.o libgphoto2_port/gphoto2-port-portability.o

CFLAGS=-I. -Isrc/ -I../lib/ -L. -D FUJI_VUSB -fPIC -D HAVE_LIBEXIF
LDFLAGS=-L. -Wl,-rpath=.

CFLAGS+=-I../camlib/src/ -I../fudge/lib

src/vcamera.o: src/fuji.c src/canon.c

$(SO_FILES): CFLAGS+=$(SO_CFLAGS)

main: main.o libusb.so
	$(CC) main.o -lcamlib $(CFLAGS) -o main $(LDFLAGS) -lusb

tcp: tcp.o $(SO_FILES)
	$(CC) tcp.o $(SO_FILES) $(CFLAGS) -o tcp $(LDFLAGS) -lexif

ap:
	sudo bash scripts/create_ap -n wlp0s20f3 FUJIFILM-X-A2-5DBC

bins:
	xxd -i bin/* > data.h

libusb.so: $(SO_FILES)
	$(CC) -g -ggdb $(SO_FILES) $(SO_CFLAGS) -fPIC -shared -o libusb.so

clean:
	$(RM) main *.o *.so libgphoto2_port/*.o gphoto2/*.o *.out src/*.o tcp
