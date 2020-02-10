.phony: shared_lib shared_lib-test demo-prog demo-prog-test all all-test

all: shared-lib demo-prog

all-test: shared_lib-test demo-prog-test

shared-lib:
		$(MAKE) -C shared_lib

demo-prog:
		$(MAKE) -C demo

shared_lib-test:
		$(MAKE) -C shared_lib libsessionfs-test

demo-prog-test:
		$(MAKE) -C demo demo-test

clean:
		$(MAKE) -C shared_lib clean
		$(MAKE) -C demo clean
