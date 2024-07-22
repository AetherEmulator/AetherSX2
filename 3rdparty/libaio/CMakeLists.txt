# Modified from upstream libaio to remove symbol versioning
# We don't need it since we're linking it as a static library on Android.

set(SRCS
	include/libaio.h
	src/aio_ring.h
	src/io_cancel.c
	src/io_destroy.c
	src/io_getevents.c
	src/io_pgetevents.c
	src/io_queue_init.c
	src/io_queue_release.c
	src/io_queue_run.c
	src/io_queue_wait.c
	src/io_setup.c
	src/io_submit.c
	src/raw_syscall.c
	src/syscall-alpha.h
	src/syscall-arm.h
	src/syscall-generic.h
	src/syscall-i386.h
	src/syscall-ia64.h
	src/syscall-ppc.h
	src/syscall-s390.h
	src/syscall-sparc.h
	src/syscall-x86_64.h
	src/syscall.h
	src/vsys_def.h
)

add_library(libaio ${SRCS})
target_include_directories(libaio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(libaio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

