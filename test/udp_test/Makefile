PROG= udp_test

CFLAGS= -g
INCLUDES= -I../../include

LIBDIR= -L../../src
LIBFILE= ../../src/libnanoev.a

SOURCES= udp_test.c

OBJECTS= $(subst .c,.o,$(SOURCES))

$(PROG): $(OBJECTS) $(LIBFILE)
	$(CC) $(OBJECTS) $(CFLAGS) $(INCLUDES) $(LIBDIR) -lnanoev -o $@
	
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ -c $<

.PHONY: clean

clean:
	rm -f $(OBJECTS)
	rm -f $(PROG)
