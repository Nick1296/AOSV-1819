.phony: source lib-test-single lib-stress-test-single lib-test-multi lib-stess-test-multi clean

all: source

clean:
		rm -f *_process*
		rm -f sess_change_test*
		rm -f fork_test*

# test the library with a single thread process that opens ~15 files randomically
lib-test-single:
		LD_LIBRARY_PATH=$(CURDIR)/../src/shared_lib ../src/demo/demo 1 15

# test the library with ~15 processes where each opens ~15 files randomically
lib-test-multi:
		LD_LIBRARY_PATH=$(CURDIR)/../src/shared_lib ../src/demo/demo 15 15

# test the library with a single thread process that opens ~100 files randomically
lib-stress-test-single:
		LD_LIBRARY_PATH=$(CURDIR)/../src/shared_lib ../src/demo/demo 1 100

# test the library with ~100 processes where each opens ~100 files randomically
lib-stress-test-multi:
		LD_LIBRARY_PATH=$(CURDIR)/../src/shared_lib ../src/demo/demo 100 100
