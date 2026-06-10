	.text

	.export_name	wasi_thread_start, wasi_thread_start

	# firebox#840 (wasm64 cfg-skew): on wasm64 ONLY POINTERS widen to i64 —
	# __stack_pointer/__tls_base/start_arg and the TLS-base arg are i64. The
	# THREAD ID stays a 32-bit `int` (wasi-threads $tid is u32; pthread_create.c
	# declares both `wasi_thread_start(int tid, ...)` and
	# `__wasi_thread_start_C(int tid, ...)` — i32 tid on every arch). An earlier
	# blind i32->i64 widen of this file (copied from the wasm32 .s) widened tid
	# too, so the .s declared __wasi_thread_start_C (i64,i64) while the C defined
	# (i32,i64). wasm-ld can't reconcile the signatures and routes the call into
	# a `signature_mismatch:__wasi_thread_start_C` stub whose body is a bare
	# `unreachable` -> every spawned wasm64 thread trapped before its body ran.
	# tid is param 0 here; keep it i32. (Host side reads the export's declared
	# param types and width-matches each arg, firebox#839 — so this needs no
	# host change.)
	.globaltype	__stack_pointer, i64
	.globaltype	__tls_base, i64
	.functype	__wasi_thread_start_C (i32, i64) -> ()
	.functype	__wasm_init_tls (i64) -> ()

	.hidden	wasi_thread_start
	.globl	wasi_thread_start
	.type	wasi_thread_start,@function

wasi_thread_start:
	.functype	wasi_thread_start (i32, i64) -> ()  # tid i32, start_arg i64 (firebox#840)

	# Set up the minimum C environment.
	# Note: offsetof(start_arg, stack) == 0
	local.get   1  # start_arg
	i64.load    0  # stack
	global.set  __stack_pointer

	# Set up the TLS area
	local.get   1  # start_arg
	i64.load    8  # tls_base
	call __wasm_init_tls

	# Make the C function do the rest of work.
	local.get   0  # tid
	local.get   1  # start_arg
	call __wasi_thread_start_C

	end_function
