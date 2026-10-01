#include <sys/sysinfo.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"

int __lsysinfo(struct sysinfo *info)
{
	return syscall(SYS_sysinfo, info);
}
#else
/* firebox#25D — sysinfo(2) from the host's own figures.
 *
 * A Linux container's sysinfo(2) is not namespaced: it reports the host
 * kernel's uptime, load, RAM, swap and task count. A native Firebox guest
 * shares the host's clocks, so the host's figures are the faithful container
 * answer; __wasix_system_info supplies them. With no host source (the browser)
 * the import is ENOSYS and so is sysinfo — the struct has no "unknown"
 * encoding, so there is no honest partial answer.
 *
 * The host reports bytes. mem_unit is chosen as the kernel chooses it: 1 when
 * every figure fits in unsigned long, else PAGE_SIZE with page counts (the
 * compat_sysinfo rule a 32-bit task on a 64-bit kernel sees — so wasm32 on a
 * host with more than 4 GiB of RAM gets PAGE_SIZE units, and wasm64 always
 * gets bytes). procs is clamped to unsigned short. totalhigh/freehigh are 0:
 * no highmem split exists on any host Firebox runs on. */
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <wasi/api.h>
#include <wasi/libc.h>

static unsigned long clamp_ul(unsigned long long v)
{
	return v > ULONG_MAX ? ULONG_MAX : (unsigned long)v;
}

int __lsysinfo(struct sysinfo *info)
{
	uint64_t v[__WASIX_SYSTEM_INFO_WORDS];
	__wasi_errno_t err = __wasix_system_info(v);
	if (err != __WASI_ERRNO_SUCCESS) {
		errno = __wasilibc_errno_from_wasi(err);
		return -1;
	}
	/* The kernel computes, then copy_to_user()s: a bad pointer is EFAULT
	 * after the query, and ENOSYS (no host source) comes first. */
	if (!info) {
		errno = EFAULT;
		return -1;
	}

	unsigned long long mem[6] = { v[4], v[5], v[6], v[7], v[8], v[9] };
	unsigned mem_unit = 1;
	for (int i = 0; i < 6; i++) {
		if (mem[i] > ULONG_MAX) {
			mem_unit = PAGE_SIZE;
			break;
		}
	}
	if (mem_unit != 1)
		for (int i = 0; i < 6; i++)
			mem[i] /= mem_unit;

	memset(info, 0, sizeof *info);
	info->uptime = v[0] > LONG_MAX ? LONG_MAX : (unsigned long)v[0];
	info->loads[0] = clamp_ul(v[1]);
	info->loads[1] = clamp_ul(v[2]);
	info->loads[2] = clamp_ul(v[3]);
	info->totalram = clamp_ul(mem[0]);
	info->freeram = clamp_ul(mem[1]);
	info->sharedram = clamp_ul(mem[2]);
	info->bufferram = clamp_ul(mem[3]);
	info->totalswap = clamp_ul(mem[4]);
	info->freeswap = clamp_ul(mem[5]);
	info->procs = v[10] > USHRT_MAX ? USHRT_MAX : (unsigned short)v[10];
	info->mem_unit = mem_unit;
	return 0;
}
#endif

weak_alias(__lsysinfo, sysinfo);
