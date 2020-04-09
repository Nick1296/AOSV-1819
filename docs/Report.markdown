# Final Project Report {#mainpage}

**Student Full Name:** Mattia Nicolella
**Student ID:** 1707844

The following report will consist on a general introduction on the content of the project and several sections that will describe how the work has been carried out in each module. The detailed description of how the functions are implemented is provided in the file documentation and it will be referenced during the report when these details are necessary.

## Introduction

SessionFS is a virtual file system wrapper which allows an userspace application to use the Unix session semantics to operate on files.

To enable the Unix session semantic, the `open` and `close` system calls are wrapped by a userspace library (that must be directly included or at least preloaded) which will check if the invocation is a "normal" one or if uses the Unix session semantic.

If the invocation of the system call is not a "normal" one and thus uses the `::O_SESS` custom flag with the "correct" path, the userspace library  will execute an ioctl to an ad-hoc virtual character device, called `SessionFS_dev`, which will handle the invocation accordingly.

If the `O_SESS` flag is missing, or the path is not the one in which sessions are allowed the userspace library will use the libc version of the called function.

This allows the userspace application to trigger the usage of the Unix session semantic with the custom defined `::O_SESS` flag when opening a file in the correct path and does not change the semantic for closing a file.

The module allows the semantic to be used inside a folder which can be specified or changed by the userspace application anytime (defaults to `/mnt`) without having effects on the already opened sessions.
To change the session path is only necessary to write on the `SessionFS_dev` device file the new absolute path in which sessions will be enabled or using the `write_sess_path()` function provided by the library.
To get the current session path instead, a read on the `SessionFS_dev` device file is needed; as before, the userspace library provides the `get_sess_path()` to do so.

On kernel level, the type of ioctl received determines the operation to be executed by another module, the session manager.
The session manger handles the creation of the new session and the closure of existing sessions.
For each file which has active sessions it will enforce the Unix session semantic when adding and deleting incarnations.
Adding an incarnation requires copying the information from the original file to an incarnation file which will have the same name of the original file but will end with `_incarnation_[pid]_[timestamp]`.
Deleting an incarnation requires that the content of the incarnation file will be copied over the original file, overwriting its content; naturally creating an incarnation while another is being closed is not possible, so these operations will be serialized, instead is possible to open concurrently multiple incarnations from the same original file.

The last module involved is the session info module, which will publish information on the active sessions via SysFS.
In detail we have a pseudofile under `/sys/devices/virtual/SessionFS_dev` called `active_sessions` which contains the number of active incarnations.
For each original file we have a folder in the same location as the previous pseudofile with the path of the original file where slashes are substituted by dashes (`/mnt/file` becomes `-mnt-file`); in this folder we have a pseudo file for each process that has an active incarnation on that original file.
These pseudofiles have as the filename the pid of the process and when read they provide the name of the process with that pid.

Finally the session manager is also able to determine if there are invalid sessions, which are owned by dead processes and remove them.

## User Space Library

This library (libsessionfs.c) wraps the `open` and `close` systemcalls to determine which semantic is used during invocation if the`SessionFS_dev` device must bu used to communicate with the kernel module.

Additionally, the library exposes two utility functions, via its header, libsessionfs.h: `get_sess_path()`, `write_sess_path()`, to change the folder in which session are enabled without making the userspace application communicate directly with the `SessionFS_dev` device.
The last utility function exported is `device_shutdown()` which asks the device to disable itself and unlock the module.

The library is composed by:
- `__attribute__((constructor)) init_method()`: constructor used to save the libc `open` and `close`;
- `open()`: wraps the libc `open` function;
- `close()`: wraps the libc `close` function;
- `get_sess_path()`: returns the current session path;
- `write_sess_path()`: changes the current session path;
- `device_shutdown()`: ask to the device to disable itself and unlock the module.

The userspace library is not srictly necessary, but makes the communication with the `SessionFS_dev` device almost transparent from the perspective of the userspace program.

## Kernel-Level Implementation

At kernel level there are several modules to implement the subsystem and several data structures to keep information about the opened sessions, which will be described in this section, referencing their documentation.

The subsystem is divided in four submodules:

- _Module Configuration_: Contains the kernel module configuration.
- _Character Device_: Contains the `SessionFS_dev` device implementation.
- _Session Manager_: Implements operations on the sessions.
- _Session Information_: Publishes sessions information under `SessionFS_dev` device kobject via SysFS.

### Kernel-Level Data Structures Specification

The data structures are described following the dependencies of each submodule, starting from the _Module Configuration_ submodule.

#### Module Configuration

On the kernel module there is only a module parameter, which is the session path of the device `::sess_path`, to offer an alternative path for reading it.

#### Character Device

There are some global variables in the character device, used to store information on the device. Then there are two headers, since we need to separate the information available for the kernel module, contained in device_sessionfs_mod.h, from the device information, which is available to the shared library, contained in device_sessionfs.h.
In device_sessionfs.h the `::sess_params` struct is used to pass parameters between userspace and kernel during ioctls.

 It's important to note that in the device implementation (device_sessionfs.c) there are two global atomic variables:
 - `::device_status`: which determines if the device is disabled and it being removed.
 - `::refcount`: which keeps the information on the number of processes that are actually using the device.

As global variables there are also the session path (`::sess_path`), the read-write lock for the session path (`::dev_lock`) and the length of the string stored in `::sess_path` as `::path_len`.

#### Session Manager

As the character device, the session manager has two headers, one which exports its APIs, session_manager.h, and another, session_types.h, which contains the definition of the objects used by the session manager.

The objects used by the session manager are:

- `::session_rcu`: Contains a  pointer to a `::session` object and is used in the rculist `::sessions`.
- `::session`: Represents an original file and its incarnations.
- `::incarnation`: An incarnation file, related to an original file, so to a `::session` object.
- `::sess_info`: Used to publish informations on a struct `::session` via SysFS.

The session manager implementation (session_manager.c), has also two global variables an RCU list called `::sessions`, which contains all the information about all the sessions and it's protected by a spinlock, `::sessions_lock`.

#### Session Information

This submodule is the one responsible to publish information in the `SessionFS_dev` folder via SysFS, it exposes its APIs to the _Session Manager_ submodule via session_info.h and uses the data structures defined in session_types.h among some global variables used in session_info.c:

- `::sessions_num`: The variable holding the number of active sessions.
- `::dev_kobj`: The `SessionFS_dev` kernel object, provided during the initialization of this subsystem.
- `::kattr`: The kernel attribute of `SessionFS_dev` which represents the number of active sessions.

### Kernel-Level Subsystem Implementation

This section contains the description about the implementation on the functions in each submodule, referencing their documentation, where the description of their implementation is more accurate.

#### Module Configuration

The _Module Configuration_ (module.c) there are only two functions, along with the macros used to add `::sess_path` as a read-only module paramenter.

The functions used in this module are:
- `sessionFS_load()`: which initializes the `SessionFS_dev` device and is used during the module initialization, as specified by `module_init()`.
- `sessionFS_unload()`: which releases the `SessionFS_dev` device and is used during the module exit, as specified by `module_exit()`.

The module exit function cannot be called anytime, because the module will be locked when is necessary by the `SessionFS_dev` device which adds a dependency to it when is in use or when there are sessions that are not closed.

#### Character Device

The _Character Device_ submodule (device_sessionfs.c) implements the `SessionFS_dev` device and it's invoked by the _Module Configuration_ submodule when the module is loaded, where `init_device()` is called and during the module unloading, where `release_device()` is called.
These two function are responsible of the device creation and initialization and the device release_device and cleanup.
It's important to note that when the device is initialized  the kernel module is locked, using `try_get_module()`, which adds a dependency to the the kernel module, locking it.

When the `SessionFS_dev` device has been initialized it can be called from userspace applications, by issuing a read, write or ioctl operation.

When a read operation is issued the `device_read()` function is called, and the content of `::sess_path` is copied in the provided buffer.

When a write operation is issued the `device_write()` is called and, after checking that the provided path is an absolute one the `::sess_path` buffer is reset and overwritten with the new content and the `::path_len` variable is updated.

When an ioctl operation is issued the `device_ioctl()` is called and, according to the parameters, a session can be created with `create_session()`, closed with `close_session()` or a device shutdown can be requested.

If a device shutdown is requested the device checks if the dependency introduced during the `init_device()` should be removed; this happens only if the `::refcount` is 0 and the _Session Manager_ submodule has no sessions.

#### Session Manager

The _Session Manager_ submodule (session_manager.c) is initialized by the _Character Device_ submodule, when `init_manager()` is called.

Then the _Character Device_ can call `create_session()` or `close_session()` according the parameters passed in the received ioctl.

When `create_session()` is called, a `::session` with the same pathname is searched (with `search_session()`) and if nothing valid is found a new  object is created with `init_session()`. Then the `::incarnation` object will be created with `create_incarnation()`.
The original file is opened during the `::session` creation, without a file descriptor associated. The incarnation file is opened during the `::incarnation` creation with an associated file descriptor which will be used by the userspace program. `open_file()` is used to open both files.

When `close_session()` is called, the parent `::session` of the `::incarnation` is located using `search_session()`, then the `::incarnation` is deleted using `delete_incarnation()`. The original file is overwritten only if the parent `::session` is a valid one.
If, after removing the `::incarnation`, the parent `::session` is empty, it will be marked as invalid and removed as soon as possible.

This module also contains a primitive "garbage collector", the `clean_manager()` function, which will walk though all the sessions and will delete the `::incarnation` associated with a process which is dead or in a zombie state, returning the number of sessions associated with an alive process. This function is used in the _Device Module_ to determine if there are active sessions and thus if the module should remain locked even if no process is using the device at the moment.

#### Session Information

The _Session Information_ (session_info.c) handles the publishing of the information on the session via SysFS, so it needs to be initialized with the kernel object of the `SessionFS_dev` device, using `init_info()`.
Conversely, when the device is being released the kernel attribute used to publish the active sessions (`::kattr`) needs to be removed from the kernel object of the device, using `release_info()`.

When an `::session` is created by the _Session Manager_ module the function `add_session_info()` is called, which creates a new subfolder in the `SessionFS_dev` device folder and add an `active_incarnations_num` pseudofile in it. Conversely when a session is removed, the function `remove_incarnation_info()` is called removing the pseudofile and the folder.

Similarly, when an `::incarnation` is created by the _Session Manager_ module the function `add_incarnation_info()` is called, which creates a  new pseudofile in the parent `::session`, which has as filename the pid of the process which owns the `::incarnation`, and gives the process name when read, then the parent `::session` `inc_num` variable, contained in the `sess_info` struct associated with the `::session` object, is incremented.
This variable is accessed when the `::session` `active_incarnations_num` file is read.
Conversely when an incarnation is removed, the function `remove_incarnation_info()` is called removing the pseudofile and decrementing the parent `::session` `inc_num` variable.

## Testcase and Benchmark

The module was tested on the latest long-term support kernel (which is 5.4 at the moment) and can be loaded and unloaded freely.
However there could be situations in which the module cannot be unloaded, this is due to the fact the device is in use or there are sessions which are being used by active processes.

In particular there is a situation in which there are no active session and the module could be unloadable, this con only happen is the last process that opens a session exits without closing it. This is not an issue since the module checks if it can be unloaded every time the `SessionFS_dev` device is used, so to "unlock" the module is sufficient to execute any operation on the device, excluding opening another new session, since in this case the module will be unloadable until the new opened session is closed.

The userspace program (userspace_test.c) is composed of four utility functions and the main, which will use them to carry out tests:
- `change_sess_path()`: changes the session path using the utility function provided by the shared userspace library.
- `func_test()`: tests the module functionalities in a per-process perspective.
- `fork_test()`: tests that the file semantic using session is the same when forking with opened sessions.
- `main()`: executes the tests for a use-case defined by the parameters passed when running the program.

	To use the program two parameters are required, in the following order:
	1. the maximum number of processes;
	2. the maximum number of files used by each process;

Tests have been conducted on four different use-cases, each of these use-cases is available to be used in the makefile under the `src` directory:

1. A single process which can open up to 15 files using sessions.
2. A single process which can open up to 100 file using sessions.
3. Up to 15 processes, where each of them can open up to 15 file using sessions.
4. Up to 100 processes, where each of them can open up to 100 file using sessions.

The single process use case are very quick and straightforward and were generally used to locate errors which made the module unusable.
Instead the multi process test were designed to test how test how the module responds when there are many processes that use it and were used to locate errors in critical sections.

Tests were conducted initially in a virtualized enviroment, which has provided a mean to debug errors swiftly, without having to restart the machine each time there was a crash. When everything was functional on the virtual machine tests have been moved on the live environment and had proven successful as they were in the virtualized environment.
In particular we can see, especially in the use-case 4, that these test are lengthy since the processes can randomly sleep and they write a great amount of data to disk, in the use-case 4 up to 2GB were written on disk.
Tests completed successfully without errors and the kernel module responded swiftly and in the correct way when invoked.
By examining the files we can see that the semantic of the Unix sessions is respected, since there are file that are completely overwritten and we can also see the interleaving of pids when processes append their content instead of overwriting.
If a process leaves the session opened and terminates, the incarnation file will not be removed since the userspace library needs to do so after using the `SessionFS_dev` device; the created session is eventually closed without overwriting the original file when the device shutdown is requested.

\todo run all use cases with time
In here we will report some simple s
1. 3.32s user 1.99s system 25% cpu 20.956 total

## Makefile organization

In the repository there are several makefiles, that execute operations with several levels of detail and where the "outer" makefiles user the "inner" makefile targets to execute its operations:
- In the root directory of the repository the makefile has only three targets:
	- `src`: which builds the source code for the kernel module, the shared library and the userspace program.
	- `docs`: which generates the documentation (including this report) in html and LaTeX in `docs/doxygen`.
	- `clean`: which will clean the repository from all the files that are generated by the above targets, excluding the pdf and html formats of the documentation.
- In the `src` directory the makefile has several targets:
	- `module`: which will build the kernel module.
	- `shared-lib-d`: which will build the userspace shared library using `libsessionfs-d` target.
	- `shared-lib`: which will build the userspace shared library.
	- `demo-lib-d`: which will build the userspace program using `demo-lib-d` target.
	- `demo-lib`: which will build the userspace program.
	- `module-test`: which will run `insmod` and will insert in the kernel the kernel module
	- `lib-test-single`: which will execute the use-case 1.
	- `lib-stress-test-single`: which will execute the use-case 2.
	- `lib-test-multi`: which will execute the use-case 3.
	- `lib-stress-test-multi`: which will execute the use-case 4.
	- `clean`: which will clean the repository from all the files that are generated by the above targets.
- In the `kmodule` directory the makefile has 4 targets:
	- `all`: which will build the kernel module.
	- `insmod`: which will run `dmesg -C` and insert the kernel module in the kernel with `insmod`.
	- `rmmod`: which will unload the module and kill `dmesg` if it is running.
	- `clean`: which will clean the repository from all the files that are generated by the above targets.
- In the `shared_lib` folder the makefile has only three targets:
	- `libsessionfs`: which will build the shared library.
	- `libsessionfs-d`: which will build the shared library enabling address sanitizer and the gdb debug options.
	- `clean`: which will clean the repository from all the files that are generated by the above target.
- In the `demo` folder the makefile has three targets:
	- `demo-lib-d`: which will compile,the userspace program enabling address sanitizer and the gdb debug options.
	- `demo-lib`: which will compile the userspace program linking it with the userspace shared library.
	- `clean`: which will clean the repository from all the files that are generated by the above target.
