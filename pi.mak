# For Fuji only for now
-include ~/.config/secret.mak
# Can recognize TARGET=pi2
ifeq ($(findstring pi,$(TARGET)),pi)

CC := arm-linux-gnueabi-gcc
CPP := arm-linux-gnueabi-g++

LDFLAGS := $(shell arm-linux-gnueabihf-pkg-config --libs libexif) -lpthread -lm
LDFLAGS += -Wl,--dynamic-linker=/lib/ld-linux-armhf.so.3 # interpreter hack

BINARY := vcam

all: $(BINARY)

ARG ?= fuji_x_h1

AP_PASS ?= 
SSID ?= FUJIFILM-X-H1-ABCD

ifdef DEVICE_PASSWORD
LOGIN := sshpass -p "$(DEVICE_PASSWORD)"
endif

sendbin: $(BINARY)
	@$(LOGIN) scp -P $(DEVICE_PORT) $(BINARY) $(DEVICE_USERNAME)@$(DEVICE_IP):/home/$(DEVICE_USERNAME)/vcam

run: sendbin
	@$(LOGIN) sshpass -p "$(DEVICE_PASSWORD)" ssh -t $(DEVICE_USERNAME)@$(DEVICE_IP) -p $(DEVICE_PORT) "cd vcam; ./$(BINARY) $(ARG) --fs /home/pi/fuji_sd"

ap:
	@-$(LOGIN) ssh -t $(DEVICE_USERNAME)@$(DEVICE_IP) -p $(DEVICE_PORT) "cd vcam; make setup-fuji"
	@$(LOGIN) ssh -t $(DEVICE_USERNAME)@$(DEVICE_IP) -p $(DEVICE_PORT) "cd vcam; make ap-fuji SSID=$(SSID) WIFI_DEV=ap0"

ssh:
	@-$(LOGIN) ssh $(DEVICE_USERNAME)@$(DEVICE_IP) -p $(DEVICE_PORT)
.PHONY: ssh ap run sendbin
endif

pidiscovery:
	make TARGET=pi ARG="fuji_x_h1 --discovery" run
pilocal:
	make TARGET=pi ARG="fuji_x_h1 --local-ip" run
piap:
	make TARGET=pi ap
pirun:
	make TARGET=pi ARG="fuji_x_h1" run

.PHONY: pidiscovery pilocal piap pirun
