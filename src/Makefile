.phony: shared_lib shared_lib-test demo-prog demo-prog-test all all-test module module-test

all: shared-lib demo-lib module

# build the kmodule
module:
		$(MAKE) -C kmodule
#build the shared library
shared-lib:
		$(MAKE) -C shared_lib
#build the userspace program
demo-lib: shared-lib kmodule
		$(MAKE) -C demo

clean:
		$(MAKE) -C shared_lib clean
		$(MAKE) -C demo clean
		$(MAKE) -C kmodule clean
		rm *_process*
		rm sess_change_test*
		rm fork_test*

#insert the module
module-test: module
		$(MAKE) -C kmodule insmod

# test the library with a single thread process that opens ~15 files randomically
lib-test-single: demo-lib module-test
		 LD_LIBRARY_PATH=$(CURDIR)/shared_lib demo/demo 1 15
		$(MAKE) -C kmodule rmmod

# test the library with ~15 processes where each opens ~15 files randomically
lib-test-multi: demo-lib module-test
		LD_LIBRARY_PATH=$(CURDIR)/shared_lib demo/demo 15 15
		$(MAKE) -C kmodule rmmod

# test the library with a single thread process that opens ~100 files randomically
lib-stress-test-single: demo-lib module-test
		LD_LIBRARY_PATH=$(CURDIR)/shared_lib demo/demo 1 100
		$(MAKE) -C kmodule rmmod

# test the library with ~100 processes where each opens ~100 files randomically
lib-stress-test-multi: demo-lib module-test
		LD_LIBRARY_PATH=$(CURDIR)/shared_lib demo/demo 100 100
		$(MAKE) -C kmodule rmmod
