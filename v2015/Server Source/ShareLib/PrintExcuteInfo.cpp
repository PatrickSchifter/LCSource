#include <string>
#include <fstream>
#include <stdlib.h>
#include <map>
#include <boost/assign/list_of.hpp>
#include "Conf.h"
#include "mysql.h"
#include "logsystem.h"
#include "PrintExcuteInfo.h"

#ifndef WIN32
#include <unistd.h>
#include <sys/utsname.h>
#include "svn_info.h"
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

#ifndef WIN32
namespace
{
#ifdef __APPLE__
	// macOS has no /proc: read the same information through sysctl and the mach host API.
	std::string SysctlString(const char* name)
	{
		size_t len = 0;
		if (sysctlbyname(name, NULL, &len, NULL, 0) != 0 || len == 0)
			return "";
		std::string value(len, '\0');
		if (sysctlbyname(name, &value[0], &len, NULL, 0) != 0)
			return "";
		value.resize(strnlen(value.c_str(), len));
		return value;
	}

	template <typename T>
	bool SysctlValue(const char* name, T& value)
	{
		size_t len = sizeof(value);
		return sysctlbyname(name, &value, &len, NULL, 0) == 0 && len == sizeof(value);
	}

	void AppendKb(std::string& out, const char* label, unsigned long long bytes)
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "%-16s%12llu kB\n", label, bytes / 1024);
		out += buf;
	}
#endif

	// Same lines the Linux build took from /proc/cpuinfo ("processor" / "model name").
	std::string GetCpuInfoString()
	{
		std::string outstr = "\n-- CPU INFORMATION --\n";
		outstr.reserve(10240);
#if defined(__linux__)
		std::string line;
		std::ifstream finfo("/proc/cpuinfo");
		while (std::getline(finfo, line))
		{
			if (line.find("processor", 0) != std::string::npos)
			{
				outstr += line + "\n";
			}
			else if (line.find("model name", 0) != std::string::npos)
			{
				outstr += line + "\n";
			}
		}
#elif defined(__APPLE__)
		std::string model = SysctlString("machdep.cpu.brand_string");
		if (model.empty())
			model = SysctlString("hw.model");
		int ncpu = 0, nphys = 0;
		if (!SysctlValue("hw.logicalcpu", ncpu))
			SysctlValue("hw.ncpu", ncpu);
		SysctlValue("hw.physicalcpu", nphys);
		char buf[256];
		for (int i = 0; i < ncpu; ++i)
		{
			snprintf(buf, sizeof(buf), "processor\t: %d\nmodel name\t: %s\n", i, model.c_str());
			outstr += buf;
		}
		snprintf(buf, sizeof(buf), "logical cpus\t: %d, physical cpus : %d\n", ncpu, nphys);
		outstr += buf;
#else
		outstr += "(not available on this platform)\n";
#endif
		return outstr;
	}

	// Linux: /proc/meminfo verbatim. macOS: the closest equivalents, in the same "Name: N kB" style.
	std::string GetMemInfoString()
	{
		std::string outstr = "\n-- MEMORY INFORMATION --\n";
		outstr.reserve(10240);
#if defined(__linux__)
		std::string line;
		std::ifstream finfo("/proc/meminfo");
		while (std::getline(finfo, line))
		{
			outstr += line + "\n";
		}
#elif defined(__APPLE__)
		unsigned long long memsize = 0;
		if (SysctlValue("hw.memsize", memsize))
			AppendKb(outstr, "MemTotal:", memsize);

		mach_port_t host = mach_host_self();
		vm_size_t page_size = 0;
		if (host_page_size(host, &page_size) != KERN_SUCCESS)
			page_size = 4096;

		vm_statistics64_data_t vm;
		mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
		if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm, &count) == KERN_SUCCESS)
		{
			unsigned long long ps = page_size;
			AppendKb(outstr, "MemFree:", (unsigned long long)(vm.free_count - vm.speculative_count) * ps);
			AppendKb(outstr, "MemAvailable:", (unsigned long long)(vm.free_count + vm.inactive_count + vm.purgeable_count) * ps);
			AppendKb(outstr, "Active:", (unsigned long long)vm.active_count * ps);
			AppendKb(outstr, "Inactive:", (unsigned long long)vm.inactive_count * ps);
			AppendKb(outstr, "Speculative:", (unsigned long long)vm.speculative_count * ps);
			AppendKb(outstr, "Wired:", (unsigned long long)vm.wire_count * ps);
			AppendKb(outstr, "Purgeable:", (unsigned long long)vm.purgeable_count * ps);
			AppendKb(outstr, "Compressed:", (unsigned long long)vm.compressor_page_count * ps);
			AppendKb(outstr, "FileBacked:", (unsigned long long)vm.external_page_count * ps);
			AppendKb(outstr, "Anonymous:", (unsigned long long)vm.internal_page_count * ps);
		}
		mach_port_deallocate(mach_task_self(), host);

		struct xsw_usage swap;
		if (SysctlValue("vm.swapusage", swap))
		{
			AppendKb(outstr, "SwapTotal:", swap.xsu_total);
			AppendKb(outstr, "SwapFree:", swap.xsu_avail);
		}
#else
		outstr += "(not available on this platform)\n";
#endif
		return outstr;
	}
}
#endif

PrintExcuteInfo::PrintExcuteInfo( void )
{
#ifdef _DEBUG
	debug_str_ = "DEBUG";
#else
	debug_str_ = "RELEASE";
#endif

	sig_no_ = -1;
}

PrintExcuteInfo* PrintExcuteInfo::Instance()
{
	static PrintExcuteInfo __instance;
	return &__instance;
}

void PrintExcuteInfo::PrintStart( const std::string server_name, const std::string national )
{
	server_name_ = server_name;
	national_ = national;

	LOG_INFO("\n\n\n");
	LOG_INFO("-------------------------- %s (%s / %s / %s) start ------------------------",
			 server_name_.c_str(), national_.c_str(), debug_str_.c_str(), test_str_.c_str());
	LOG_INFO("\n\n\n");

#ifndef WIN32
	LOG_INFO("svn info : %s", last_svn_revision);
	LOG_INFO("compile time : %d", compile_time ^ 0x12345678);

	// 이 프로세서가 열수 있는 최대 파일수
	LOG_INFO("openfiles = getdtablesize[%d]", getdtablesize());

	{
		// OS Name 출력하기
		struct utsname uts;
		uname(&uts);
		LOG_INFO("OS = sysname : %s, release : %s, version : %s, machine : %s",
				 uts.sysname, uts.release, uts.version, uts.machine);
	}

	LOG_INFO("%s", GetCpuInfoString().c_str());
	LOG_INFO("%s", GetMemInfoString().c_str());

#endif

	LOG_INFO("MYSQL CLIENT VERSION : %s", MYSQL_SERVER_VERSION);
}

void PrintExcuteInfo::PrintEnd()
{
	if (sig_no_ != -1)
	{
		PrintEndBySignal();
		return;
	}

	LOG_INFO("\n\n\n");
	LOG_INFO("-------------------------- %s (%s / %s / %s) end (%s) ------------------------",
			 server_name_.c_str(), national_.c_str(), debug_str_.c_str(), test_str_.c_str(), stop_message_.c_str());
	LOG_INFO("\n\n\n");
}

void PrintExcuteInfo::PrintEndBySignal()
{
	std::map<int, std::string> localmap = boost::assign::map_list_of(1, "SIGHUP")
										  (2, "SIGINT")
										  (3, "SIGQUIT")
										  (4, "SIGILL")
										  (6, "SIGABRT")
										  (7, "SIGBUS")
										  (8, "SIGFPE")
										  (9, "SIGKILL")
										  (11, "SIGSEGV")
										  (13, "SIGPIPE")
										  (14, "SIGALRM")
										  (15, "SIGTERM");

	std::string signame = "Unknown (to use man 7 signal)";
	std::map<int, std::string>::iterator it = localmap.find(sig_no_);
	if (it != localmap.end())
	{
		signame = it->second;
	}

	LOG_INFO("\n\n\n");
	LOG_INFO("-------------------------- %s (%s / %s / %s) end : signal %s(%d) ------------------------",
			 server_name_.c_str(), national_.c_str(), debug_str_.c_str(), test_str_.c_str(), signame.c_str(), sig_no_);
	LOG_INFO("\n\n\n");
}
