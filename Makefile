SO_CFLAGS=$(shell pkg-config --cflags libusb-1.0)
SO_FILES=src/log.o src/libusb.o src/vcamera.o src/libgphoto2_port/gphoto2-port-portability.o src/usb2ip.o

CFLAGS=-g -I. -Isrc/ -I../lib/ -L. -fPIC -D HAVE_LIBEXIF "-DVCAMERADIR=\"sd/\""
LDFLAGS=-L. -Wl,-rpath=.

CFLAGS+=-I../camlib/src/ -I../fudge/lib

libusb.so: $(SO_FILES)
	$(CC) -g -ggdb $(SO_FILES) $(SO_CFLAGS) -fPIC -shared -o libusb.so

tcp-fuji: CFLAGS+=-D FUJI_VUSB
tcp-fuji: SO_FILES+=src/fuji.o
tcp-fuji: src/tcp-fuji.o $(SO_FILES)
	$(CC) src/tcp-fuji.o $(SO_FILES) $(CFLAGS) -o tcp-fuji $(LDFLAGS) -lexif

tcp-ip: CFLAGS+=-D CANON_VUSB
tcp-ip: SO_FILES+=src/data.o
tcp-ip: src/tcp-ip.o src/data.o $(SO_FILES)
	$(CC) src/tcp-ip.o $(SO_FILES) $(CFLAGS) -o tcp-ip $(LDFLAGS) -lexif

src/vcamera.o: src/opcodes.h

$(SO_FILES): CFLAGS+=$(SO_CFLAGS)

test-usb.out: test-usb.o libusb.so
	$(CC) test-usb.o -lcamlib $(CFLAGS) -o main $(LDFLAGS) -lusb

bins:
	xxd -i bin/* > data.h

clean:
	$(RM) main *.o *.so libgphoto2_port/*.o gphoto2/*.o *.out src/*.o tcp libgphoto2_port/*.o
	$(RM) tcp-fuji tcp-ip

# Wireless AP networking Hacks

setup-fuji:
	sudo ip link add fuji_dummy type dummy
	sudo ip address add 10.0.0.1/24 dev fuji_dummy
	sudo ip address add 192.168.0.1/24 dev fuji_dummy
	sudo ip address add 200.201.202.203/24 dev fuji_dummy
	ip a

kill-fuji:
	sudo ip link delete fuji_dummy

ap-fuji:
	sudo bash scripts/create_ap wlp0s20f3 fuji_dummy FUJIFILM-X-T20-ABCD

setup-canon:
	sudo ip link add canon_dummy type dummy
	#sudo ip address add 192.168.1.10/24 dev canon_dummy
	sudo ip link set dev canon_dummy address '00:BB:C1:85:9F:AB'
	sudo ip link set canon_dummy up
	sudo ip route add 192.168.1.2 dev canon_dummy
	sudo ip address add 192.168.1.10/24 brd + dev canon_dummy noprefixroute
	ip a

ap-canon:
	sudo bash scripts/create_ap wlp0s20f3 canon_dummy 'EOST6{-464_Canon0A' zzzzzzzz -g 192.168.1.2 --ieee80211n

kill-canon:
	sudo ip link delete canon_dummy
