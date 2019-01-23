LIB= libnanoev.a

SOURCES= \
	nanoev_lib.c \
	nanoev_event.c \
	nanoev_timer.c \
	nanoev_misc.c \
	nanoev_internal_unix.c \
	nanoev_tcp.c \
	nanoev_udp.c \
	nanoev_loop.c

OBJECTS= $(subst .c,.o,$(SOURCES))

$(LIB): $(OBJECTS)
	ar rcs $@ $^
	
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ -c $<

.PHONY: clean

clean:
	rm -f $(OBJECTS)
	rm -f $(LIB)
