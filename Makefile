SO_CFLAGS=$(shell pkg-config --cflags libusb-1.0)
SO_FILES=src/log.o src/libusb.o src/vcamera.o src/libgphoto2_port/gphoto2-port-portability.o src/fuji.o

CFLAGS=-g -I. -Isrc/ -I../lib/ -L. -D FUJI_VUSB -fPIC -D HAVE_LIBEXIF "-DVCAMERADIR=\"sd/\""
LDFLAGS=-L. -Wl,-rpath=.

CFLAGS+=-I../camlib/src/ -I../fudge/lib

libusb.so: $(SO_FILES)
	$(CC) -g -ggdb $(SO_FILES) $(SO_CFLAGS) -fPIC -shared -o libusb.so

tcp: src/tcp-fuji.o $(SO_FILES)
	$(CC) src/tcp-fuji.o $(SO_FILES) $(CFLAGS) -o tcp $(LDFLAGS) -lexif

src/vcamera.o: src/opcodes.h

$(SO_FILES): CFLAGS+=$(SO_CFLAGS)

test-usb.out: test-usb.o libusb.so
	$(CC) test-usb.o -lcamlib $(CFLAGS) -o main $(LDFLAGS) -lusb

bins:
	xxd -i bin/* > data.h

clean:
	$(RM) main *.o *.so libgphoto2_port/*.o gphoto2/*.o *.out src/*.o tcp libgphoto2_port/*.o

SSID=FUJIFILM-X-T20-ABCD

ap:
	sudo bash scripts/create_ap -n wlp0s20f3 $(SSID)
