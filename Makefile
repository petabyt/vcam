-include config.mak

all:
	cmake -G Ninja -B build
	cmake --build build
clean:
	rm -rf build

autosave:
	build/vcam fuji_x_t4 tcp --discovery

# WiFi: the vcam wifi backend brings up the camera AP (hostapd/dnsmasq as
# supervised children), waits for a phone to associate, then serves PTP/IP.
# The interface carrying your default route is never touched.
#   sudo build/vcam canon_1300d wifi
#   sudo build/vcam fuji_x_h1 wifi
# Dry run without touching anything:
#   build/vcam canon_1300d wifi --check
ap:
	sudo build/vcam $(model) wifi

local-ip:
	build/vcam fuji_x_t4 tcp --local-ip --fs ~/Pictures/fuji_sd2/

reset:
	# the wifi backend cleans up after itself; this only reverts leftovers
	# of a run that was SIGKILLed or lost power
	sudo build/vcam fuji_x_h1 wifi --cleanup

gfs:
	-mkdir /dev/gadget
	mount -t gadgetfs gadgetfs /dev/gadget
