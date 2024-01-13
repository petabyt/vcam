# TODO: depend on config.mak rather than make CLI flags
-include config.mak

# WiFi hardware for spoofing (requires AP support)
WIFI_DEV?=wlp0s20f3

# Set this to a folder with images (as it appears to PTP)
VCAMERADIR?=$(HOME)/Documents/fuji_sd/

ifeq ($(wildcard $(VCAMERADIR).*),)
$(info Directory '$(VCAMERADIR)' not found)
VCAMERADIR=$(PWD)/sd
$(info Using '$(VCAMERADIR)')
endif

VCAM_CORE:=src/log.o src/vcamera.o src/gphoto.o src/packet.o src/ops.o src/canon.o src/fuji.o src/tcp-fuji.o src/tcp-ip.o
VCAM_CORE+=src/canon_setup.o src/data.o

SO_CFLAGS=$(shell pkg-config --cflags libusb-1.0)
SO_FILES=$(VCAM_CORE) src/libusb.o

VCAM_FILES=$(VCAM_CORE) src/main.o

CFLAGS=-g -I. -Isrc/ -I../lib/ -L. -fPIC -D HAVE_LIBEXIF
LDFLAGS=-L. -Wl,-rpath=.
CFLAGS+='-D VCAMERADIR="$(VCAMERADIR)"'
CFLAGS+='-D PWD="$(shell pwd)"'

$(SO_FILES): CFLAGS+=$(SO_CFLAGS)

# generic libusb.so Canon EOS Device
libusb.so: $(SO_FILES)
	$(CC) -g -ggdb $(SO_FILES) $(SO_CFLAGS) -fPIC -lexif -shared -o libusb.so

vcam: $(VCAM_FILES)
	$(CC) $(VCAM_FILES) $(CFLAGS) -o vcam $(LDFLAGS) -lexif

-include src/*.d
%.o: %.c $(H)
	$(CC) -MMD -c $< $(CFLAGS) -o $@

clean:
	$(RM) main *.o *.so libgphoto2_port/*.o gphoto2/*.o *.out src/*.o tcp libgphoto2_port/*.o src/*.d
	$(RM) fuji canon vcam

ln:
	ln ../camlib/src/ptp.h src/ptp.h
	ln ../camlib/src/cl_data.h src/cl_data.h

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
	@while make vcam; do \
	echo '------------------------------------------'; \
	./vcam fuji_x_a2; \
	done

setup-canon:
	sudo ip link add canon_dummy type dummy
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
	@while make vcam; do \
	echo '------------------------------------------'; \
	./vcam canon_1300d; \
	done
