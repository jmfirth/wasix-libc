	.text

	.export_name	wasi_thread_start, wasi_thread_start

	.globaltype	__stack_pointer, i32
	.globaltype	__tls_base, i32
	.functype	__wasi_thread_start_C (i32, i32) -> ()
	.functype	__wasm_init_tls (i32) -> ()
	# firebox#456: defensive recovery helper for the Binaryen-asyncify
	# escape on thread teardown. See pthread_create.c's
	# __wasilibc_thread_escape_recover for the full rationale. The
	# helper is _Noreturn (always traps via __wasi_thread_exit(0)),
	# so this function-end fall-through is unreachable in practice.
	# Both wasi_thread_start AND __wasilibc_thread_escape_recover
	# must be on the asyncify-removelist in every threaded-package
	# build, or Binaryen instruments them and the state==1 escape
	# bypasses the call below.
	.functype	__wasilibc_thread_escape_recover () -> ()

	.hidden	wasi_thread_start
	.globl	wasi_thread_start
	.type	wasi_thread_start,@function

wasi_thread_start:
	.functype	wasi_thread_start (i32, i32) -> ()

	# Set up the minimum C environment.
	# Note: offsetof(start_arg, stack) == 0
	local.get   1  # start_arg
	i32.load    0  # stack
	global.set  __stack_pointer

	# Set up the TLS area
	local.get   1  # start_arg
	i32.load    4  # tls_base
	call __wasm_init_tls

	# Make the C function do the rest of work.
	local.get   0  # tid
	local.get   1  # start_arg
	call __wasi_thread_start_C

	# firebox#456: any return from __wasi_thread_start_C is, by
	# definition, an asyncify escape -- __pthread_exit is _Noreturn
	# and traps via __wasi_thread_exit(0). The recovery helper
	# drains __asyncify_state, wakes the joiner futex on
	# &self->detach_state, and traps __wasi_thread_exit(0) so the
	# host's set_status_finished path runs. See
	# work/tasks/456-*/reports/2026-05-22-phase1-asyncify-state-tracer-findings.md
	# for the trace evidence.
	call __wasilibc_thread_escape_recover

	end_function
