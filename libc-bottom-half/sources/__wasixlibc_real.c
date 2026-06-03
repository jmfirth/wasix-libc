/* firebox#796: DUAL-TARGET generated file. The compiler selects the pointer width:
 *   #if defined(__wasm64__) -> wasm64 generation: wasix_64v1 imports,
 *       __wasi_size_t = uint64_t, $size-typed args i64 to match the wasmer
 *       Memory64 host's M::Offset (e.g. sock_listen $backlog).
 *   #else                   -> wasm32 generation, BYTE-IDENTICAL to the prior
 *       committed file (no __LINE__/__FILE__ -> wasm32 build + sysroot are
 *       unaffected -> no registry reseed).
 * Regenerate the wasm64 half with ./build64.sh, then re-run this #if-wrap
 * (work/tasks/797). Do NOT hand-edit a generated half. */
#if defined(__wasm64__)
/**
 * THIS FILE IS AUTO-GENERATED from the following files:
 *   wasix_v1.witx
 *
 * To regenerate this file execute:
 *
 *     cargo run --manifest-path tools/wasi-headers/Cargo.toml generate-libc
 *
 * Modifications to this file will cause CI to fail, the code generator tool
 * must be modified to change this file.
 */

#include <wasi/api.h>
#include <string.h>

int32_t __imported_wasix_64v1_clock_time_set(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("clock_time_set")
));

__wasi_errno_t __wasi_clock_time_set(
    __wasi_clockid_t id,
    __wasi_timestamp_t timestamp
){
    int32_t ret = __imported_wasix_64v1_clock_time_set((int32_t) id, (int64_t) timestamp);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_fd_dup(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("fd_dup")
));

__wasi_errno_t __wasi_fd_dup(
    __wasi_fd_t fd,
    __wasi_fd_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_fd_dup((int32_t) fd, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_fd_dup2(int32_t arg0, int32_t arg1, int32_t arg2, int64_t arg3) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("fd_dup2")
));

__wasi_errno_t __wasi_fd_dup2(
    __wasi_fd_t fd,
    __wasi_fd_t min_result_fd,
    __wasi_bool_t cloexec,
    __wasi_fd_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_fd_dup2((int32_t) fd, (int32_t) min_result_fd, (int32_t) cloexec, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_fd_event(int64_t arg0, int32_t arg1, int64_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("fd_event")
));

__wasi_errno_t __wasi_fd_event(
    uint64_t initial_val,
    __wasi_eventfdflags_t flags,
    __wasi_fd_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_fd_event((int64_t) initial_val, flags, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_fd_pipe(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("fd_pipe")
));

__wasi_errno_t __wasi_fd_pipe(
    __wasi_fd_t *retptr0,
    __wasi_fd_t *retptr1
){
    int32_t ret = __imported_wasix_64v1_fd_pipe((intptr_t) retptr0, (intptr_t) retptr1);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_tty_get(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("tty_get")
));

__wasi_errno_t __wasi_tty_get(
    __wasi_tty_t * state
){
    int32_t ret = __imported_wasix_64v1_tty_get((int64_t) state);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_tty_set(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("tty_set")
));

__wasi_errno_t __wasi_tty_set(
    __wasi_tty_t * state
){
    int32_t ret = __imported_wasix_64v1_tty_set((int64_t) state);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_getcwd(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("getcwd")
));

__wasi_errno_t __wasi_getcwd(
    uint8_t * path,
    __wasi_pointersize_t * path_len
){
    int32_t ret = __imported_wasix_64v1_getcwd((int64_t) path, (int64_t) path_len);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_chdir(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("chdir")
));

__wasi_errno_t __wasi_chdir(
    const char *path
){
    size_t path_len = strlen(path);
    int32_t ret = __imported_wasix_64v1_chdir((intptr_t) path, (intptr_t) path_len);
    return (uint16_t) ret;
}

void __imported_wasix_64v1_callback_signal(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("callback_signal")
));

void __wasi_callback_signal(
    const char *callback
){
    size_t callback_len = strlen(callback);
    __imported_wasix_64v1_callback_signal((intptr_t) callback, (intptr_t) callback_len);
}

int32_t __imported_wasix_64v1_thread_spawn_v2(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("thread_spawn_v2")
));

__wasi_errno_t __wasi_thread_spawn_v2(
    __wasi_thread_start_t * args,
    __wasi_tid_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_thread_spawn_v2((int64_t) args, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_thread_sleep(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("thread_sleep")
));

__wasi_errno_t __wasi_thread_sleep(
    __wasi_timestamp_t duration
){
    int32_t ret = __imported_wasix_64v1_thread_sleep((int64_t) duration);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_thread_id(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("thread_id")
));

__wasi_errno_t __wasi_thread_id(
    __wasi_tid_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_thread_id((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_thread_join(int32_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("thread_join")
));

__wasi_errno_t __wasi_thread_join(
    __wasi_tid_t tid
){
    int32_t ret = __imported_wasix_64v1_thread_join((int32_t) tid);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_thread_parallelism(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("thread_parallelism")
));

__wasi_errno_t __wasi_thread_parallelism(
    __wasi_size_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_thread_parallelism((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_thread_signal(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("thread_signal")
));

__wasi_errno_t __wasi_thread_signal(
    __wasi_tid_t tid,
    __wasi_signal_t signal
){
    int32_t ret = __imported_wasix_64v1_thread_signal((int32_t) tid, (int32_t) signal);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_futex_wait(int64_t arg0, int32_t arg1, int64_t arg2, int64_t arg3) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("futex_wait")
));

__wasi_errno_t __wasi_futex_wait(
    uint32_t * futex,
    uint32_t expected,
    const __wasi_option_timestamp_t * timeout,
    __wasi_bool_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_futex_wait((int64_t) futex, (int32_t) expected, (int64_t) timeout, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_futex_wake(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("futex_wake")
));

__wasi_errno_t __wasi_futex_wake(
    uint32_t * futex,
    __wasi_bool_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_futex_wake((int64_t) futex, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_futex_wake_all(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("futex_wake_all")
));

__wasi_errno_t __wasi_futex_wake_all(
    uint32_t * futex,
    __wasi_bool_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_futex_wake_all((int64_t) futex, (intptr_t) retptr0);
    return (uint16_t) ret;
}

_Noreturn void __imported_wasix_64v1_thread_exit(int32_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("thread_exit")
));

_Noreturn void __wasi_thread_exit(
    __wasi_exitcode_t rval
){
    __imported_wasix_64v1_thread_exit((int32_t) rval);
}

int32_t __imported_wasix_64v1_stack_checkpoint(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("stack_checkpoint")
));

__wasi_errno_t __wasi_stack_checkpoint(
    __wasi_stack_snapshot_t * snapshot,
    __wasi_longsize_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_stack_checkpoint((int64_t) snapshot, (intptr_t) retptr0);
    return (uint16_t) ret;
}

_Noreturn void __imported_wasix_64v1_stack_restore(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("stack_restore")
));

_Noreturn void __wasi_stack_restore(
    const __wasi_stack_snapshot_t * snapshot,
    __wasi_longsize_t val
){
    __imported_wasix_64v1_stack_restore((int64_t) snapshot, (int64_t) val);
}

int32_t __imported_wasix_64v1_path_open2(int32_t arg0, int32_t arg1, int64_t arg2, int64_t arg3, int32_t arg4, int64_t arg5, int64_t arg6, int32_t arg7, int32_t arg8, int64_t arg9) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("path_open2")
));

__wasi_errno_t __wasi_path_open2(
    __wasi_fd_t fd,
    __wasi_lookupflags_t dirflags,
    const char *path,
    __wasi_oflags_t oflags,
    __wasi_rights_t fs_rights_base,
    __wasi_rights_t fs_rights_inheriting,
    __wasi_fdflags_t fdflags,
    __wasi_fdflagsext_t fdflagsext,
    __wasi_fd_t *retptr0
){
    size_t path_len = strlen(path);
    int32_t ret = __imported_wasix_64v1_path_open2((int32_t) fd, dirflags, (intptr_t) path, (intptr_t) path_len, oflags, fs_rights_base, fs_rights_inheriting, fdflags, fdflagsext, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_fd_fdflags_get(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("fd_fdflags_get")
));

__wasi_errno_t __wasi_fd_fdflags_get(
    __wasi_fd_t fd,
    __wasi_fdflagsext_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_fd_fdflags_get((int32_t) fd, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_fd_fdflags_set(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("fd_fdflags_set")
));

__wasi_errno_t __wasi_fd_fdflags_set(
    __wasi_fd_t fd,
    __wasi_fdflagsext_t flags
){
    int32_t ret = __imported_wasix_64v1_fd_fdflags_set((int32_t) fd, flags);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_raise_interval(int32_t arg0, int64_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_raise_interval")
));

__wasi_errno_t __wasi_proc_raise_interval(
    __wasi_signal_t sig,
    __wasi_timestamp_t interval,
    __wasi_bool_t repeat
){
    int32_t ret = __imported_wasix_64v1_proc_raise_interval((int32_t) sig, (int64_t) interval, (int32_t) repeat);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_fork(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_fork")
));

__wasi_errno_t __wasi_proc_fork(
    __wasi_bool_t copy_memory,
    __wasi_pid_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_proc_fork((int32_t) copy_memory, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_fork_env(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_fork_env")
));

__wasi_errno_t __wasi_proc_fork_env(
    __wasi_pid_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_proc_fork_env((intptr_t) retptr0);
    return (uint16_t) ret;
}

void __imported_wasix_64v1_proc_exit2(int32_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_exit2")
));

void __wasi_proc_exit2(
    __wasi_exitcode_t rval
){
    __imported_wasix_64v1_proc_exit2((int32_t) rval);
}

_Noreturn void __imported_wasix_64v1_proc_exec(int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_exec")
));

_Noreturn void __wasi_proc_exec(
    const char *name,
    const char *args
){
    size_t name_len = strlen(name);
    size_t args_len = strlen(args);
    __imported_wasix_64v1_proc_exec((intptr_t) name, (intptr_t) name_len, (intptr_t) args, (intptr_t) args_len);
}

_Noreturn void __imported_wasix_64v1_proc_exec2(int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_exec2")
));

_Noreturn void __wasi_proc_exec2(
    const char *name,
    const char *args,
    const char *envs
){
    size_t name_len = strlen(name);
    size_t args_len = strlen(args);
    size_t envs_len = strlen(envs);
    __imported_wasix_64v1_proc_exec2((intptr_t) name, (intptr_t) name_len, (intptr_t) args, (intptr_t) args_len, (intptr_t) envs, (intptr_t) envs_len);
}

int32_t __imported_wasix_64v1_proc_exec3(int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5, int32_t arg6, int64_t arg7, int64_t arg8) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_exec3")
));

__wasi_errno_t __wasi_proc_exec3(
    const char *name,
    const char *args,
    const char *envs,
    __wasi_bool_t search_path,
    const char *path
){
    size_t name_len = strlen(name);
    size_t args_len = strlen(args);
    size_t envs_len = strlen(envs);
    size_t path_len = strlen(path);
    int32_t ret = __imported_wasix_64v1_proc_exec3((intptr_t) name, (intptr_t) name_len, (intptr_t) args, (intptr_t) args_len, (intptr_t) envs, (intptr_t) envs_len, (int32_t) search_path, (intptr_t) path, (intptr_t) path_len);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_spawn(int64_t arg0, int64_t arg1, int32_t arg2, int64_t arg3, int64_t arg4, int64_t arg5, int64_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int64_t arg10, int64_t arg11, int64_t arg12) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_spawn")
));

__wasi_errno_t __wasi_proc_spawn(
    const char *name,
    __wasi_bool_t chroot,
    const char *args,
    const char *preopen,
    __wasi_stdio_mode_t stdin,
    __wasi_stdio_mode_t stdout,
    __wasi_stdio_mode_t stderr,
    const char *working_dir,
    __wasi_process_handles_t *retptr0
){
    size_t name_len = strlen(name);
    size_t args_len = strlen(args);
    size_t preopen_len = strlen(preopen);
    size_t working_dir_len = strlen(working_dir);
    int32_t ret = __imported_wasix_64v1_proc_spawn((intptr_t) name, (intptr_t) name_len, (int32_t) chroot, (intptr_t) args, (intptr_t) args_len, (intptr_t) preopen, (intptr_t) preopen_len, (int32_t) stdin, (int32_t) stdout, (int32_t) stderr, (intptr_t) working_dir, (intptr_t) working_dir_len, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_spawn2(int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5, int64_t arg6, int64_t arg7, int64_t arg8, int64_t arg9, int32_t arg10, int64_t arg11, int64_t arg12, int64_t arg13) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_spawn2")
));

__wasi_errno_t __wasi_proc_spawn2(
    const char *name,
    const char *args,
    const char *envs,
    const __wasi_proc_spawn_fd_op_t *fd_ops,
    size_t fd_ops_len,
    const __wasi_signal_disposition_t *signal_dispositions,
    size_t signal_dispositions_len,
    __wasi_bool_t search_path,
    const char *path,
    __wasi_pid_t *retptr0
){
    size_t name_len = strlen(name);
    size_t args_len = strlen(args);
    size_t envs_len = strlen(envs);
    size_t path_len = strlen(path);
    int32_t ret = __imported_wasix_64v1_proc_spawn2((intptr_t) name, (intptr_t) name_len, (intptr_t) args, (intptr_t) args_len, (intptr_t) envs, (intptr_t) envs_len, (intptr_t) fd_ops, (intptr_t) fd_ops_len, (intptr_t) signal_dispositions, (intptr_t) signal_dispositions_len, (int32_t) search_path, (intptr_t) path, (intptr_t) path_len, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_id(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_id")
));

__wasi_errno_t __wasi_proc_id(
    __wasi_pid_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_proc_id((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_parent(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_parent")
));

__wasi_errno_t __wasi_proc_parent(
    __wasi_pid_t pid,
    __wasi_pid_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_proc_parent((int32_t) pid, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_join(int64_t arg0, int32_t arg1, int64_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_join")
));

__wasi_errno_t __wasi_proc_join(
    __wasi_option_pid_t * pid,
    __wasi_join_flags_t flags,
    __wasi_join_status_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_proc_join((int64_t) pid, flags, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_signal(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_signal")
));

__wasi_errno_t __wasi_proc_signal(
    __wasi_pid_t pid,
    __wasi_signal_t signal
){
    int32_t ret = __imported_wasix_64v1_proc_signal((int32_t) pid, (int32_t) signal);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_signals_get(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_signals_get")
));

__wasi_errno_t __wasi_proc_signals_get(
    uint8_t * buf
){
    int32_t ret = __imported_wasix_64v1_proc_signals_get((int64_t) buf);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_signals_sizes_get(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_signals_sizes_get")
));

__wasi_errno_t __wasi_proc_signals_sizes_get(
    __wasi_size_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_proc_signals_sizes_get((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_proc_snapshot() __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("proc_snapshot")
));

__wasi_errno_t __wasi_proc_snapshot(
    void
){
    int32_t ret = __imported_wasix_64v1_proc_snapshot();
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_bridge(int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int32_t arg4) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_bridge")
));

__wasi_errno_t __wasi_port_bridge(
    const char *network,
    const char *token,
    __wasi_stream_security_t security
){
    size_t network_len = strlen(network);
    size_t token_len = strlen(token);
    int32_t ret = __imported_wasix_64v1_port_bridge((intptr_t) network, (intptr_t) network_len, (intptr_t) token, (intptr_t) token_len, security);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_unbridge() __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_unbridge")
));

__wasi_errno_t __wasi_port_unbridge(
    void
){
    int32_t ret = __imported_wasix_64v1_port_unbridge();
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_dhcp_acquire() __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_dhcp_acquire")
));

__wasi_errno_t __wasi_port_dhcp_acquire(
    void
){
    int32_t ret = __imported_wasix_64v1_port_dhcp_acquire();
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_addr_add(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_addr_add")
));

__wasi_errno_t __wasi_port_addr_add(
    const __wasi_addr_cidr_t * addr
){
    int32_t ret = __imported_wasix_64v1_port_addr_add((int64_t) addr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_addr_remove(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_addr_remove")
));

__wasi_errno_t __wasi_port_addr_remove(
    const __wasi_addr_t * addr
){
    int32_t ret = __imported_wasix_64v1_port_addr_remove((int64_t) addr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_addr_clear() __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_addr_clear")
));

__wasi_errno_t __wasi_port_addr_clear(
    void
){
    int32_t ret = __imported_wasix_64v1_port_addr_clear();
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_mac(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_mac")
));

__wasi_errno_t __wasi_port_mac(
    __wasi_hardware_address_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_port_mac((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_addr_list(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_addr_list")
));

__wasi_errno_t __wasi_port_addr_list(
    __wasi_addr_cidr_t * addrs,
    __wasi_size_t * naddrs
){
    int32_t ret = __imported_wasix_64v1_port_addr_list((int64_t) addrs, (int64_t) naddrs);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_gateway_set(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_gateway_set")
));

__wasi_errno_t __wasi_port_gateway_set(
    const __wasi_addr_t * addr
){
    int32_t ret = __imported_wasix_64v1_port_gateway_set((int64_t) addr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_route_add(int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_route_add")
));

__wasi_errno_t __wasi_port_route_add(
    const __wasi_addr_cidr_t * cidr,
    const __wasi_addr_t * via_router,
    const __wasi_option_timestamp_t * preferred_until,
    const __wasi_option_timestamp_t * expires_at
){
    int32_t ret = __imported_wasix_64v1_port_route_add((int64_t) cidr, (int64_t) via_router, (int64_t) preferred_until, (int64_t) expires_at);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_route_remove(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_route_remove")
));

__wasi_errno_t __wasi_port_route_remove(
    const __wasi_addr_t * cidr
){
    int32_t ret = __imported_wasix_64v1_port_route_remove((int64_t) cidr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_route_clear() __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_route_clear")
));

__wasi_errno_t __wasi_port_route_clear(
    void
){
    int32_t ret = __imported_wasix_64v1_port_route_clear();
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_port_route_list(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("port_route_list")
));

__wasi_errno_t __wasi_port_route_list(
    __wasi_route_t * routes,
    __wasi_size_t * nroutes
){
    int32_t ret = __imported_wasix_64v1_port_route_list((int64_t) routes, (int64_t) nroutes);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_status(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_status")
));

__wasi_errno_t __wasi_sock_status(
    __wasi_fd_t fd,
    __wasi_sock_status_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_sock_status((int32_t) fd, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_addr_local(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_addr_local")
));

__wasi_errno_t __wasi_sock_addr_local(
    __wasi_fd_t fd,
    __wasi_addr_port_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_sock_addr_local((int32_t) fd, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_addr_peer(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_addr_peer")
));

__wasi_errno_t __wasi_sock_addr_peer(
    __wasi_fd_t fd,
    __wasi_addr_port_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_sock_addr_peer((int32_t) fd, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_open(int32_t arg0, int32_t arg1, int32_t arg2, int64_t arg3) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_open")
));

__wasi_errno_t __wasi_sock_open(
    __wasi_address_family_t af,
    __wasi_sock_type_t socktype,
    __wasi_sock_proto_t sock_proto,
    __wasi_fd_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_sock_open((int32_t) af, (int32_t) socktype, (int32_t) sock_proto, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_pair(int32_t arg0, int32_t arg1, int32_t arg2, int64_t arg3, int64_t arg4) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_pair")
));

__wasi_errno_t __wasi_sock_pair(
    __wasi_address_family_t af,
    __wasi_sock_type_t socktype,
    __wasi_sock_proto_t sock_proto,
    __wasi_fd_t *retptr0,
    __wasi_fd_t *retptr1
){
    int32_t ret = __imported_wasix_64v1_sock_pair((int32_t) af, (int32_t) socktype, (int32_t) sock_proto, (intptr_t) retptr0, (intptr_t) retptr1);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_set_opt_flag(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_set_opt_flag")
));

__wasi_errno_t __wasi_sock_set_opt_flag(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    __wasi_bool_t flag
){
    int32_t ret = __imported_wasix_64v1_sock_set_opt_flag((int32_t) fd, (int32_t) sockopt, (int32_t) flag);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_get_opt_flag(int32_t arg0, int32_t arg1, int64_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_get_opt_flag")
));

__wasi_errno_t __wasi_sock_get_opt_flag(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    __wasi_bool_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_sock_get_opt_flag((int32_t) fd, (int32_t) sockopt, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_set_opt_time(int32_t arg0, int32_t arg1, int64_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_set_opt_time")
));

__wasi_errno_t __wasi_sock_set_opt_time(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    const __wasi_option_timestamp_t * timeout
){
    int32_t ret = __imported_wasix_64v1_sock_set_opt_time((int32_t) fd, (int32_t) sockopt, (int64_t) timeout);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_get_opt_time(int32_t arg0, int32_t arg1, int64_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_get_opt_time")
));

__wasi_errno_t __wasi_sock_get_opt_time(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    __wasi_option_timestamp_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_sock_get_opt_time((int32_t) fd, (int32_t) sockopt, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_set_opt_size(int32_t arg0, int32_t arg1, int64_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_set_opt_size")
));

__wasi_errno_t __wasi_sock_set_opt_size(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    __wasi_filesize_t size
){
    int32_t ret = __imported_wasix_64v1_sock_set_opt_size((int32_t) fd, (int32_t) sockopt, (int64_t) size);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_get_opt_size(int32_t arg0, int32_t arg1, int64_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_get_opt_size")
));

__wasi_errno_t __wasi_sock_get_opt_size(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    __wasi_filesize_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_sock_get_opt_size((int32_t) fd, (int32_t) sockopt, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_join_multicast_v4(int32_t arg0, int64_t arg1, int64_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_join_multicast_v4")
));

__wasi_errno_t __wasi_sock_join_multicast_v4(
    __wasi_fd_t fd,
    const __wasi_addr_ip4_t * multiaddr,
    const __wasi_addr_ip4_t * interface
){
    int32_t ret = __imported_wasix_64v1_sock_join_multicast_v4((int32_t) fd, (int64_t) multiaddr, (int64_t) interface);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_leave_multicast_v4(int32_t arg0, int64_t arg1, int64_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_leave_multicast_v4")
));

__wasi_errno_t __wasi_sock_leave_multicast_v4(
    __wasi_fd_t fd,
    const __wasi_addr_ip4_t * multiaddr,
    const __wasi_addr_ip4_t * interface
){
    int32_t ret = __imported_wasix_64v1_sock_leave_multicast_v4((int32_t) fd, (int64_t) multiaddr, (int64_t) interface);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_join_multicast_v6(int32_t arg0, int64_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_join_multicast_v6")
));

__wasi_errno_t __wasi_sock_join_multicast_v6(
    __wasi_fd_t fd,
    const __wasi_addr_ip6_t * multiaddr,
    uint32_t interface
){
    int32_t ret = __imported_wasix_64v1_sock_join_multicast_v6((int32_t) fd, (int64_t) multiaddr, (int32_t) interface);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_leave_multicast_v6(int32_t arg0, int64_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_leave_multicast_v6")
));

__wasi_errno_t __wasi_sock_leave_multicast_v6(
    __wasi_fd_t fd,
    const __wasi_addr_ip6_t * multiaddr,
    uint32_t interface
){
    int32_t ret = __imported_wasix_64v1_sock_leave_multicast_v6((int32_t) fd, (int64_t) multiaddr, (int32_t) interface);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_bind(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_bind")
));

__wasi_errno_t __wasi_sock_bind(
    __wasi_fd_t fd,
    const __wasi_addr_port_t * addr
){
    int32_t ret = __imported_wasix_64v1_sock_bind((int32_t) fd, (int64_t) addr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_listen(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_listen")
));

__wasi_errno_t __wasi_sock_listen(
    __wasi_fd_t fd,
    __wasi_size_t backlog
){
    int32_t ret = __imported_wasix_64v1_sock_listen((int32_t) fd, (int64_t) backlog);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_accept_v2(int32_t arg0, int32_t arg1, int64_t arg2, int64_t arg3) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_accept_v2")
));

__wasi_errno_t __wasi_sock_accept_v2(
    __wasi_fd_t fd,
    __wasi_fdflags_t flags,
    __wasi_fd_t *retptr0,
    __wasi_addr_port_t *retptr1
){
    int32_t ret = __imported_wasix_64v1_sock_accept_v2((int32_t) fd, flags, (intptr_t) retptr0, (intptr_t) retptr1);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_connect(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_connect")
));

__wasi_errno_t __wasi_sock_connect(
    __wasi_fd_t fd,
    const __wasi_addr_port_t * addr
){
    int32_t ret = __imported_wasix_64v1_sock_connect((int32_t) fd, (int64_t) addr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_recv_from(int32_t arg0, int64_t arg1, int64_t arg2, int32_t arg3, int64_t arg4, int64_t arg5, int64_t arg6) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_recv_from")
));

__wasi_errno_t __wasi_sock_recv_from(
    __wasi_fd_t fd,
    const __wasi_iovec_t *ri_data,
    size_t ri_data_len,
    __wasi_riflags_t ri_flags,
    __wasi_size_t *retptr0,
    __wasi_roflags_t *retptr1,
    __wasi_addr_port_t *retptr2
){
    int32_t ret = __imported_wasix_64v1_sock_recv_from((int32_t) fd, (intptr_t) ri_data, (intptr_t) ri_data_len, ri_flags, (intptr_t) retptr0, (intptr_t) retptr1, (intptr_t) retptr2);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_send_to(int32_t arg0, int64_t arg1, int64_t arg2, int32_t arg3, int64_t arg4, int64_t arg5) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_send_to")
));

__wasi_errno_t __wasi_sock_send_to(
    __wasi_fd_t fd,
    const __wasi_ciovec_t *si_data,
    size_t si_data_len,
    __wasi_siflags_t si_flags,
    const __wasi_addr_port_t * addr,
    __wasi_size_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_sock_send_to((int32_t) fd, (intptr_t) si_data, (intptr_t) si_data_len, si_flags, (int64_t) addr, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_sock_send_file(int32_t arg0, int32_t arg1, int64_t arg2, int64_t arg3, int64_t arg4) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("sock_send_file")
));

__wasi_errno_t __wasi_sock_send_file(
    __wasi_fd_t out_fd,
    __wasi_fd_t in_fd,
    __wasi_filesize_t offset,
    __wasi_filesize_t count,
    __wasi_filesize_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_sock_send_file((int32_t) out_fd, (int32_t) in_fd, (int64_t) offset, (int64_t) count, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_resolve(int64_t arg0, int64_t arg1, int32_t arg2, int64_t arg3, int64_t arg4, int64_t arg5) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("resolve")
));

__wasi_errno_t __wasi_resolve(
    const char *host,
    uint16_t port,
    __wasi_addr_ip_t * addrs,
    __wasi_size_t naddrs,
    __wasi_size_t *retptr0
){
    size_t host_len = strlen(host);
    int32_t ret = __imported_wasix_64v1_resolve((intptr_t) host, (intptr_t) host_len, (int32_t) port, (int64_t) addrs, (int64_t) naddrs, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_epoll_create(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("epoll_create")
));

__wasi_errno_t __wasi_epoll_create(
    __wasi_fd_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_epoll_create((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_epoll_ctl(int32_t arg0, int32_t arg1, int32_t arg2, int64_t arg3) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("epoll_ctl")
));

__wasi_errno_t __wasi_epoll_ctl(
    __wasi_fd_t epfd,
    __wasi_epoll_ctl_t op,
    __wasi_fd_t fd,
    const __wasi_epoll_event_t * event
){
    int32_t ret = __imported_wasix_64v1_epoll_ctl((int32_t) epfd, (int32_t) op, (int32_t) fd, (int64_t) event);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_epoll_wait(int32_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("epoll_wait")
));

__wasi_errno_t __wasi_epoll_wait(
    __wasi_fd_t epfd,
    __wasi_epoll_event_t * event,
    __wasi_size_t maxevents,
    __wasi_timestamp_t timeout,
    __wasi_size_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_epoll_wait((int32_t) epfd, (int64_t) event, (int64_t) maxevents, (int64_t) timeout, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_dl_invalid_handle(int32_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("dl_invalid_handle")
));

__wasi_errno_t __wasi_dl_invalid_handle(
    __wasi_dl_handle_t handle
){
    int32_t ret = __imported_wasix_64v1_dl_invalid_handle((int32_t) handle);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_dlopen(int64_t arg0, int64_t arg1, int32_t arg2, int64_t arg3, int64_t arg4, int64_t arg5, int64_t arg6, int64_t arg7) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("dlopen")
));

__wasi_errno_t __wasi_dlopen(
    const char *path,
    __wasi_dl_flags_t flags,
    uint8_t * err_buf,
    __wasi_size_t err_buf_len,
    const char *ld_library_path,
    __wasi_dl_handle_t *retptr0
){
    size_t path_len = strlen(path);
    size_t ld_library_path_len = strlen(ld_library_path);
    int32_t ret = __imported_wasix_64v1_dlopen((intptr_t) path, (intptr_t) path_len, flags, (int64_t) err_buf, (int64_t) err_buf_len, (intptr_t) ld_library_path, (intptr_t) ld_library_path_len, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_dlsym(int32_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("dlsym")
));

__wasi_errno_t __wasi_dlsym(
    __wasi_dl_handle_t handle,
    const char *symbol,
    uint8_t * err_buf,
    __wasi_size_t err_buf_len,
    __wasi_size_t *retptr0
){
    size_t symbol_len = strlen(symbol);
    int32_t ret = __imported_wasix_64v1_dlsym((int32_t) handle, (intptr_t) symbol, (intptr_t) symbol_len, (int64_t) err_buf, (int64_t) err_buf_len, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_call_dynamic(int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int32_t arg5) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("call_dynamic")
));

__wasi_errno_t __wasi_call_dynamic(
    __wasi_function_pointer_t function_id,
    const uint8_t *values,
    size_t values_len,
    uint8_t * results,
    __wasi_pointersize_t results_len,
    __wasi_bool_t strict
){
    int32_t ret = __imported_wasix_64v1_call_dynamic((int64_t) function_id, (intptr_t) values, (intptr_t) values_len, (int64_t) results, (int64_t) results_len, (int32_t) strict);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_closure_prepare(int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5, int64_t arg6) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("closure_prepare")
));

__wasi_errno_t __wasi_closure_prepare(
    __wasi_function_pointer_t backing_function_id,
    __wasi_function_pointer_t closure_id,
    const __wasi_wasm_value_type_t *argument_types,
    size_t argument_types_len,
    const __wasi_wasm_value_type_t *result_types,
    size_t result_types_len,
    uint8_t * user_data_ptr
){
    int32_t ret = __imported_wasix_64v1_closure_prepare((int64_t) backing_function_id, (int64_t) closure_id, (intptr_t) argument_types, (intptr_t) argument_types_len, (intptr_t) result_types, (intptr_t) result_types_len, (int64_t) user_data_ptr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_closure_allocate(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("closure_allocate")
));

__wasi_errno_t __wasi_closure_allocate(
    __wasi_function_pointer_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_closure_allocate((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_closure_free(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("closure_free")
));

__wasi_errno_t __wasi_closure_free(
    __wasi_function_pointer_t closure_id
){
    int32_t ret = __imported_wasix_64v1_closure_free((int64_t) closure_id);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_reflect_signature(int64_t arg0, int64_t arg1, int32_t arg2, int64_t arg3, int32_t arg4, int64_t arg5) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("reflect_signature")
));

__wasi_errno_t __wasi_reflect_signature(
    __wasi_function_pointer_t function_id,
    __wasi_wasm_value_type_t * argument_types,
    uint16_t argument_types_len,
    __wasi_wasm_value_type_t * result_types,
    uint16_t result_types_len,
    __wasi_reflection_result_t *retptr0
){
    int32_t ret = __imported_wasix_64v1_reflect_signature((int64_t) function_id, (int64_t) argument_types, (int32_t) argument_types_len, (int64_t) result_types, (int32_t) result_types_len, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_context_create(int64_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("context_create")
));

__wasi_errno_t __wasi_context_create(
    __wasi_context_id_t * new_context_ptr,
    __wasi_function_pointer_t entrypoint
){
    int32_t ret = __imported_wasix_64v1_context_create((int64_t) new_context_ptr, (int64_t) entrypoint);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_context_switch(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("context_switch")
));

__wasi_errno_t __wasi_context_switch(
    __wasi_context_id_t next_context
){
    int32_t ret = __imported_wasix_64v1_context_switch((int64_t) next_context);
    return (uint16_t) ret;
}

int32_t __imported_wasix_64v1_context_destroy(int64_t arg0) __attribute__((
    __import_module__("wasix_64v1"),
    __import_name__("context_destroy")
));

__wasi_errno_t __wasi_context_destroy(
    __wasi_context_id_t context
){
    int32_t ret = __imported_wasix_64v1_context_destroy((int64_t) context);
    return (uint16_t) ret;
}


/* ── Firebox WASIX extensions (carried for wasm64; firebox#796) ───────────── */
#ifdef __wasm64__
#define FBX_WASIX_V1 "wasix_64v1"
#else
#define FBX_WASIX_V1 "wasix_32v1"
#endif
int32_t __imported_wasix_fbx_fd_lock(int32_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_lock")));
__wasi_errno_t __wasix_fd_lock(__wasi_fd_t fd,uint32_t op){return (uint16_t)__imported_wasix_fbx_fd_lock((int32_t)fd,(int32_t)op);}
int32_t __imported_wasix_fbx_fd_lock_range(int32_t,int32_t,int32_t,int32_t,int64_t,int64_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_lock_range")));
__wasi_errno_t __wasix_fd_lock_range(__wasi_fd_t fd,uint32_t op,uint32_t l_type,uint32_t whence,int64_t start,int64_t len){return (uint16_t)__imported_wasix_fbx_fd_lock_range((int32_t)fd,(int32_t)op,(int32_t)l_type,(int32_t)whence,start,len);}
int32_t __imported_wasix_fbx_fd_chmod(int32_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_chmod")));
__wasi_errno_t __wasix_fd_chmod(__wasi_fd_t fd,uint32_t mode){return (uint16_t)__imported_wasix_fbx_fd_chmod((int32_t)fd,(int32_t)mode);}
int32_t __imported_wasix_fbx_path_chmod(int32_t,intptr_t,intptr_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_chmod")));
__wasi_errno_t __wasix_path_chmod(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t mode){return (uint16_t)__imported_wasix_fbx_path_chmod((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)mode);}
int32_t __imported_wasix_fbx_path_lchmod(int32_t,intptr_t,intptr_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_lchmod")));
__wasi_errno_t __wasix_path_lchmod(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t mode){return (uint16_t)__imported_wasix_fbx_path_lchmod((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)mode);}
int32_t __imported_wasix_fbx_path_mknod(int32_t,intptr_t,intptr_t,int32_t,int64_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_mknod")));
__wasi_errno_t __wasix_path_mknod(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t mode,uint64_t dev){return (uint16_t)__imported_wasix_fbx_path_mknod((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)mode,(int64_t)dev);}
int32_t __imported_wasix_fbx_fd_ioctl(int32_t,int32_t,intptr_t,intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_ioctl")));
__wasi_errno_t __wasi_fd_ioctl(__wasi_fd_t fd,uint32_t request,void *argp,int32_t *retptr0){return (uint16_t)__imported_wasix_fbx_fd_ioctl((int32_t)fd,(int32_t)request,(intptr_t)argp,(intptr_t)retptr0);}
#else /* firebox#796: wasm32 generation */
/**
 * THIS FILE IS AUTO-GENERATED from the following files:
 *   wasix_v1.witx
 *
 * To regenerate this file execute:
 *
 *     cargo run --manifest-path tools/wasi-headers/Cargo.toml generate-libc
 *
 * Modifications to this file will cause CI to fail, the code generator tool
 * must be modified to change this file.
 */

#include <wasi/api.h>
#include <string.h>

/*
 * firebox #54: compute the length of a combined argv/envp buffer
 * produced by __wasilibc_exec_combine_strings().
 *
 * The buffer uses `'\0'` as the between-entries separator (post-#54
 * guest), so plain strlen() stops at the first entry and passes a
 * truncated length to the WASIX host. The buffer terminates with a
 * double NUL (`...\0\0`) specifically so this helper can find the
 * real end.
 *
 * For NULL buffers we return 0 (matches the host's null-pointer
 * handling in proc_exec3 / proc_spawn2).
 *
 * Callers of these wrappers inside wasix-libc are exclusively
 * execvp.c, execv.c, posix_spawn.c — all of which use
 * __wasilibc_exec_combine_strings and produce the double-NUL
 * terminator. External callers do not exist: the wrappers are not
 * in any public header.
 */
static size_t __wasilibc_exec_buffer_len(const char *buf)
{
    if (buf == (const char *)0) {
        return 0;
    }
    const unsigned char *p = (const unsigned char *)buf;
    if (*p == 0) {
        return 0;
    }
    /* Scan for two consecutive zero bytes. Return the position of the
     * first zero plus one, so the host sees a complete final entry
     * followed by its terminating NUL. */
    for (;;) {
        while (*p != 0) {
            p++;
        }
        if (p[1] == 0) {
            return (size_t)(p - (const unsigned char *)buf) + 1;
        }
        p++;
    }
}

int32_t __imported_wasix_32v1_clock_time_set(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("clock_time_set")
));

__wasi_errno_t __wasi_clock_time_set(
    __wasi_clockid_t id,
    __wasi_timestamp_t timestamp
){
    int32_t ret = __imported_wasix_32v1_clock_time_set((int32_t) id, (int64_t) timestamp);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_fd_dup(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("fd_dup")
));

__wasi_errno_t __wasi_fd_dup(
    __wasi_fd_t fd,
    __wasi_fd_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_fd_dup((int32_t) fd, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_fd_dup2(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("fd_dup2")
));

__wasi_errno_t __wasi_fd_dup2(
    __wasi_fd_t fd,
    __wasi_fd_t min_result_fd,
    __wasi_bool_t cloexec,
    __wasi_fd_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_fd_dup2((int32_t) fd, (int32_t) min_result_fd, (int32_t) cloexec, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_fd_event(int64_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("fd_event")
));

__wasi_errno_t __wasi_fd_event(
    uint64_t initial_val,
    __wasi_eventfdflags_t flags,
    __wasi_fd_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_fd_event((int64_t) initial_val, flags, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_fd_pipe(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("fd_pipe")
));

__wasi_errno_t __wasi_fd_pipe(
    __wasi_fd_t *retptr0,
    __wasi_fd_t *retptr1
){
    int32_t ret = __imported_wasix_32v1_fd_pipe((intptr_t) retptr0, (intptr_t) retptr1);
    return (uint16_t) ret;
}

/*
 * firebox#712 (GUI O1): general ioctl passthrough.
 *
 * Hand-added (NOT regenerated from wasix_v1.witx — the witx does not yet
 * carry fd_ioctl). The host syscall is fd_ioctl(fd, request, argp, ret):
 * the guest passes the full Linux _IOC(dir, type, nr, size) `request` and
 * an `argp` pointer into its own linear memory; the host decodes the
 * request's direction/size, dispatches to the device's VirtualFile::ioctl
 * handler, and writes the ioctl's integer return through `ret`. The errno
 * is the function's return value. See the wasmer fork's
 * lib/wasix/src/syscalls/wasix/fd_ioctl.rs (firebox#705) and
 * docs/architecture/framebuffer-device.md §3 (O1). This is the import a
 * real program's ioctl(2) default case (sys/ioctl/ioctl.c) resolves to,
 * so fbterm / SDL fbcon / libinput reach the framebuffer + evdev devices.
 *
 * The __import_module__ decoration is load-bearing: an undecorated extern
 * would land under the default "env" module and the host (which registers
 * under "wasix_32v1") would never link it (the import-module-name drift
 * class lesson). This file is the wasm-only generated thunk TU, so no
 * `#ifdef __wasm__` weak-default gate is needed — every thunk here is an
 * unconditional wasm import, the same shape as the fd_dup2/tty_get thunks
 * above. `wasm-objdump -j Import` on a consumer confirms the module/name.
 */
int32_t __imported_wasix_32v1_fd_ioctl(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("fd_ioctl")
));

__wasi_errno_t __wasi_fd_ioctl(
    __wasi_fd_t fd,
    uint32_t request,
    void *argp,
    int32_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_fd_ioctl((int32_t) fd, (int32_t) request, (intptr_t) argp, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_tty_get(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("tty_get")
));

__wasi_errno_t __wasi_tty_get(
    __wasi_tty_t * state
){
    int32_t ret = __imported_wasix_32v1_tty_get((int32_t) state);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_tty_set(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("tty_set")
));

__wasi_errno_t __wasi_tty_set(
    __wasi_tty_t * state
){
    int32_t ret = __imported_wasix_32v1_tty_set((int32_t) state);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_getcwd(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("getcwd")
));

__wasi_errno_t __wasi_getcwd(
    uint8_t * path,
    __wasi_pointersize_t * path_len
){
    int32_t ret = __imported_wasix_32v1_getcwd((int32_t) path, (int32_t) path_len);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_chdir(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("chdir")
));

__wasi_errno_t __wasi_chdir(
    const char *path
){
    size_t path_len = strlen(path);
    int32_t ret = __imported_wasix_32v1_chdir((intptr_t) path, (intptr_t) path_len);
    return (uint16_t) ret;
}

void __imported_wasix_32v1_callback_signal(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("callback_signal")
));

void __wasi_callback_signal(
    const char *callback
){
    size_t callback_len = strlen(callback);
    __imported_wasix_32v1_callback_signal((intptr_t) callback, (intptr_t) callback_len);
}

int32_t __imported_wasix_32v1_thread_spawn_v2(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("thread_spawn_v2")
));

__wasi_errno_t __wasi_thread_spawn_v2(
    __wasi_thread_start_t * args,
    __wasi_tid_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_thread_spawn_v2((int32_t) args, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_thread_sleep(int64_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("thread_sleep")
));

__wasi_errno_t __wasi_thread_sleep(
    __wasi_timestamp_t duration
){
    int32_t ret = __imported_wasix_32v1_thread_sleep((int64_t) duration);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_thread_id(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("thread_id")
));

__wasi_errno_t __wasi_thread_id(
    __wasi_tid_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_thread_id((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_thread_join(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("thread_join")
));

__wasi_errno_t __wasi_thread_join(
    __wasi_tid_t tid
){
    int32_t ret = __imported_wasix_32v1_thread_join((int32_t) tid);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_thread_parallelism(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("thread_parallelism")
));

__wasi_errno_t __wasi_thread_parallelism(
    __wasi_size_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_thread_parallelism((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_thread_signal(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("thread_signal")
));

__wasi_errno_t __wasi_thread_signal(
    __wasi_tid_t tid,
    __wasi_signal_t signal
){
    int32_t ret = __imported_wasix_32v1_thread_signal((int32_t) tid, (int32_t) signal);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_futex_wait(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("futex_wait")
));

__wasi_errno_t __wasi_futex_wait(
    uint32_t * futex,
    uint32_t expected,
    const __wasi_option_timestamp_t * timeout,
    __wasi_bool_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_futex_wait((int32_t) futex, (int32_t) expected, (int32_t) timeout, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_futex_wake(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("futex_wake")
));

__wasi_errno_t __wasi_futex_wake(
    uint32_t * futex,
    __wasi_bool_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_futex_wake((int32_t) futex, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_futex_wake_all(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("futex_wake_all")
));

__wasi_errno_t __wasi_futex_wake_all(
    uint32_t * futex,
    __wasi_bool_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_futex_wake_all((int32_t) futex, (intptr_t) retptr0);
    return (uint16_t) ret;
}

_Noreturn void __imported_wasix_32v1_thread_exit(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("thread_exit")
));

_Noreturn void __wasi_thread_exit(
    __wasi_exitcode_t rval
){
    __imported_wasix_32v1_thread_exit((int32_t) rval);
}

int32_t __imported_wasix_32v1_stack_checkpoint(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("stack_checkpoint")
));

__wasi_errno_t __wasi_stack_checkpoint(
    __wasi_stack_snapshot_t * snapshot,
    __wasi_longsize_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_stack_checkpoint((int32_t) snapshot, (intptr_t) retptr0);
    return (uint16_t) ret;
}

_Noreturn void __imported_wasix_32v1_stack_restore(int32_t arg0, int64_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("stack_restore")
));

_Noreturn void __wasi_stack_restore(
    const __wasi_stack_snapshot_t * snapshot,
    __wasi_longsize_t val
){
    __imported_wasix_32v1_stack_restore((int32_t) snapshot, (int64_t) val);
}

int32_t __imported_wasix_32v1_path_open2(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int64_t arg5, int64_t arg6, int32_t arg7, int32_t arg8, int32_t arg9) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("path_open2")
));

__wasi_errno_t __wasi_path_open2(
    __wasi_fd_t fd,
    __wasi_lookupflags_t dirflags,
    const char *path,
    __wasi_oflags_t oflags,
    __wasi_rights_t fs_rights_base,
    __wasi_rights_t fs_rights_inheriting,
    __wasi_fdflags_t fdflags,
    __wasi_fdflagsext_t fdflagsext,
    __wasi_fd_t *retptr0
){
    size_t path_len = strlen(path);
    int32_t ret = __imported_wasix_32v1_path_open2((int32_t) fd, dirflags, (intptr_t) path, (intptr_t) path_len, oflags, fs_rights_base, fs_rights_inheriting, fdflags, fdflagsext, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_fd_fdflags_get(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("fd_fdflags_get")
));

__wasi_errno_t __wasi_fd_fdflags_get(
    __wasi_fd_t fd,
    __wasi_fdflagsext_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_fd_fdflags_get((int32_t) fd, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_fd_fdflags_set(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("fd_fdflags_set")
));

__wasi_errno_t __wasi_fd_fdflags_set(
    __wasi_fd_t fd,
    __wasi_fdflagsext_t flags
){
    int32_t ret = __imported_wasix_32v1_fd_fdflags_set((int32_t) fd, flags);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_raise_interval(int32_t arg0, int64_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_raise_interval")
));

__wasi_errno_t __wasi_proc_raise_interval(
    __wasi_signal_t sig,
    __wasi_timestamp_t interval,
    __wasi_bool_t repeat
){
    int32_t ret = __imported_wasix_32v1_proc_raise_interval((int32_t) sig, (int64_t) interval, (int32_t) repeat);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_fork(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_fork")
));

__wasi_errno_t __wasi_proc_fork(
    __wasi_bool_t copy_memory,
    __wasi_pid_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_proc_fork((int32_t) copy_memory, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_fork_env(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_fork_env")
));

__wasi_errno_t __wasi_proc_fork_env(
    __wasi_pid_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_proc_fork_env((intptr_t) retptr0);
    return (uint16_t) ret;
}

void __imported_wasix_32v1_proc_exit2(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_exit2")
));

void __wasi_proc_exit2(
    __wasi_exitcode_t rval
){
    __imported_wasix_32v1_proc_exit2((int32_t) rval);
}

_Noreturn void __imported_wasix_32v1_proc_exec(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_exec")
));

_Noreturn void __wasi_proc_exec(
    const char *name,
    const char *args
){
    size_t name_len = strlen(name);
    size_t args_len = strlen(args);
    __imported_wasix_32v1_proc_exec((intptr_t) name, (intptr_t) name_len, (intptr_t) args, (intptr_t) args_len);
}

_Noreturn void __imported_wasix_32v1_proc_exec2(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_exec2")
));

_Noreturn void __wasi_proc_exec2(
    const char *name,
    const char *args,
    const char *envs
){
    size_t name_len = strlen(name);
    size_t args_len = strlen(args);
    size_t envs_len = strlen(envs);
    __imported_wasix_32v1_proc_exec2((intptr_t) name, (intptr_t) name_len, (intptr_t) args, (intptr_t) args_len, (intptr_t) envs, (intptr_t) envs_len);
}

int32_t __imported_wasix_32v1_proc_exec3(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7, int32_t arg8) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_exec3")
));

__wasi_errno_t __wasi_proc_exec3(
    const char *name,
    const char *args,
    const char *envs,
    __wasi_bool_t search_path,
    const char *path
){
    size_t name_len = strlen(name);
    /* firebox #54: args / envs are NUL-separated combined buffers, so
     * use double-NUL scan instead of strlen. */
    size_t args_len = __wasilibc_exec_buffer_len(args);
    size_t envs_len = __wasilibc_exec_buffer_len(envs);
    size_t path_len = (path != (const char *)0) ? strlen(path) : 0;
    int32_t ret = __imported_wasix_32v1_proc_exec3((intptr_t) name, (intptr_t) name_len, (intptr_t) args, (intptr_t) args_len, (intptr_t) envs, (intptr_t) envs_len, (int32_t) search_path, (intptr_t) path, (intptr_t) path_len);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_spawn(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_spawn")
));

__wasi_errno_t __wasi_proc_spawn(
    const char *name,
    __wasi_bool_t chroot,
    const char *args,
    const char *preopen,
    __wasi_stdio_mode_t stdin,
    __wasi_stdio_mode_t stdout,
    __wasi_stdio_mode_t stderr,
    const char *working_dir,
    __wasi_process_handles_t *retptr0
){
    size_t name_len = strlen(name);
    /* firebox #54: args / preopen are NUL-separated combined buffers. */
    size_t args_len = __wasilibc_exec_buffer_len(args);
    size_t preopen_len = __wasilibc_exec_buffer_len(preopen);
    size_t working_dir_len = strlen(working_dir);
    int32_t ret = __imported_wasix_32v1_proc_spawn((intptr_t) name, (intptr_t) name_len, (int32_t) chroot, (intptr_t) args, (intptr_t) args_len, (intptr_t) preopen, (intptr_t) preopen_len, (int32_t) stdin, (int32_t) stdout, (int32_t) stderr, (intptr_t) working_dir, (intptr_t) working_dir_len, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_spawn2(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12, int32_t arg13) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_spawn2")
));

__wasi_errno_t __wasi_proc_spawn2(
    const char *name,
    const char *args,
    const char *envs,
    const __wasi_proc_spawn_fd_op_t *fd_ops,
    size_t fd_ops_len,
    const __wasi_signal_disposition_t *signal_dispositions,
    size_t signal_dispositions_len,
    __wasi_bool_t search_path,
    const char *path,
    __wasi_pid_t *retptr0
){
    size_t name_len = strlen(name);
    /* firebox #54: args / envs are NUL-separated combined buffers. */
    size_t args_len = __wasilibc_exec_buffer_len(args);
    size_t envs_len = __wasilibc_exec_buffer_len(envs);
    size_t path_len = (path != (const char *)0) ? strlen(path) : 0;
    int32_t ret = __imported_wasix_32v1_proc_spawn2((intptr_t) name, (intptr_t) name_len, (intptr_t) args, (intptr_t) args_len, (intptr_t) envs, (intptr_t) envs_len, (intptr_t) fd_ops, (intptr_t) fd_ops_len, (intptr_t) signal_dispositions, (intptr_t) signal_dispositions_len, (int32_t) search_path, (intptr_t) path, (intptr_t) path_len, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_id(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_id")
));

__wasi_errno_t __wasi_proc_id(
    __wasi_pid_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_proc_id((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_parent(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_parent")
));

__wasi_errno_t __wasi_proc_parent(
    __wasi_pid_t pid,
    __wasi_pid_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_proc_parent((int32_t) pid, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_join(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_join")
));

__wasi_errno_t __wasi_proc_join(
    __wasi_option_pid_t * pid,
    __wasi_join_flags_t flags,
    __wasi_join_status_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_proc_join((int32_t) pid, flags, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_signal(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_signal")
));

__wasi_errno_t __wasi_proc_signal(
    __wasi_pid_t pid,
    __wasi_signal_t signal
){
    int32_t ret = __imported_wasix_32v1_proc_signal((int32_t) pid, (int32_t) signal);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_signals_get(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_signals_get")
));

__wasi_errno_t __wasi_proc_signals_get(
    uint8_t * buf
){
    int32_t ret = __imported_wasix_32v1_proc_signals_get((int32_t) buf);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_signals_sizes_get(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_signals_sizes_get")
));

__wasi_errno_t __wasi_proc_signals_sizes_get(
    __wasi_size_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_proc_signals_sizes_get((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_proc_snapshot() __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("proc_snapshot")
));

__wasi_errno_t __wasi_proc_snapshot(
    void
){
    int32_t ret = __imported_wasix_32v1_proc_snapshot();
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_bridge(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_bridge")
));

__wasi_errno_t __wasi_port_bridge(
    const char *network,
    const char *token,
    __wasi_stream_security_t security
){
    size_t network_len = strlen(network);
    size_t token_len = strlen(token);
    int32_t ret = __imported_wasix_32v1_port_bridge((intptr_t) network, (intptr_t) network_len, (intptr_t) token, (intptr_t) token_len, security);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_unbridge() __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_unbridge")
));

__wasi_errno_t __wasi_port_unbridge(
    void
){
    int32_t ret = __imported_wasix_32v1_port_unbridge();
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_dhcp_acquire() __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_dhcp_acquire")
));

__wasi_errno_t __wasi_port_dhcp_acquire(
    void
){
    int32_t ret = __imported_wasix_32v1_port_dhcp_acquire();
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_addr_add(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_addr_add")
));

__wasi_errno_t __wasi_port_addr_add(
    const __wasi_addr_cidr_t * addr
){
    int32_t ret = __imported_wasix_32v1_port_addr_add((int32_t) addr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_addr_remove(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_addr_remove")
));

__wasi_errno_t __wasi_port_addr_remove(
    const __wasi_addr_t * addr
){
    int32_t ret = __imported_wasix_32v1_port_addr_remove((int32_t) addr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_addr_clear() __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_addr_clear")
));

__wasi_errno_t __wasi_port_addr_clear(
    void
){
    int32_t ret = __imported_wasix_32v1_port_addr_clear();
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_mac(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_mac")
));

__wasi_errno_t __wasi_port_mac(
    __wasi_hardware_address_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_port_mac((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_addr_list(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_addr_list")
));

__wasi_errno_t __wasi_port_addr_list(
    __wasi_addr_cidr_t * addrs,
    __wasi_size_t * naddrs
){
    int32_t ret = __imported_wasix_32v1_port_addr_list((int32_t) addrs, (int32_t) naddrs);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_gateway_set(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_gateway_set")
));

__wasi_errno_t __wasi_port_gateway_set(
    const __wasi_addr_t * addr
){
    int32_t ret = __imported_wasix_32v1_port_gateway_set((int32_t) addr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_route_add(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_route_add")
));

__wasi_errno_t __wasi_port_route_add(
    const __wasi_addr_cidr_t * cidr,
    const __wasi_addr_t * via_router,
    const __wasi_option_timestamp_t * preferred_until,
    const __wasi_option_timestamp_t * expires_at
){
    int32_t ret = __imported_wasix_32v1_port_route_add((int32_t) cidr, (int32_t) via_router, (int32_t) preferred_until, (int32_t) expires_at);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_route_remove(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_route_remove")
));

__wasi_errno_t __wasi_port_route_remove(
    const __wasi_addr_t * cidr
){
    int32_t ret = __imported_wasix_32v1_port_route_remove((int32_t) cidr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_route_clear() __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_route_clear")
));

__wasi_errno_t __wasi_port_route_clear(
    void
){
    int32_t ret = __imported_wasix_32v1_port_route_clear();
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_port_route_list(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("port_route_list")
));

__wasi_errno_t __wasi_port_route_list(
    __wasi_route_t * routes,
    __wasi_size_t * nroutes
){
    int32_t ret = __imported_wasix_32v1_port_route_list((int32_t) routes, (int32_t) nroutes);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_status(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_status")
));

__wasi_errno_t __wasi_sock_status(
    __wasi_fd_t fd,
    __wasi_sock_status_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_sock_status((int32_t) fd, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_addr_local(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_addr_local")
));

__wasi_errno_t __wasi_sock_addr_local(
    __wasi_fd_t fd,
    __wasi_addr_port_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_sock_addr_local((int32_t) fd, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_addr_peer(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_addr_peer")
));

__wasi_errno_t __wasi_sock_addr_peer(
    __wasi_fd_t fd,
    __wasi_addr_port_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_sock_addr_peer((int32_t) fd, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_open(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_open")
));

__wasi_errno_t __wasi_sock_open(
    __wasi_address_family_t af,
    __wasi_sock_type_t socktype,
    __wasi_sock_proto_t sock_proto,
    __wasi_fd_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_sock_open((int32_t) af, (int32_t) socktype, (int32_t) sock_proto, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_pair(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_pair")
));

__wasi_errno_t __wasi_sock_pair(
    __wasi_address_family_t af,
    __wasi_sock_type_t socktype,
    __wasi_sock_proto_t sock_proto,
    __wasi_fd_t *retptr0,
    __wasi_fd_t *retptr1
){
    int32_t ret = __imported_wasix_32v1_sock_pair((int32_t) af, (int32_t) socktype, (int32_t) sock_proto, (intptr_t) retptr0, (intptr_t) retptr1);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_set_opt_flag(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_set_opt_flag")
));

__wasi_errno_t __wasi_sock_set_opt_flag(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    __wasi_bool_t flag
){
    int32_t ret = __imported_wasix_32v1_sock_set_opt_flag((int32_t) fd, (int32_t) sockopt, (int32_t) flag);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_get_opt_flag(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_get_opt_flag")
));

__wasi_errno_t __wasi_sock_get_opt_flag(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    __wasi_bool_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_sock_get_opt_flag((int32_t) fd, (int32_t) sockopt, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_set_opt_time(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_set_opt_time")
));

__wasi_errno_t __wasi_sock_set_opt_time(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    const __wasi_option_timestamp_t * timeout
){
    int32_t ret = __imported_wasix_32v1_sock_set_opt_time((int32_t) fd, (int32_t) sockopt, (int32_t) timeout);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_get_opt_time(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_get_opt_time")
));

__wasi_errno_t __wasi_sock_get_opt_time(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    __wasi_option_timestamp_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_sock_get_opt_time((int32_t) fd, (int32_t) sockopt, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_set_opt_size(int32_t arg0, int32_t arg1, int64_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_set_opt_size")
));

__wasi_errno_t __wasi_sock_set_opt_size(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    __wasi_filesize_t size
){
    int32_t ret = __imported_wasix_32v1_sock_set_opt_size((int32_t) fd, (int32_t) sockopt, (int64_t) size);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_get_opt_size(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_get_opt_size")
));

__wasi_errno_t __wasi_sock_get_opt_size(
    __wasi_fd_t fd,
    __wasi_sock_option_t sockopt,
    __wasi_filesize_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_sock_get_opt_size((int32_t) fd, (int32_t) sockopt, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_join_multicast_v4(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_join_multicast_v4")
));

__wasi_errno_t __wasi_sock_join_multicast_v4(
    __wasi_fd_t fd,
    const __wasi_addr_ip4_t * multiaddr,
    const __wasi_addr_ip4_t * interface
){
    int32_t ret = __imported_wasix_32v1_sock_join_multicast_v4((int32_t) fd, (int32_t) multiaddr, (int32_t) interface);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_leave_multicast_v4(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_leave_multicast_v4")
));

__wasi_errno_t __wasi_sock_leave_multicast_v4(
    __wasi_fd_t fd,
    const __wasi_addr_ip4_t * multiaddr,
    const __wasi_addr_ip4_t * interface
){
    int32_t ret = __imported_wasix_32v1_sock_leave_multicast_v4((int32_t) fd, (int32_t) multiaddr, (int32_t) interface);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_join_multicast_v6(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_join_multicast_v6")
));

__wasi_errno_t __wasi_sock_join_multicast_v6(
    __wasi_fd_t fd,
    const __wasi_addr_ip6_t * multiaddr,
    uint32_t interface
){
    int32_t ret = __imported_wasix_32v1_sock_join_multicast_v6((int32_t) fd, (int32_t) multiaddr, (int32_t) interface);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_leave_multicast_v6(int32_t arg0, int32_t arg1, int32_t arg2) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_leave_multicast_v6")
));

__wasi_errno_t __wasi_sock_leave_multicast_v6(
    __wasi_fd_t fd,
    const __wasi_addr_ip6_t * multiaddr,
    uint32_t interface
){
    int32_t ret = __imported_wasix_32v1_sock_leave_multicast_v6((int32_t) fd, (int32_t) multiaddr, (int32_t) interface);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_bind(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_bind")
));

__wasi_errno_t __wasi_sock_bind(
    __wasi_fd_t fd,
    const __wasi_addr_port_t * addr
){
    int32_t ret = __imported_wasix_32v1_sock_bind((int32_t) fd, (int32_t) addr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_listen(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_listen")
));

__wasi_errno_t __wasi_sock_listen(
    __wasi_fd_t fd,
    __wasi_size_t backlog
){
    int32_t ret = __imported_wasix_32v1_sock_listen((int32_t) fd, (int32_t) backlog);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_accept_v2(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_accept_v2")
));

__wasi_errno_t __wasi_sock_accept_v2(
    __wasi_fd_t fd,
    __wasi_fdflags_t flags,
    __wasi_fd_t *retptr0,
    __wasi_addr_port_t *retptr1
){
    int32_t ret = __imported_wasix_32v1_sock_accept_v2((int32_t) fd, flags, (intptr_t) retptr0, (intptr_t) retptr1);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_connect(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_connect")
));

__wasi_errno_t __wasi_sock_connect(
    __wasi_fd_t fd,
    const __wasi_addr_port_t * addr
){
    int32_t ret = __imported_wasix_32v1_sock_connect((int32_t) fd, (int32_t) addr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_recv_from(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_recv_from")
));

__wasi_errno_t __wasi_sock_recv_from(
    __wasi_fd_t fd,
    const __wasi_iovec_t *ri_data,
    size_t ri_data_len,
    __wasi_riflags_t ri_flags,
    __wasi_size_t *retptr0,
    __wasi_roflags_t *retptr1,
    __wasi_addr_port_t *retptr2
){
    int32_t ret = __imported_wasix_32v1_sock_recv_from((int32_t) fd, (intptr_t) ri_data, (intptr_t) ri_data_len, ri_flags, (intptr_t) retptr0, (intptr_t) retptr1, (intptr_t) retptr2);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_send_to(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_send_to")
));

__wasi_errno_t __wasi_sock_send_to(
    __wasi_fd_t fd,
    const __wasi_ciovec_t *si_data,
    size_t si_data_len,
    __wasi_siflags_t si_flags,
    const __wasi_addr_port_t * addr,
    __wasi_size_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_sock_send_to((int32_t) fd, (intptr_t) si_data, (intptr_t) si_data_len, si_flags, (int32_t) addr, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_sock_send_file(int32_t arg0, int32_t arg1, int64_t arg2, int64_t arg3, int32_t arg4) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("sock_send_file")
));

__wasi_errno_t __wasi_sock_send_file(
    __wasi_fd_t out_fd,
    __wasi_fd_t in_fd,
    __wasi_filesize_t offset,
    __wasi_filesize_t count,
    __wasi_filesize_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_sock_send_file((int32_t) out_fd, (int32_t) in_fd, (int64_t) offset, (int64_t) count, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_resolve(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("resolve")
));

__wasi_errno_t __wasi_resolve(
    const char *host,
    uint16_t port,
    __wasi_addr_ip_t * addrs,
    __wasi_size_t naddrs,
    __wasi_size_t *retptr0
){
    size_t host_len = strlen(host);
    int32_t ret = __imported_wasix_32v1_resolve((intptr_t) host, (intptr_t) host_len, (int32_t) port, (int32_t) addrs, (int32_t) naddrs, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_epoll_create(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("epoll_create")
));

__wasi_errno_t __wasi_epoll_create(
    __wasi_fd_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_epoll_create((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_epoll_ctl(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("epoll_ctl")
));

__wasi_errno_t __wasi_epoll_ctl(
    __wasi_fd_t epfd,
    __wasi_epoll_ctl_t op,
    __wasi_fd_t fd,
    const __wasi_epoll_event_t * event
){
    int32_t ret = __imported_wasix_32v1_epoll_ctl((int32_t) epfd, (int32_t) op, (int32_t) fd, (int32_t) event);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_epoll_wait(int32_t arg0, int32_t arg1, int32_t arg2, int64_t arg3, int32_t arg4) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("epoll_wait")
));

__wasi_errno_t __wasi_epoll_wait(
    __wasi_fd_t epfd,
    __wasi_epoll_event_t * event,
    __wasi_size_t maxevents,
    __wasi_timestamp_t timeout,
    __wasi_size_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_epoll_wait((int32_t) epfd, (int32_t) event, (int32_t) maxevents, (int64_t) timeout, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_dl_invalid_handle(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("dl_invalid_handle")
));

__wasi_errno_t __wasi_dl_invalid_handle(
    __wasi_dl_handle_t handle
){
    int32_t ret = __imported_wasix_32v1_dl_invalid_handle((int32_t) handle);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_dlopen(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("dlopen")
));

__wasi_errno_t __wasi_dlopen(
    const char *path,
    __wasi_dl_flags_t flags,
    uint8_t * err_buf,
    __wasi_size_t err_buf_len,
    const char *ld_library_path,
    __wasi_dl_handle_t *retptr0
){
    size_t path_len = strlen(path);
    size_t ld_library_path_len = strlen(ld_library_path);
    int32_t ret = __imported_wasix_32v1_dlopen((intptr_t) path, (intptr_t) path_len, flags, (int32_t) err_buf, (int32_t) err_buf_len, (intptr_t) ld_library_path, (intptr_t) ld_library_path_len, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_dlsym(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("dlsym")
));

__wasi_errno_t __wasi_dlsym(
    __wasi_dl_handle_t handle,
    const char *symbol,
    uint8_t * err_buf,
    __wasi_size_t err_buf_len,
    __wasi_size_t *retptr0
){
    size_t symbol_len = strlen(symbol);
    int32_t ret = __imported_wasix_32v1_dlsym((int32_t) handle, (intptr_t) symbol, (intptr_t) symbol_len, (int32_t) err_buf, (int32_t) err_buf_len, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_call_dynamic(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("call_dynamic")
));

__wasi_errno_t __wasi_call_dynamic(
    __wasi_function_pointer_t function_id,
    const uint8_t *values,
    size_t values_len,
    uint8_t * results,
    __wasi_pointersize_t results_len,
    __wasi_bool_t strict
){
    int32_t ret = __imported_wasix_32v1_call_dynamic((int32_t) function_id, (intptr_t) values, (intptr_t) values_len, (int32_t) results, (int32_t) results_len, (int32_t) strict);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_closure_prepare(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("closure_prepare")
));

__wasi_errno_t __wasi_closure_prepare(
    __wasi_function_pointer_t backing_function_id,
    __wasi_function_pointer_t closure_id,
    const __wasi_wasm_value_type_t *argument_types,
    size_t argument_types_len,
    const __wasi_wasm_value_type_t *result_types,
    size_t result_types_len,
    uint8_t * user_data_ptr
){
    int32_t ret = __imported_wasix_32v1_closure_prepare((int32_t) backing_function_id, (int32_t) closure_id, (intptr_t) argument_types, (intptr_t) argument_types_len, (intptr_t) result_types, (intptr_t) result_types_len, (int32_t) user_data_ptr);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_closure_allocate(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("closure_allocate")
));

__wasi_errno_t __wasi_closure_allocate(
    __wasi_function_pointer_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_closure_allocate((intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_closure_free(int32_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("closure_free")
));

__wasi_errno_t __wasi_closure_free(
    __wasi_function_pointer_t closure_id
){
    int32_t ret = __imported_wasix_32v1_closure_free((int32_t) closure_id);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_reflect_signature(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("reflect_signature")
));

__wasi_errno_t __wasi_reflect_signature(
    __wasi_function_pointer_t function_id,
    __wasi_wasm_value_type_t * argument_types,
    uint16_t argument_types_len,
    __wasi_wasm_value_type_t * result_types,
    uint16_t result_types_len,
    __wasi_reflection_result_t *retptr0
){
    int32_t ret = __imported_wasix_32v1_reflect_signature((int32_t) function_id, (int32_t) argument_types, (int32_t) argument_types_len, (int32_t) result_types, (int32_t) result_types_len, (intptr_t) retptr0);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_context_create(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("context_create")
));

__wasi_errno_t __wasi_context_create(
    __wasi_context_id_t * new_context_ptr,
    __wasi_function_pointer_t entrypoint
){
    int32_t ret = __imported_wasix_32v1_context_create((int32_t) new_context_ptr, (int32_t) entrypoint);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_context_switch(int64_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("context_switch")
));

__wasi_errno_t __wasi_context_switch(
    __wasi_context_id_t next_context
){
    int32_t ret = __imported_wasix_32v1_context_switch((int64_t) next_context);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_context_destroy(int64_t arg0) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("context_destroy")
));

__wasi_errno_t __wasi_context_destroy(
    __wasi_context_id_t context
){
    int32_t ret = __imported_wasix_32v1_context_destroy((int64_t) context);
    return (uint16_t) ret;
}

// Firebox extension: path_chmod and fd_chmod for POSIX permission support.
// These are not part of the upstream WASIX spec but are provided by
// Firebox's patched wasmer runtime (jmfirth/wasmer#firebox-patches).

int32_t __imported_wasix_32v1_path_chmod(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("path_chmod")
));

__wasi_errno_t __wasix_path_chmod(
    __wasi_fd_t fd,
    const char *path,
    size_t path_len,
    uint32_t mode
){
    int32_t ret = __imported_wasix_32v1_path_chmod((int32_t) fd, (int32_t) path, (int32_t) path_len, (int32_t) mode);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_path_lchmod(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("path_lchmod")
));

__wasi_errno_t __wasix_path_lchmod(
    __wasi_fd_t fd,
    const char *path,
    size_t path_len,
    uint32_t mode
){
    int32_t ret = __imported_wasix_32v1_path_lchmod((int32_t) fd, (int32_t) path, (int32_t) path_len, (int32_t) mode);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_fd_chmod(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("fd_chmod")
));

__wasi_errno_t __wasix_fd_chmod(
    __wasi_fd_t fd,
    uint32_t mode
){
    int32_t ret = __imported_wasix_32v1_fd_chmod((int32_t) fd, (int32_t) mode);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_path_mknod(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int64_t arg4) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("path_mknod")
));

__wasi_errno_t __wasix_path_mknod(
    __wasi_fd_t fd,
    const char *path,
    size_t path_len,
    uint32_t mode,
    uint64_t dev
){
    int32_t ret = __imported_wasix_32v1_path_mknod((int32_t) fd, (int32_t) path, (int32_t) path_len, (int32_t) mode, (int64_t) dev);
    return (uint16_t) ret;
}

// Firebox extension: advisory file locks (Firebox issue #243).
// These are not part of the upstream WASIX spec but are provided by
// Firebox's patched wasmer runtime (jmfirth/wasmer#firebox-patches).
//
// fd_lock implements BSD flock(2) semantics; fd_lock_range implements
// POSIX fcntl(F_SETLK)/F_GETLK/F_SETLKW semantics. The runtime maintains
// a process-wide lock table keyed by inode and pid. See the Rust-side
// implementation at lib/wasix/src/fs/file_lock.rs in the wasmer fork
// for the table semantics; the headers in <sys/file.h> and <fcntl.h>
// expose the constants the wasix runtime expects in `op` / `l_type` /
// `whence` here.

int32_t __imported_wasix_32v1_fd_lock(int32_t arg0, int32_t arg1) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("fd_lock")
));

__wasi_errno_t __wasix_fd_lock(
    __wasi_fd_t fd,
    uint32_t op
){
    int32_t ret = __imported_wasix_32v1_fd_lock((int32_t) fd, (int32_t) op);
    return (uint16_t) ret;
}

int32_t __imported_wasix_32v1_fd_lock_range(
    int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3,
    int64_t arg4, int64_t arg5
) __attribute__((
    __import_module__("wasix_32v1"),
    __import_name__("fd_lock_range")
));

__wasi_errno_t __wasix_fd_lock_range(
    __wasi_fd_t fd,
    uint32_t op,
    uint32_t l_type,
    uint32_t whence,
    int64_t start,
    int64_t len
){
    int32_t ret = __imported_wasix_32v1_fd_lock_range(
        (int32_t) fd, (int32_t) op, (int32_t) l_type, (int32_t) whence,
        start, len
    );
    return (uint16_t) ret;
}

#endif /* firebox#796: dual-target */
