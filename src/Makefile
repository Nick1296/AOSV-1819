.phony: shared_lib shared_lib-test demo-prog demo-prog-test all all-test module module-test

all: shared-lib demo-lib module

wrap: demo-wrap

module:
		$(MAKE) -C kmodule

shared-lib:
		$(MAKE) -C shared_lib

demo-lib: shared-lib kmodule
		$(MAKE) -C demo

demo-wrap: module
		$(MAKE) -C demo demo-wrap

clean:
		$(MAKE) -C shared_lib clean
		$(MAKE) -C demo clean
		$(MAKE) -C kmodule clean

module-test: module
		$(MAKE) -C kmodule insmod

wrap-test: demo-wrap module-test
		demo/demo

lib-test-single: demo-lib module-test
		LD_PRELOAD="/lib/libasan.so $(CURDIR)/shared_lib/libsessionfs.so" LD_LIBRARY_PATH=$(CURDIR)/shared_lib demo/demo 1 20
		$(MAKE) -C kmodule rmmod


lib-test-multi: demo-lib module-test
		LD_PRELOAD="/lib/libasan.so $(CURDIR)/shared_lib/libsessionfs.so" LD_LIBRARY_PATH=$(CURDIR)/shared_lib demo/demo 15 15
		$(MAKE) -C kmodule rmmod

lib-stress-test-single: demo-lib module-test
		LD_PRELOAD="/lib/libasan.so $(CURDIR)/shared_lib/libsessionfs.so" LD_LIBRARY_PATH=$(CURDIR)/shared_lib demo/demo 1 100
		$(MAKE) -C kmodule rmmod

lib-stress-test-multi: demo-lib module-test
		LD_PRELOAD="/lib/libasan.so $(CURDIR)/shared_lib/libsessionfs.so" LD_LIBRARY_PATH=$(CURDIR)/shared_lib demo/demo 100 100
		$(MAKE) -C kmodule rmmod
