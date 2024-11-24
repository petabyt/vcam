#include <libudev.h>
static int scan_udev() {
const char *USBIP_VHCI_BUS_TYPE = "platform";
const char *USBIP_VHCI_DEVICE_NAME = "vhci_hcd.0";
	struct udev *udev_context = udev_new();
	struct udev_device *hc_device = udev_device_new_from_subsystem_sysname(udev_context, USBIP_VHCI_BUS_TYPE, USBIP_VHCI_DEVICE_NAME);

	if (hc_device == NULL) return -1;

	const char *nports = udev_device_get_sysattr_value(hc_device, "nports");
	printf("nports: %s\n", nports);

	const char *path = udev_device_get_syspath(hc_device);
	const char *status = udev_device_get_sysattr_value(hc_device, "status");
	printf("sts: %s\n", status);

	struct udev_device *platform = udev_device_get_parent(hc_device);
	printf("%s\n", udev_device_get_syspath(platform));

	udev_unref(udev_context);
	return 0;
}
