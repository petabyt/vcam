SO_CFLAGS=$(shell pkg-config --cflags libusb-1.0)
SO_FILES=src/log.o src/libusb.o src/vcamera.o src/gphoto-system.o src/packet.o

CFLAGS=-g -I. -Isrc/ -I../lib/ -L. -fPIC -D HAVE_LIBEXIF -D CAM_HAS_EXTERN_DEV_INFO
LDFLAGS=-L. -Wl,-rpath=.

CFLAGS+="-D VCAMERADIR=\"/home/daniel/Documents/fuji_sd/\""

CFLAGS+=-I../camlib/src/ -I../fudge/lib

libusb.so: CFLAGS+=-D CANON_VUSB
libusb.so: SO_FILES+=src/data.o src/canon.o
libusb.so: src/data.o src/canon.o $(SO_FILES)
libusb.so: $(SO_FILES)
	$(CC) -g -ggdb $(SO_FILES) $(SO_CFLAGS) -fPIC -lexif -shared -o libusb.so

$(SO_FILES): CFLAGS+=$(SO_CFLAGS)
src/vcamera.o: src/opcodes.h

tcp-fuji: CFLAGS+=-D FUJI_VUSB
tcp-fuji: SO_FILES+=src/fuji.o src/tcp-fuji.o
tcp-fuji: src/fuji.o src/tcp-fuji.o $(SO_FILES)
	$(CC) $(SO_FILES) $(CFLAGS) -o tcp-fuji $(LDFLAGS) -lexif

ip-canon: CFLAGS+=-D CANON_VUSB
ip-canon: SO_FILES+=src/tcp-ip.o src/data.o src/canon.o
ip-canon: src/tcp-ip.o src/data.o src/canon.o $(SO_FILES)
	$(CC) $(SO_FILES) $(CFLAGS) -o ip-canon $(LDFLAGS) -lexif

clean:
	$(RM) main *.o *.so libgphoto2_port/*.o gphoto2/*.o *.out src/*.o tcp libgphoto2_port/*.o
	$(RM) tcp-fuji ip-canon

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
test-fuji:
	@while true; do \
	echo '------------------------------------------'; \
	make tcp-fuji && ./tcp-fuji; \
	done

setup-canon:
	sudo ip link add canon_dummy type dummy
	#sudo ip address add 192.168.1.2/24 dev canon_dummy # required for local testing
	sudo ip link set dev canon_dummy address '00:BB:C1:85:9F:AB'
	sudo ip link set canon_dummy up
	sudo ip route add 192.168.1.2 dev canon_dummy
	sudo ip address add 192.168.1.10/24 brd + dev canon_dummy noprefixroute
	ip a
ap-canon:
	sudo bash scripts/create_ap wlp0s20f3 canon_dummy 'EOST6{-464_Canon0A' zzzzzzzz -g 192.168.1.2 --ieee80211n
kill-canon:
	sudo ip link delete canon_dummy
test-canon:
	@while true; do \
	echo '------------------------------------------'; \
	make ip-canon && ./ip-canon; \
	done
