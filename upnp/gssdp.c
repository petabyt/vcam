/* 
 * Copyright (C) 2006 OpenedHand Ltd.
 *
 * Author: Jorn Baayen <jorn@openedhand.com>
 *
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Modified by Daniel C to respond to EOS Cameras
 */

#include <libgssdp/gssdp.h>
#include <gio/gio.h>
#include <stdlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 49152
#define BUFFER_SIZE 1024

void send_response(int client_socket) {
    const char *payload = "<?xml version=\"1.0\"?>\n"
    "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
    "  <specVersion>\n"
    "    <major>1</major>\n"
    "    <minor>0</minor>\n"
    "  </specVersion>\n"
    "  <URLBase>http://192.168.1.2:49152/upnp/</URLBase>\n"
    "  <device>\n"
    "    <deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>\n"
    "    <friendlyName>Canon EOS Rebel T6</friendlyName>\n"
    "    <manufacturer>Canon</manufacturer>\n"
    "    <manufacturerURL>http://www.canon.com/</manufacturerURL>\n"
    "    <modelDescription>Canon Digital Camera</modelDescription>\n"
    "    <modelName>Canon EOS vcam</modelName>\n"
    "    <serialNumber>172073046108</serialNumber>\n"
    "    <UDN>uuid:00000000-0000-0000-0001-00BBC1859FAB</UDN>\n"
    "    <serviceList>\n"
    "      <service>\n"
    "        <serviceType>urn:schemas-canon-com:service:ICPO-SmartPhoneEOSSystemService:1</serviceType>\n"
    "        <serviceId>urn:schemas-canon-com:serviceId:ICPO-SmartPhoneEOSSystemService-1</serviceId>\n"
    "        <SCPDURL>CameraSvcDesc.xml</SCPDURL>\n"
    "        <controlURL>control/CanonCamera/</controlURL>\n"
    "        <eventSubURL></eventSubURL>\n"
    "        <ns:X_targetId xmlns:ns=\"urn:schemas-canon-com:schema-upnp\">uuid:00000000-0000-0000-0001-FFFFFFFFFFFF</ns:X_targetId>\n"
    "        <ns:X_onService xmlns:ns=\"urn:schemas-canon-com:schema-upnp\">0</ns:X_onService>\n"
    "        <ns:X_deviceUsbId xmlns:ns=\"urn:schemas-canon-com:schema-upnp\">32b4</ns:X_deviceUsbId>\n"
    "        <ns:X_deviceNickname xmlns:ns=\"urn:schemas-canon-com:schema-upnp\">Fake Nickname</ns:X_deviceNickname>\n"
    "      </service>\n"
    "    </serviceList>\n"
    "    <presentationURL>/</presentationURL>\n"
    "  </device>\n"
    "</root>";

   	const char *response_fmt =
	    "HTTP/1.1 200 OK\r\n"
	    "Content-Length: %d\r\n"
	    "Connection: close\r\n"
	    "Content-Type: text/xml\r\n\r\n"
	    "%s";

	char *buffer = malloc(strlen(payload) + 4096);
	int length = sprintf(buffer, response_fmt, strlen(payload), payload);

	send(client_socket, buffer, length, 0);
}

void *start_upnp_server(void *arg) {
	int server_socket, client_socket;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	// Create a socket
	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket < 0) {
		perror("upnp: Socket creation failed");
		exit(EXIT_FAILURE);
	}

	// Set up the server address struct
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.s_addr = INADDR_ANY;

	// Bind the server socket to the specified port
	if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("upnp: Binding failed");
		exit(EXIT_FAILURE);
	}

	// Listen for incoming connections
	if (listen(server_socket, 5) < 0) {
		perror("upnp: Listening failed");
		exit(EXIT_FAILURE);
	}

	printf("Server is listening on port %d...\n", PORT);

	while (1) {
		// Accept a client connection
		client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
		if (client_socket < 0) {
			perror("upnp: Accepting connection failed");
			continue;
		}

		char buffer[1024];
		int rc = recv(client_socket, buffer, sizeof(buffer), 0);
		printf("upnp server: %d, %s\n", rc, buffer);

		printf("upnp server: Connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		// Send the response
		send_response(client_socket);
		printf("upnp server: Sent response\n");

		close(client_socket);
	}

	close(server_socket);

	return 0;
}

static void
resource_available_cb (G_GNUC_UNUSED GSSDPResourceBrowser *resource_browser,
			   const char			 *usn,
			   GList				  *locations)
{
	GList *l;

	g_print ("resource available\n"
		 "  USN:	  %s\n",
		 usn);
	
	for (l = locations; l; l = l->next) {
		g_print ("  Location: %s\n", (char *) l->data);
	}
}

static void
resource_unavailable_cb (G_GNUC_UNUSED GSSDPResourceBrowser *resource_browser,
			 const char			 *usn)
{
	g_print ("resource unavailable\n"
		 "  USN:	  %s\n",
		 usn);
}

int main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char **argv) {
	GSSDPResourceBrowser *resource_browser;
	GError *error;
	GMainLoop *main_loop;
	GSSDPClient *client;

	pthread_t thread;
	if (pthread_create(&thread, NULL, start_upnp_server, NULL) != 0) {
		perror("Thread creation failed");
		return 1;
	}

	error = NULL;
	client = gssdp_client_new(NULL, NULL);
	if (error) {
		g_printerr ("Error creating the GSSDP client: %s\n", error->message);
		g_error_free (error);
		return EXIT_FAILURE;
	}

	resource_browser = gssdp_resource_browser_new (client, GSSDP_ALL_RESOURCES);

	g_signal_connect (resource_browser,
			  "resource-available",
			  G_CALLBACK (resource_available_cb),
			  NULL);
	g_signal_connect (resource_browser,
			  "resource-unavailable",
			  G_CALLBACK (resource_unavailable_cb),
			  NULL);

	gssdp_resource_browser_set_active (resource_browser, TRUE);

	GSSDPResourceGroup *resource_group;
	resource_group = gssdp_resource_group_new (client);
	gssdp_resource_group_add_resource_simple
		(resource_group,
		 "urn:schemas-canon-com:service:ICPO-SmartPhoneEOSSystemService:1",
		 "uuid:00000000-0000-0000-0001-00BBC1859FAB::urn:schemas-canon-com:service:ICPO-SmartPhoneEOSSystemService:1",
		 "http://192.168.1.2:49152/upnp/CameraDevDesc.xml");
	gssdp_resource_group_set_available (resource_group, TRUE);


	main_loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (main_loop);
	g_main_loop_unref (main_loop);

	g_object_unref (resource_browser);
	g_object_unref (client);

	return EXIT_SUCCESS;
}
