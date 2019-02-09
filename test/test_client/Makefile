PROG= test_client

CFLAGS= -g
INCLUDES= -I../../include

LIBDIR= -L../../source
LIBFILE= ../../source/libnanoev.a

SOURCES= test_client.c

OBJECTS= $(subst .c,.o,$(SOURCES))

$(PROG): $(OBJECTS) $(LIBFILE)
	$(CC) $(OBJECTS) $(CFLAGS) $(INCLUDES) $(LIBDIR) -lnanoev -o $@
	
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ -c $<

.PHONY: clean

clean:
	rm -f $(OBJECTS)
	rm -f $(PROG)
