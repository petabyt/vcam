// WiFi backend: camera access-point bring-up and lifecycle management.
// Spawns hostapd/dnsmasq as children (PR_SET_PDEATHSIG - they can never
// outlive vcam), waits for a phone to associate and lease an address, then
// hands over to the vendor TCP backend. All changes are scoped and reversed
// on teardown; the interface carrying the default route is never touched.
#ifndef VCAM_WIFI_H
#define VCAM_WIFI_H

#define WIFI_VENDOR_FUJI 0
#define WIFI_VENDOR_CANON 1

struct vcam_wifi_config {
	/// @brief AP interface. Empty = auto-pick (AP-capable, not the
	/// default-route interface, no station connection)
	char iface[64];
	/// @brief Copy of the model name (generic SSID derivation)
	char model[64];
	/// @brief 2.4 GHz channel (0 = default 6)
	int channel;
	/// @brief Optional regulatory domain to set (global to the kernel!)
	char reg_domain[4];
	/// @brief Seconds to wait for association + lease (-1 = preset
	/// default, 0 = don't wait)
	int wait_timeout;
	/// @brief Print the bring-up plan and exit, touch nothing
	int check_only;
	/// @brief Revert leftovers from a previous run and exit
	int cleanup_only;
	/// @brief Filled in during bring-up: selected interface index
	int ifindex;
};

// Extract wifi-backend flags from argv (compacted in place), fill cfg.
// Returns the new argument count for the vendor init parsers.
// (vcam_wifi_run is declared in vcam.h - it needs the vcam type)
int vcam_wifi_filter_args(int argc, char **argv, struct vcam_wifi_config *cfg);

#endif
