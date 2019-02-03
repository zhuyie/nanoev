LIB= libnanoev.a

CFLAGS= -g

SOURCES= \
	nanoev_lib.c \
	nanoev_event.c \
	nanoev_timer.c \
	nanoev_misc.c \
	nanoev_internal_unix.c \
	nanoev_tcp.c \
	nanoev_udp.c \
	nanoev_async.c \
	nanoev_loop.c \
	nanoev_poller.c \
	nanoev_poller_iocp.c \
	nanoev_poller_kqueue.c \
	nanoev_poller_epoll.c

OBJECTS= $(subst .c,.o,$(SOURCES))

$(LIB): $(OBJECTS)
	libtool -static -o $@ $^
	
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ -c $<

.PHONY: clean

clean:
	rm -f $(OBJECTS)
	rm -f $(LIB)
