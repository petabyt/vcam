-include config.mak

WIFI_DEV?=wlp0s20f3

# Set this to a folder with images (as it appears to PTP)
VCAMERADIR?=$(HOME)/Documents/fuji_sd/

ifeq ($(wildcard $(VCAMERADIR).*),)
$(info Directory '$(VCAMERADIR)' not found)
VCAMERADIR=$(PWD)/sd
$(info Using '$(VCAMERADIR)')
endif

SO_CFLAGS=$(shell pkg-config --cflags libusb-1.0)
SO_FILES=src/log.o src/libusb.o src/vcamera.o src/gphoto-system.o src/packet.o src/ops.o

CFLAGS=-g -I. -Isrc/ -I../lib/ -L. -fPIC -D HAVE_LIBEXIF
LDFLAGS=-L. -Wl,-rpath=.
CFLAGS+="-D VCAMERADIR=\"$(VCAMERADIR)\""
CFLAGS+=-I../camlib/src/ -I../fudge/lib

$(SO_FILES): CFLAGS+=$(SO_CFLAGS)

# generic libusb.so Canon EOS Device
libusb.so: CFLAGS+=-D VCAM_CANON -D CAM_HAS_EXTERN_DEV_INFO
libusb.so: SO_FILES+=src/data.o src/canon.o
libusb.so: src/data.o src/canon.o $(SO_FILES)
libusb.so: $(SO_FILES)
	$(CC) -g -ggdb $(SO_FILES) $(SO_CFLAGS) -fPIC -lexif -shared -o libusb.so

src/vcamera.o: src/opcodes.h

fuji: CFLAGS+=-D VCAM_FUJI -D CAM_HAS_EXTERN_DEV_INFO
fuji: SO_FILES+=src/tcp-fuji.o src/fuji.o
fuji: src/fuji.o src/tcp-fuji.o $(SO_FILES)
	$(CC) $(SO_FILES) $(CFLAGS) -o fuji $(LDFLAGS) -lexif

canon: CFLAGS+=-D VCAM_CANON -D CAM_HAS_EXTERN_DEV_INFO
canon: SO_FILES+=src/tcp-ip.o src/data.o src/canon.o
canon: src/tcp-ip.o src/data.o src/canon.o $(SO_FILES)
	$(CC) $(SO_FILES) $(CFLAGS) -o canon $(LDFLAGS) -lexif

clean:
	$(RM) main *.o *.so libgphoto2_port/*.o gphoto2/*.o *.out src/*.o tcp libgphoto2_port/*.o
	$(RM) fuji canon

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
	sudo bash scripts/create_ap $(WIFI_DEV) fuji_dummy FUJIFILM-X-T20-ABCD
test-fuji:
	@while make fuji; do \
	echo '------------------------------------------'; \
	./fuji; \
	done

setup-canon:
	sudo ip link add canon_dummy type dummy
	#sudo ip address add 192.168.1.2/24 dev canon_dummy # local testing doesn't work without this
	sudo ip link set dev canon_dummy address '00:BB:C1:85:9F:AB'
	sudo ip link set canon_dummy up
	sudo ip route add 192.168.1.2 dev canon_dummy
	sudo ip address add 192.168.1.10/24 brd + dev canon_dummy noprefixroute
	ip a
ap-canon:
	sudo bash scripts/create_ap $(WIFI_DEV) canon_dummy 'EOST6{-464_Canon0A' zzzzzzzz -g 192.168.1.2 --ieee80211n
kill-canon:
	sudo ip link delete canon_dummy
test-canon:
	@while make canon; do \
	echo '------------------------------------------'; \
	./canon; \
	done
