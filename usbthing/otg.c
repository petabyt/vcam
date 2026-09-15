#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadgetfs.h>
#include <poll.h>
#include <signal.h>
#include "usbthing.h"

#define MTP_REQ_CANCEL              0x64
#define MTP_REQ_GET_EXT_EVENT_DATA  0x65
#define MTP_REQ_RESET               0x66
#define MTP_REQ_GET_DEVICE_STATUS   0x67

#define FETCH(_var_)                            \
    memcpy(cp, &_var_, _var_.bLength);          \
    cp += _var_.bLength;

#define USB_DEV "/dev/gadget/20980000.usb"
#define USB_EPIN "/dev/gadget/ep1in"
#define USB_EPOUT "/dev/gadget/ep2out"
#define USB_EPINT "/dev/gadget/ep2in"

enum {
    STRINGID_MANUFACTURER = 1,
    STRINGID_PRODUCT,
    STRINGID_SERIAL,
    STRINGID_CONFIG_HS,
    STRINGID_CONFIG_LS,
    STRINGID_INTERFACE,
    STRINGID_MAX
};

struct io_thread_args {
    unsigned stop;
    int fd_in, fd_out, fd_int;
};

static struct io_thread_args thread_args;
pthread_t thread_ctx;

#define CONFIG_VALUE 1
static struct usb_string stringtab [] = {
    { STRINGID_MANUFACTURER, "Canon Inc.", },
    { STRINGID_PRODUCT,      "Canon Digital Camera", },
    { STRINGID_SERIAL,       "12345678", },
    { STRINGID_CONFIG_HS,    "High speed configuration", },
    { STRINGID_CONFIG_LS,    "Low speed configuration", },
    { STRINGID_INTERFACE,    "MTP", },
    { STRINGID_MAX, NULL},
};

static struct usb_gadget_strings strings = {
    .language = 0x0409, /* en-us */
    .strings = stringtab,
};

static struct usb_endpoint_descriptor ep_descriptor_in;
static struct usb_endpoint_descriptor ep_descriptor_out;
static struct usb_endpoint_descriptor ep_descriptor_int;

struct _GPPortPrivateLibrary {
	int isopen;
	vcam *vcamera;
};
GPPort *priv_gpport;

static int start_vcam(int argc, char **argv) {
	struct CamConfig *conf = vcam_new_config(argc, argv);

	GPPort *port = malloc(sizeof(GPPort));
	C_MEM(port->pl = calloc(1, sizeof(GPPortPrivateLibrary)));
	port->pl->vcamera = vcam_init_standard(CAM_CANON);
	port->pl->vcamera->conf = conf;

	priv_gpport = port;

	if (port->pl->isopen)
		return -1;

	return 0;
}

static void* io_thread(void* arg)
{
    struct io_thread_args* thread_args = (struct io_thread_args*)arg;
    fd_set read_set, write_set;
    struct timeval timeout;
    int ret, max_read_fd, max_write_fd;
    char buffer[512];

    max_read_fd = max_write_fd = 0;

    if (thread_args->fd_in > max_write_fd) max_write_fd = thread_args->fd_in;
    if (thread_args->fd_out > max_read_fd) max_read_fd  = thread_args->fd_out;

    FD_ZERO(&read_set);
    FD_SET(thread_args->fd_out, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000; // 10ms

    ret = select(max_read_fd + 1, &read_set, NULL, NULL, &timeout);

    FD_ZERO(&write_set);
    FD_SET(thread_args->fd_in, &write_set);
    ret = select(max_write_fd + 1, NULL, &write_set, NULL, NULL);

    while (!thread_args->stop) {
		printf("Waiting on read...\n");
        ret = read (thread_args->fd_out, buffer, sizeof(buffer));

        if (ret > 0)
            printf("otg: read %d bytes\n", ret);
        else
            printf("otg: read error: %d\n", ret);

		ret = vcam_write(priv_gpport->pl->vcamera, 0x2, (unsigned char *)buffer, ret);
		if (ret < 0) {
			vcam_log("read error: %d\n", ret);
			break;
		}

		//fcntl(thread_args->fd_in, F_SETFL, fcntl(thread_args->fd_in, F_GETFL) | O_NONBLOCK);

		struct pollfd pfd;
		pfd.fd = thread_args->fd_in;
		pfd.events = POLLOUT;

		//ret = poll(&pfd, 1, 2000);

		vcam_log("%d bytes in queue\n", priv_gpport->pl->vcamera->nrinbulk);

		struct PtpBulkContainer *first = (struct PtpBulkContainer *)priv_gpport->pl->vcamera->inbulk;

		ret = write (thread_args->fd_in, first, first->length);
		printf("otg: Was able to write %d bytes\n", ret);

		if (first->type == PTP_PACKET_TYPE_DATA) {
			first = (struct PtpBulkContainer *)(priv_gpport->pl->vcamera->inbulk + first->length);
			ret = write(thread_args->fd_in, first, first->length);
			printf("otg: Was able to write %d bytes\n", ret);
		}

		priv_gpport->pl->vcamera->nrinbulk = 0;

		//fcntl(thread_args->fd_in, F_SETFL, fcntl(thread_args->fd_in, F_GETFL) & ~O_NONBLOCK);
		fflush(stdout);
    }

    close (thread_args->fd_in);
    close (thread_args->fd_out);

    thread_args->fd_in = -1;
    thread_args->fd_out = -1;

    return NULL;
}

static int init_ep(int* fd_in, int* fd_out, int *fd_int)
{
    uint8_t init_config[2048];
    uint8_t* cp;
    int ret = -1;
    uint32_t send_size;

	{
		// Configure ep1 (low/full speed + high speed)
		*fd_in = open(USB_EPIN, O_RDWR);

		if (*fd_in <= 0)
		{
		    printf("Unable to open %s (%m)\n", USB_EPIN);
		    goto end;
		}

		*(uint32_t*)init_config = 1;
		cp = &init_config[4];

		FETCH(ep_descriptor_in);
		FETCH(ep_descriptor_in);

		send_size = (uint32_t)cp - (uint32_t)init_config;
		ret = write(*fd_in, init_config, send_size);

		if (ret != send_size)
		{
		    printf("Write error %d (%m)\n", ret);
		    goto end;
		}

		printf("ep1 configured\n");
	}

	{
		// Configure ep2 (low/full speed + high speed)
		*fd_out = open(USB_EPOUT, O_RDWR);

		if (*fd_out <= 0)
		{
		    printf("Unable to open %s (%m)\n", USB_EPOUT);
		    goto end;
		}

		*(uint32_t*)init_config = 1;
		cp = &init_config[4];

		FETCH(ep_descriptor_out);
		FETCH(ep_descriptor_out);

		send_size = (uint32_t)cp - (uint32_t)init_config;
		ret = write(*fd_out, init_config, send_size);

		if (ret != send_size)
		{
		    printf("Write error %d (%m)\n", ret);
		    goto end;
		}

		printf("ep2 configured\n");
	}

	{
		// Configure ep3 (low/full speed + high speed)
		*fd_int = open(USB_EPINT, O_RDWR);

		if (*fd_int <= 0)
		{
		    printf("Unable to open %s (%m)\n", USB_EPINT);
		    goto end;
		}

		*(uint32_t*)init_config = 1;
		cp = &init_config[4];

		FETCH(ep_descriptor_int);
		FETCH(ep_descriptor_int);

		send_size = (uint32_t)cp-(uint32_t)init_config;
		ret = write(*fd_int, init_config, send_size);

		if (ret != send_size)
		{
		    printf("Write error %d (%m)\n", ret);
		    goto end;
		}

		printf("ep3 configured\n");
	}

    ret = 0;

end:
    return ret;
}

static void handle_setup_request(int fd, struct usb_ctrlrequest* setup)
{
    int status;
    uint8_t buffer[512];

    printf("Setup request %d\n", setup->bRequest);

    switch (setup->bRequest)
    {
    case USB_REQ_GET_DESCRIPTOR:
        if (setup->bRequestType != USB_DIR_IN)
            goto stall;
        switch (setup->wValue >> 8)
        {
            case USB_DT_STRING:
                printf("Get string id #%d (max length %d)\n", setup->wValue & 0xff,
                    setup->wLength);
                status = usb_gadget_get_string (&strings, setup->wValue & 0xff, buffer);
                // Error 
                if (status < 0)
                {
                    printf("String not found !!\n");
                    break;
                }
                else
                {
                    printf("Found %d bytes\n", status);
                }
                write (fd, buffer, status);
                return;
        default:
            printf("Cannot return descriptor %d\n", (setup->wValue >> 8));
        }
        break;
    case USB_REQ_SET_CONFIGURATION:
        if (setup->bRequestType != USB_DIR_OUT)
        {
            printf("Bad dir\n");
            goto stall;
        }
        switch (setup->wValue) {
        case CONFIG_VALUE:
            printf("Set config value\n");
            if (!thread_args.stop)
            {
                thread_args.stop = 1;
                usleep(200000); // Wait for termination
            }
            if (thread_args.fd_in <= 0)
            {
                status = init_ep (&thread_args.fd_in, &thread_args.fd_out, &thread_args.fd_int);
            }
            else
                status = 0;
            if (!status)
            {
                thread_args.stop = 0;
                pthread_create(&thread_ctx, NULL, io_thread, &thread_args);
            }
            break;
        case 0:
            printf("Disable threads\n");
            thread_args.stop = 1;
            break;
        default:
            printf("Unhandled configuration value %d\n", setup->wValue);
            break;
        }        
        // Just ACK
        status = read (fd, &status, 0);
        return;
    case USB_REQ_GET_INTERFACE:
        printf("GET_INTERFACE\n");
        buffer[0] = 0;
        write (fd, buffer, 1);
        return;
    case USB_REQ_SET_INTERFACE:
        printf("SET_INTERFACE\n");
        ioctl (thread_args.fd_in, GADGETFS_CLEAR_HALT);
        ioctl (thread_args.fd_out, GADGETFS_CLEAR_HALT);
        ioctl (thread_args.fd_int, GADGETFS_CLEAR_HALT);
        // ACK
        status = read (fd, &status, 0);
        return;
	case MTP_REQ_CANCEL:
		printf("MTP_REQ_CANCEL\n");
		return;
	default:
		printf("Unknown setup request\n");
    }

stall:
    printf("Stalled\n");
    // Error
    if (setup->bRequestType & USB_DIR_IN)
        read (fd, &status, 0);
    else
        write (fd, &status, 0);
}

static void handle_ep0(int fd)
{
    int ret, nevents, i;
    fd_set read_set;
    struct usb_gadgetfs_event events[5];

    while (1)
    {
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);

        select(fd+1, &read_set, NULL, NULL, NULL);

        ret = read(fd, &events, sizeof(events));

        if (ret < 0)
        {
            printf("Read error %d (%m)\n", ret);
            goto end;        
        }

        nevents = ret / sizeof(events[0]);

        printf("%d event(s)\n", nevents);

        for (i=0; i<nevents; i++)
        {
            switch (events[i].type)
            {
            case GADGETFS_CONNECT:
                printf("EP0 CONNECT\n");
                break;
            case GADGETFS_DISCONNECT:
                printf("EP0 DISCONNECT\n");
                break;
            case GADGETFS_SETUP:
                printf("EP0 SETUP\n");
                handle_setup_request(fd, &events[i].u.setup);
                break;
            case GADGETFS_NOP:
            case GADGETFS_SUSPEND:
                break;
            }
        }
    }

end:
    return;
}

static int gadget_fd;

void kill_sig(int x) {
	fprintf(stderr, "Killing...\n");
	pthread_kill(thread_ctx, SIGUSR1);
	usleep(500);
    close (thread_args.fd_in);
    close (thread_args.fd_out);
	close(gadget_fd);
	exit(0);
}

void *kill_thread(void *arg) {
	while (1) {
		int ch = fgetc(stdin);
		kill_sig(1);
	}
}

int usbt_gadgetfs_init(struct UsbThing *ctx) {

	signal(SIGINT, kill_sig);
	pthread_t killtd;
	pthread_create(&killtd, NULL, kill_thread, NULL);

	int fd = -1, ret, err = -1;
	struct usb_config_descriptor config;
	struct usb_config_descriptor config_hs;
	struct usb_device_descriptor device_descriptor;
	struct usb_interface_descriptor if_descriptor;
	uint8_t init_config[2048];

	int devn = 0;

	fd = open(USB_DEV, O_RDWR | O_SYNC);
	gadget_fd = fd;

	if (fd <= 0)
	{
		printf("Unable to open %s (%m)\n", USB_DEV);
		return 1;
	}

	int of = 0;
	*(uint32_t *)init_config = 0;
	of += 4;

	of += ctx->get_total_config_descriptor(ctx, devn, 0, init_config + of);

	ctx->get_device_descriptor(ctx, devn, &device_descriptor);
	memcpy(init_config + of, &device_descriptor, sizeof(device_descriptor));
	of += sizeof(device_descriptor);

	ret = write(fd, init_config, of);

	if (ret != of)
	{
		printf("Write error %d (%m)\n", ret);
		goto end;
	}

	printf("ep0 configured\n");

	handle_ep0(fd);

	end:
	if (fd != -1) close(fd);

	return err;

	return 0;
}