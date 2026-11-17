/* Test harness restricted to the Bumble VHCI controller and camera. */
#include <stdio.h>
#include <string.h>
#include <dbus/dbus.h>
#include <runtime.h>
#include <bluetooth.h>
#include "module.h"

#define AGENT_PATH "/org/vcam/test_agent"
#define CAMERA_SUFFIX "/dev_F6_F7_F8_F9_FA_FB"

/* Do not print libfuji's manufacturer token. */
void pak_debug_log(struct PakModule *mod, const char *fmt, ...) { (void)mod; (void)fmt; }
int pak_rt_add_user_instruction(struct PakModule *mod, const char *message) { (void)mod; fprintf(stderr, "%s\n", message); return 0; }

static DBusHandlerResult agent_call(DBusConnection *bus, DBusMessage *msg, void *data)
{
	(void)data;
	const char *method = dbus_message_get_member(msg);
	if (!dbus_message_is_method_call(msg, "org.bluez.Agent1", method))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	int allow = !strcmp(method, "Release") || !strcmp(method, "Cancel");
	if (!strcmp(method, "RequestConfirmation") || !strcmp(method, "RequestAuthorization")) {
		const char *path = NULL;
		DBusMessageIter iter;
		if (dbus_message_iter_init(msg, &iter) && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH)
			dbus_message_iter_get_basic(&iter, &path);
		size_t n = path ? strlen(path) : 0;
		size_t suffix = strlen(CAMERA_SUFFIX);
		allow = n >= suffix && !strcmp(path + n - suffix, CAMERA_SUFFIX);
		if (allow) puts("Virtual test: automatically confirming pairing");
	}
	DBusMessage *reply = allow ? dbus_message_new_method_return(msg) :
		dbus_message_new_error(msg, "org.bluez.Error.Rejected", "Not the virtual test camera");
	if (reply) {
		dbus_connection_send(bus, reply, NULL);
		dbus_message_unref(reply);
	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

static int register_agent(void)
{
	DBusError error;
	dbus_error_init(&error);
	DBusConnection *bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (!bus) return -1;
	static DBusObjectPathVTable vtable = {.message_function = agent_call};
	if (!dbus_connection_register_object_path(bus, AGENT_PATH, &vtable, NULL)) return -1;
	DBusMessage *call = dbus_message_new_method_call("org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent");
	const char *path = AGENT_PATH, *capability = "DisplayYesNo";
	dbus_message_append_args(call, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_STRING, &capability, DBUS_TYPE_INVALID);
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(bus, call, 10000, &error);
	dbus_message_unref(call);
	if (!reply) {
		fprintf(stderr, "RegisterAgent: %s\n", error.message ? error.message : "failed");
		dbus_error_free(&error);
		return -1;
	}
	dbus_message_unref(reply);
	return 0;
}

int main(int argc, char **argv)
{
	int secure = argc == 2 && !strcmp(argv[1], "--secure");
	if (argc > 2 || (argc == 2 && !secure)) return 2;
	struct PakBt *ctx = pak_bt_get_context();
	struct PakBtAdapter *adapter = NULL;
	struct PakBtDevice *dev = NULL;
	int count = pak_bt_get_n_adapters(ctx);
	for (int i = 0; i < count; i++) {
		struct PakBtAdapter *a = pak_bt_get_adapter(ctx, i);
		if (a && !strcasecmp(a->address, "02:56:43:41:4D:01")) { adapter = a; break; }
		if (a) pak_bt_unref_adapter(ctx, a);
	}
	if (!adapter) { fprintf(stderr, "virtual adapter missing\n"); return 1; }
	int n = pak_bt_get_n_devices(ctx, adapter, 0);
	for (int i = 0; i < n; i++) {
		struct PakBtDevice *d = pak_bt_get_device(ctx, adapter, i, 0);
		if (d && !strcasecmp(d->mac_address, "F6:F7:F8:F9:FA:FB")) { dev = d; break; }
		if (d) pak_bt_unref_device(ctx, d);
	}
	if (!dev) { fprintf(stderr, "emulator not found, scan first\n"); return 1; }
	if (secure && register_agent()) { fprintf(stderr, "failed to register test agent\n"); return 1; }
	struct PakModule mod = {0};
	struct ModulePriv priv = {0};
	mod.bt = ctx; mod.priv = &priv; priv.mod = &mod;
	int rc = fuji_connect_bluetooth(&mod, ctx, dev, NULL);
	printf("fuji_connect_bluetooth returned %d\n", rc);
	pak_bt_device_disconnect(ctx, dev);
	pak_bt_unref_device(ctx, dev);
	pak_bt_unref_adapter(ctx, adapter);
	return rc ? 1 : 0;
}
