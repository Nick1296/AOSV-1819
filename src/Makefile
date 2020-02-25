.phony: shared_lib shared_lib-test demo-prog demo-prog-test all all-test module

all: shared-lib demo-lib module

wrap: demo-wrap

module:
		$(MAKE) -C kmodule

shared-lib:
		$(MAKE) -C shared_lib

demo-lib: shared-lib kmodule
		$(MAKE) -C demo

demo-wrap: kmodule
		$(MAKE) -C demo demo-wrap

clean:
		$(MAKE) -C shared_lib clean
		$(MAKE) -C demo clean
		$(MAKE) -C kmodule clean

wrap-test:
		demo/demo

lib-test:
		LD_PRELOAD=$(PWD)/shared_lib/libsessionfs.so demo/demo
