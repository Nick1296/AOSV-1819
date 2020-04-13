.phony: shared_lib shared_lib-test demo-prog demo-prog-test all all-test module module-test

all: shared-lib demo-lib module

# build the kmodule
module:
		$(MAKE) -C kmodule

#build the shared library
shared-lib:
		$(MAKE) -C shared_lib libsessionfs

#build the userspace program
demo-lib: shared-lib kmodule
		$(MAKE) -C demo demo-lib

clean:
		$(MAKE) -C shared_lib clean
		$(MAKE) -C demo clean
		$(MAKE) -C kmodule clean
