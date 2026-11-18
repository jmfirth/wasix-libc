cmake_minimum_required (VERSION 3.5.0)
project (basic_executable)

add_executable(main main.c)
set_property(TARGET main APPEND_STRING PROPERTY LINK_FLAGS
    " -Wl,--wrap=__pthread_mutex_unlock -Wl,--wrap=__wasilibc_futex_wait_wasix -Wl,--wrap=__clock_gettime -Wl,--wrap=__wasi_poll_oneoff -Wl,--wrap=__wasi_fd_read")
