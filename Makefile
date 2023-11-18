# TODO: depend on config.mak rather than make CLI flags
-include config.mak

WIFI_DEV?=wlp0s20f3

# Set this to a folder with images (as it appears to PTP)
VCAMERADIR?=$(HOME)/Documents/fuji_sd/

ifeq ($(wildcard $(VCAMERADIR).*),)
$(info Directory '$(VCAMERADIR)' not found)
VCAMERADIR=$(PWD)/sd
$(info Using '$(VCAMERADIR)')
endif

VCAM_CORE=src/log.o src/vcamera.o src/gphoto-system.o src/packet.o src/ops.o

SO_CFLAGS=$(shell pkg-config --cflags libusb-1.0)
SO_FILES=$(VCAM_CORE) src/libusb.o

CFLAGS=-g -I. -Isrc/ -I../lib/ -L. -fPIC -D HAVE_LIBEXIF
LDFLAGS=-L. -Wl,-rpath=.
CFLAGS+="-D VCAMERADIR=\"$(VCAMERADIR)\""
CFLAGS+=-I../camlib/src/

$(SO_FILES): CFLAGS+=$(SO_CFLAGS)

# generic libusb.so Canon EOS Device
libusb.so: CFLAGS+=-D VCAM_CANON -D CAM_HAS_EXTERN_DEV_INFO
libusb.so: SO_FILES+=src/canon.o
libusb.so: src/canon.o $(SO_FILES)
libusb.so: $(SO_FILES)
	$(CC) -g -ggdb $(SO_FILES) $(SO_CFLAGS) -fPIC -lexif -shared -o libusb.so

FUJI_FILES=$(VCAM_CORE) src/tcp-fuji.o src/fuji.o src/main.o
fuji: CFLAGS+=-D VCAM_FUJI -D CAM_HAS_EXTERN_DEV_INFO
fuji: $(FUJI_FILES)
	$(CC) $(FUJI_FILES) $(CFLAGS) -o fuji $(LDFLAGS) -lexif

canon: CFLAGS+=-D VCAM_CANON -D CAM_HAS_EXTERN_DEV_INFO
CANON_FILES=$(VCAM_CORE) src/tcp-ip.o src/canon.o src/main.o src/fuji.o src/tcp-fuji.o
canon: $(CANON_FILES)
	$(CC) $(CANON_FILES) $(CFLAGS) -o canon $(LDFLAGS) -lexif

vcam: CFLAGS+=-D VCAM_CANON -D CAM_HAS_EXTERN_DEV_INFO
VCAM_FILES=$(VCAM_CORE) src/tcp-ip.o src/canon.o src/main.o src/fuji.o src/tcp-fuji.o
vcam: $(VCAM_FILES)
	$(CC) $(VCAM_FILES) $(CFLAGS) -o vcam $(LDFLAGS) -lexif

# Recompile when headers change
%.o: %.c $(H) $(wildcard src/*.h)
	$(CC) -c $< $(CFLAGS) -o $@

clean:
	$(RM) main *.o *.so libgphoto2_port/*.o gphoto2/*.o *.out src/*.o tcp libgphoto2_port/*.o
	$(RM) fuji canon vcam

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
	sudo bash scripts/create_ap $(WIFI_DEV) fuji_dummy FUJIFILM-X-A2-ABCD
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
