PROG= test_server

CFLAGS= -g
INCLUDES= -I../../include

LIBDIR= -L../../src
LIBFILE= ../../src/libnanoev.a

SOURCES= test_server.c

OBJECTS= $(subst .c,.o,$(SOURCES))

$(PROG): $(OBJECTS) $(LIBFILE)
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBDIR) -lnanoev -o $@ $(OBJECTS)
	
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ -c $<

.PHONY: clean

clean:
	rm -f $(OBJECTS)
	rm -f $(PROG)
