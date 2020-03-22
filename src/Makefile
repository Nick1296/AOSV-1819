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

lib-test: demo-lib module-test
		LD_PRELOAD=$(CURDIR)/shared_lib/libsessionfs.so LD_LIBRARY_PATH=$(CURDIR)/shared_lib demo/demo
