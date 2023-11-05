CFLAGS=-pthread -I/usr/include/gssdp-1.2 -I/usr/include/libsoup-2.4 -I/usr/include/libxml2 -I/usr/include/libmount -I/usr/include/blkid -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include
CFLAGS+=-lgssdp-1.6 -lgio-2.0 -lgobject-2.0 -lglib-2.0

all:
	$(CC) gssdp.c $(CFLAGS) -o gssdp.out
	./gssdp.out

clean:
	$(RM) gssdp.out
