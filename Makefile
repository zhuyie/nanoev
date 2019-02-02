DIRS= src test/udp_test test/test_client test/test_server

all:
	@for dir in $(DIRS); do \
	$(MAKE) -C $$dir || exit 1; \
	done

clean:
	@for dir in $(DIRS); do \
	$(MAKE) -C $$dir clean || exit 1; \
	done
