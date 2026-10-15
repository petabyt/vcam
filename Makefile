-include config.mak

all:
	cmake -G Ninja -B build
	cmake --build build
clean:
	rm -rf build

autosave:
	build/vcam fuji_x_t4 tcp --discovery

# WiFi hardware for spoofing (requires AP support)
WIFI_DEV ?= $(shell iw dev | awk '$$1=="Interface"{print $$2}')

FUJI_SSID_ID := ABCD

ifdef model
  ifeq ($(model),fuji_x_h1)
    SSID := FUJIFILM-XH1-$(FUJI_SSID_ID)
    ap: ap-fuji
  else ifeq ($(model),fuji_x30)
    SSID := FUJIFILM-XT30-$(FUJI_SSID_ID)
    ap: ap-fuji
  else ifeq ($(model),canon_1300d)
    SSID := 'EOST6{-464_Canon0A'
    ap: ap-canon
  endif
endif

local-ip:
	build/vcam fuji_x_t4 tcp --local-ip --fs ~/Pictures/fuji_sd2/

SSID ?= FUJIFILM-X30-ABCD
PASSWORD ?= 

setup-fuji:
	-sudo ip link add fuji_dummy type dummy
	-sudo ip address add 10.0.0.1/24 dev fuji_dummy
	-sudo ip address add 192.168.0.1/24 dev fuji_dummy
	-sudo ip address add 200.201.202.203/24 dev fuji_dummy
	-ip a
ap-fuji: setup-fuji
	sudo bash scripts/create_ap $(WIFI_DEV) fuji_dummy $(SSID) $(PASSWORD)

setup-canon:
	ip link add canon_dummy type dummy
	ip link set dev canon_dummy address '00:BB:C1:85:9F:AB'
	ip link set canon_dummy up
	ip route add 192.168.1.2 dev canon_dummy
	ip address add 192.168.1.10/24 brd + dev canon_dummy noprefixroute
	ip a
ap-canon: setup-canon
	sudo bash scripts/create_ap $(WIFI_DEV) canon_dummy 'EOST6{-464_Canon0A' 12345678 -g 192.168.1.2

reset:
	-sudo ip link delete canon_dummy
	-sudo ip link delete fuji_dummy

gfs:
	-mkdir /dev/gadget
	mount -t gadgetfs gadgetfs /dev/gadget
