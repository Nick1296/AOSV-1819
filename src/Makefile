.phony: shared_lib shared_lib-test demo-prog demo-prog-test all all-test

all: shared-lib demo-lib

wrap: demo-wrap

shared-lib:
		$(MAKE) -C shared_lib

demo-lib: shared-lib
		$(MAKE) -C demo

demo-wrap:
		$(MAKE) -C demo demo-wrap

clean:
		$(MAKE) -C shared_lib clean
		$(MAKE) -C demo clean

wrap-test:
		demo/demo

lib-test:
		LD_PRELOAD=$(PWD)/shared_lib/libsessionfs.so demo/demo
