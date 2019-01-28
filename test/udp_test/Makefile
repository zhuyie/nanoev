PROG= udp_test

INCLUDES= -I../../include

LIBDIR= -L../../src
LIBFILE= ../../src/libnanoev.a

SOURCES= udp_test.c

OBJECTS= $(subst .c,.o,$(SOURCES))

$(PROG): $(OBJECTS) $(LIBFILE)
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBDIR) -lnanoev -o $@ $(OBJECTS)
	
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ -c $<

.PHONY: clean

clean:
	rm -f $(OBJECTS)
	rm -f $(PROG)
