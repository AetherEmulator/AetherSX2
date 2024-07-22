/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(_WIN32)
#include <wx/thread.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "common/PageFaultSource.h"

// Apple uses the MAP_ANON define instead of MAP_ANONYMOUS, but they mean
// the same thing.
#if defined(__APPLE__) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/ashmem.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ucontext.h>
#endif

extern void SignalExit(int sig);

static const uptr m_pagemask = getpagesize() - 1;

static struct sigaction s_old_sigsegv_action;
#if defined(__APPLE__) || defined(__aarch64__)
static struct sigaction s_old_sigbus_action;
#endif

// Linux implementation of SIGSEGV handler.  Bind it using sigaction().
static void SysPageFaultSignalFilter(int signal, siginfo_t* siginfo, void* ctx)
{
	// [TODO] : Add a thread ID filter to the Linux Signal handler here.
	// Rationale: On windows, the __try/__except model allows per-thread specific behavior
	// for page fault handling.  On linux, there is a single signal handler for the whole
	// process, but the handler is executed by the thread that caused the exception.


	// Stdio Usage note: SIGSEGV handling is a synchronous in-thread signal.  It is done
	// from the context of the current thread and stackframe.  So long as the thread is not
	// the main/ui thread, use of the px assertion system should be safe.  Use of stdio should
	// be safe even on the main thread.
	//  (in other words, stdio limitations only really apply to process-level asynchronous
	//   signals)

	// Note: Use of stdio functions isn't safe here.  Avoid console logs,
	// assertions, file logs, or just about anything else useful.

#if defined(_M_AMD64)
	void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
	void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.pc);
#else
	void* const exception_pc = nullptr;
#endif

	// Note: This signal can be accessed by the EE or MTVU thread
	// Source_PageFault is a global variable with its own state information
	// so for now we lock this exception code unless someone can fix this better...
	Threading::ScopedLock lock(PageFault_Mutex);

	Source_PageFault->Dispatch(PageFaultInfo((uptr)exception_pc, (uptr)siginfo->si_addr & ~m_pagemask));

	// resumes execution right where we left off (re-executes instruction that
	// caused the SIGSEGV).
	if (Source_PageFault->WasHandled())
		return;

#ifndef __ANDROID__
	if (!wxThread::IsMain())
	{
		pxFailRel(pxsFmt("Unhandled page fault @ 0x%08x", siginfo->si_addr));
	}

	// Bad mojo!  Completely invalid address.
	// Instigate a trap if we're in a debugger, and if not then do a SIGKILL.

	pxTrap();
	if (!IsDebugBuild)
		raise(SIGKILL);
#else
  // call old signal handler
#if !defined(__APPLE__) && !defined(__aarch64__)
  const struct sigaction& sa = s_old_sigsegv_action;
#else
  const struct sigaction& sa = (signal == SIGBUS) ? s_old_sigbus_action : s_old_sigsegv_action;
#endif
  if (sa.sa_flags & SA_SIGINFO)
    sa.sa_sigaction(signal, siginfo, ctx);
  else if (sa.sa_handler == SIG_DFL)
  {
	struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = SysPageFaultSignalFilter;
	::signal(signal, SIG_DFL);
	raise(signal);
	sigaction(signal, &sa, &sa);
  }
  else if (sa.sa_handler == SIG_IGN)
    return;
  else
    sa.sa_handler(signal);
#endif
}

void _platform_InstallSignalHandler()
{
	Console.WriteLn("Installing POSIX SIGSEGV handler...");
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = SysPageFaultSignalFilter;
#if defined(__APPLE__) || defined(__aarch64__)
	// MacOS uses SIGBUS for memory permission violations
	sigaction(SIGBUS, &sa, &s_old_sigbus_action);
#endif
#if !defined(__APPLE__)
	sigaction(SIGSEGV, &sa, &s_old_sigsegv_action);
#endif
}

static __ri void PageSizeAssertionTest(size_t size)
{
	pxAssertMsg((__pagesize == getpagesize()), pxsFmt(
												   "Internal system error: Operating system pagesize does not match compiled pagesize.\n\t"
												   L"\tOS Page Size: 0x%x (%d), Compiled Page Size: 0x%x (%u)",
												   getpagesize(), getpagesize(), __pagesize, __pagesize));

	pxAssertDev((size & (__pagesize - 1)) == 0, pxsFmt(
													L"Memory block size must be a multiple of the target platform's page size.\n"
													L"\tPage Size: 0x%x (%u), Block Size: 0x%x (%u)",
													__pagesize, __pagesize, size, size));
}

uint LinuxProt(const PageProtectionMode& mode)
{
	uint lnxmode = 0;

	if (mode.CanWrite())
		lnxmode |= PROT_WRITE;
	if (mode.CanRead())
		lnxmode |= PROT_READ;
	if (mode.CanExecute())
		lnxmode |= PROT_EXEC | PROT_READ;

	return lnxmode;
}

// returns FALSE if the mprotect call fails with an ENOMEM.
// Raises assertions on other types of POSIX errors (since those typically reflect invalid object
// or memory states).
static bool _memprotect(void* baseaddr, size_t size, const PageProtectionMode& mode)
{
	PageSizeAssertionTest(size);

	uint lnxmode = LinuxProt(mode);

	const int result = mprotect(baseaddr, size, lnxmode);

	if (result == 0)
		return true;

	switch (errno)
	{
		case EINVAL:
			pxFailDev(pxsFmt(L"mprotect returned EINVAL @ 0x%08X -> 0x%08X  (mode=%s)",
				baseaddr, (uptr)baseaddr + size, WX_STR(mode.ToString())));
			break;

		case EACCES:
			pxFailDev(pxsFmt(L"mprotect returned EACCES @ 0x%08X -> 0x%08X  (mode=%s)",
				baseaddr, (uptr)baseaddr + size, WX_STR(mode.ToString())));
			break;

		case ENOMEM:
			// caller handles assertion or exception, or whatever.
			break;
	}
	return false;
}

void* HostSys::MmapAllocatePtr(void* base, size_t size, const PageProtectionMode& mode)
{
	PageSizeAssertionTest(size);
	return mmap(base, size, LinuxProt(mode), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

void* HostSys::MmapAllocate(uptr base, size_t size, const PageProtectionMode& mode)
{
	return MmapAllocatePtr((void*)base, size, mode);
}

void* HostSys::Mmap(uptr base, size_t size)
{
	PageSizeAssertionTest(size);

	// MAP_ANONYMOUS - means we have no associated file handle (or device).

	return mmap((void*)base, size, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

void HostSys::Munmap(uptr base, size_t size)
{
	if (!base)
		return;
	munmap((void*)base, size);
}

void HostSys::MemProtect(void* baseaddr, size_t size, const PageProtectionMode& mode)
{
	if (!_memprotect(baseaddr, size, mode))
	{
		throw Exception::OutOfMemory(L"MemProtect")
			.SetDiagMsg(pxsFmt(L"mprotect failed @ 0x%08X -> 0x%08X  (mode=%s)",
				baseaddr, (uptr)baseaddr + size, WX_STR(mode.ToString())));
	}
}

wxString HostSys::GetFileMappingName(const char* prefix)
{
	const unsigned pid = static_cast<unsigned>(getpid());

	FastFormatAscii ret;
#ifdef LIBRETRO
	// libretro second-instance runahead is insane, and loads a second copy of the module in the same process, which means
	// we'd overlap the memory mapping for the "primary" core. Work around this by taking the address of this function,
	// which should be unique per instance.
	ret.Write("%s_%u_%p", prefix, pid, ((void*)&GetFileMappingName));
#elif __ANDROID__
	ret.Write("pcsx2");
#else
	ret.Write("pcsx2_%u", prefix, pid);
#endif

	return ret.GetString();
}

void* HostSys::CreateSharedMemory(const wxString& name, size_t size)
{
#if defined(__ANDROID__)
	// ASharedMemory path - works on API >= 26 and falls through on API < 26:

	// We can't call ASharedMemory_create the normal way without increasing the
	// minimum version requirement to API 26, so we use dlopen/dlsym instead
	static void* libandroid = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
	static auto shared_memory_create =
		reinterpret_cast<int (*)(const char*, size_t)>(dlsym(libandroid, "ASharedMemory_create"));
	int fd = -1;
	if (shared_memory_create)
		fd = shared_memory_create(name.c_str(), size);

	// /dev/ashmem path - works on API < 29:
	if (fd < 0)
	{
		fd = open("/dev/ashmem", O_RDWR);
		if (fd < 0)
			return nullptr;
	}

	// We don't really care if we can't set the name, it is optional
	ioctl(fd, ASHMEM_SET_NAME, static_cast<const char*>(name.c_str()));

	int ret = ioctl(fd, ASHMEM_SET_SIZE, size);
	if (ret < 0)
	{
		close(fd);
		std::fprintf(stderr, "Ashmem returned error: 0x%08x\n", ret);
		return nullptr;
	}

	return reinterpret_cast<void*>(static_cast<intptr_t>(fd));
#else
	const int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd < 0)
	{
		std::fprintf(stderr, "shm_open failed: %d\n", errno);
		return nullptr;
	}

	// we're not going to be opening this mapping in other processes, so remove the file
	shm_unlink(name.c_str());

	// ensure it's the correct size
	if (ftruncate64(fd, static_cast<off64_t>(size)) < 0)
	{
		std::fprintf(stderr, "ftruncate64(%zu) failed: %d\n", size, errno);
		return nullptr;
	}

	return reinterpret_cast<void*>(static_cast<intptr_t>(fd));
#endif
}

void HostSys::DestroySharedMemory(void* ptr)
{
	close(static_cast<int>(reinterpret_cast<intptr_t>(ptr)));
}

void* HostSys::ReserveSharedMemoryArea(size_t size)
{
	void* base_address = mmap(nullptr, size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (base_address == MAP_FAILED)
		pxFailRel("Failed to reserve fastmem area");

	return base_address;
}

void* HostSys::MapSharedMemory(void* handle, size_t offset, void* baseaddr, size_t size, const PageProtectionMode& mode)
{
	uint lnxmode = LinuxProt(mode);

	const int flags = (baseaddr != nullptr) ? (MAP_SHARED | MAP_FIXED) : MAP_SHARED;
	void* ptr = mmap(baseaddr, size, lnxmode, flags, static_cast<int>(reinterpret_cast<intptr_t>(handle)), static_cast<off_t>(offset));
	if (!ptr || ptr == reinterpret_cast<void*>(-1))
		return nullptr;

	return ptr;
}

void HostSys::UnmapSharedMemory(void* handle, void* baseaddr, size_t size)
{
	if (mmap(baseaddr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
		pxFailRel("Failed to unmap shared memory");
}

#endif
