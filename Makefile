-include config.mak

all: libusb-vcam.so vcam

include pi.mak

# WiFi hardware for spoofing (requires AP support)
WIFI_DEV ?= wlp0s20f3

VCAM_CORE += src/log.o src/vcamera.o src/pack.o src/packet.o src/ops.o src/canon.o src/fuji.o src/fuji_server.o src/ptpip.o
VCAM_CORE += src/canon_props.o src/data.o src/props.o src/fujissdp.o src/socket.o src/fuji_usb.o src/fuji_fs.o src/usbthing.o
VCAM_CORE += usb/device.o usb/usbstring.o usb/vhci.o

# include libusb-1.0 headers for .so
SO_CFLAGS := $(shell pkg-config --cflags libusb-1.0)
SO_FILES := $(VCAM_CORE) usb/libusb.o

VCAM_FILES := $(VCAM_CORE) src/main.o
VCAM_OTG_FILES := $(VCAM_CORE) src/otg.o

CFLAGS += -g -I. -Isrc/ -Iusb/ -L. -D HAVE_LIBEXIF -Wall -fPIC -Wall -Wshadow -Wcast-qual -Wpedantic -Werror=incompatible-pointer-types -Wstrict-aliasing=3
LDFLAGS += -L. -Wl,-rpath=.

# Used to access bin/
CFLAGS += '-D PWD="$(shell pwd)"'

# Is this actually needed by GCC?
$(SO_FILES): CFLAGS += $(SO_CFLAGS)

# generic libusb.so Canon EOS Device
libusb-vcam.so: $(SO_FILES)
	$(CC) -g -ggdb $(SO_FILES) $(SO_CFLAGS) -lexif -shared -o libusb-vcam.so

vcam: $(VCAM_FILES)
	$(CC)  -g -ggdb $(VCAM_FILES) $(CFLAGS) -o vcam $(LDFLAGS) -lexif 

install: vcam libusb-vcam.so
	sudo cp vcam /usr/bin/
	sudo cp libusb-vcam.so /usr/lib/

-include src/*.d
-include usb/*.d
%.o: %.c $(H)
	$(CC) -MMD -c $< $(CFLAGS) -o $@

%.o: %.cpp $(H)
	g++ -MMD -c $< $(CFLAGS) -o $@

clean:
	$(RM) *.so *.out fuji canon vcam vcam-otg vcam2 temp
	$(RM) $(VCAM_CORE:.o=.d) $(SO_FILES:.o=.d) $(VCAM_CORE) $(SO_FILES)

# Wireless AP networking Hacks

SSID ?= FUJIFILM-X30-ABCD

setup-fuji:
	-sudo ip link add fuji_dummy type dummy
	-sudo ip address add 10.0.0.1/24 dev fuji_dummy
	-sudo ip address add 192.168.0.1/24 dev fuji_dummy
	-sudo ip address add 200.201.202.203/24 dev fuji_dummy
	-ip a
kill-fuji:
	sudo ip link delete fuji_dummy
ap-fuji: setup-fuji
	sudo bash scripts/create_ap $(WIFI_DEV) fuji_dummy $(SSID) $(PASSWORD)
test-fuji:
	@while make vcam; do \
	echo '------------------------------------------'; \
	./vcam fuji_x_a2 --local; \
	done

setup-canon:
	-sudo ip link add canon_dummy type dummy
	-sudo ip link set dev canon_dummy address '00:BB:C1:85:9F:AB'
	-sudo ip link set canon_dummy up
	-sudo ip route add 192.168.1.2 dev canon_dummy
	-sudo ip address add 192.168.1.10/24 brd + dev canon_dummy noprefixroute
	-ip a
ap-canon: setup-canon
	sudo bash scripts/create_ap $(WIFI_DEV) canon_dummy 'EOST6{-464_Canon0A' $(PASSWORD) -g 192.168.1.2 --ieee80211n
kill-canon:
	sudo ip link delete canon_dummy
test-canon:
	@while make vcam; do \
	echo '------------------------------------------'; \
	./vcam canon_1300d; \
	done

gfs:
	-mkdir /dev/gadget
	mount -t gadgetfs gadgetfs /dev/gadget
