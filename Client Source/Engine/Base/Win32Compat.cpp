#include "stdh.h"

// Win32 functions emulated outside Windows (declared in Win32Compat.h). Compiled empty on Windows.
#ifndef _WIN32

#include <sys/types.h>
#include <sys/sysctl.h>
#ifdef __APPLE__
  #include <mach/mach.h>
#endif
#include <time.h>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

/////////////////////////////////////////////////////////////////////
// time

DWORD timeGetTime(void)
{
  static const std::chrono::steady_clock::time_point tpStart = std::chrono::steady_clock::now();
  const std::chrono::steady_clock::duration d = std::chrono::steady_clock::now() - tpStart;
  return (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

void Sleep(DWORD dwMilliseconds)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(dwMilliseconds));
}

BOOL QueryPerformanceCounter(LARGE_INTEGER *pliCount)
{
  pliCount->QuadPart = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER *pliFrequency)
{
  pliFrequency->QuadPart = 1000000000LL;
  return TRUE;
}

char *_strtime(char *strTime)
{
  time_t t = time(NULL);
  struct tm tmLocal;
  localtime_r(&t, &tmLocal);
  strftime(strTime, 9, "%H:%M:%S", &tmLocal);
  return strTime;
}

char *_strdate(char *strDate)
{
  time_t t = time(NULL);
  struct tm tmLocal;
  localtime_r(&t, &tmLocal);
  strftime(strDate, 9, "%m/%d/%y", &tmLocal);
  return strDate;
}

/////////////////////////////////////////////////////////////////////
// multimedia timers: a thread per timer; callbacks are serialized

namespace {

struct MMTimer {
  UINT uID;
  std::chrono::milliseconds msDelay;
  LPTIMECALLBACK pCallback;
  DWORD_PTR dwUser;
  BOOL bPeriodic;
  std::thread thread;
  std::mutex mx;
  std::condition_variable cv;
  BOOL bStop = FALSE;
};

std::mutex _mxTimers;          // guards _mapTimers and _uNextTimerID
std::recursive_mutex _mxCallbacks;  // one callback at a time
std::map<UINT, std::shared_ptr<MMTimer>> _mapTimers;
UINT _uNextTimerID = 1;

void TimerThread(std::shared_ptr<MMTimer> ptm)
{
  std::chrono::steady_clock::time_point tpNext = std::chrono::steady_clock::now() + ptm->msDelay;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(ptm->mx);
      if (ptm->cv.wait_until(lock, tpNext, [&] { return ptm->bStop != FALSE; })) {
        return;
      }
    }
    {
      std::lock_guard<std::recursive_mutex> lockCallbacks(_mxCallbacks);
      ptm->pCallback(ptm->uID, 0, ptm->dwUser, 0, 0);
    }
    if (!ptm->bPeriodic) {
      // a one-shot timer is gone once it fires
      std::lock_guard<std::mutex> lockTimers(_mxTimers);
      std::map<UINT, std::shared_ptr<MMTimer>>::iterator it = _mapTimers.find(ptm->uID);
      if (it != _mapTimers.end() && it->second == ptm) {
        ptm->thread.detach();
        _mapTimers.erase(it);
      }
      return;
    }
    // periodic: keep the original schedule, as winmm does
    tpNext += ptm->msDelay;
  }
}

}  // namespace

MMRESULT timeSetEvent(UINT uDelay, UINT uResolution, LPTIMECALLBACK lpTimeProc, DWORD_PTR dwUser, UINT fuEvent)
{
  (void)uResolution;
  std::shared_ptr<MMTimer> ptm = std::make_shared<MMTimer>();
  ptm->msDelay = std::chrono::milliseconds(uDelay);
  ptm->pCallback = lpTimeProc;
  ptm->dwUser = dwUser;
  ptm->bPeriodic = (fuEvent & TIME_PERIODIC) != 0;

  std::lock_guard<std::mutex> lockTimers(_mxTimers);
  ptm->uID = _uNextTimerID++;
  if (_uNextTimerID == 0) _uNextTimerID = 1;
  _mapTimers[ptm->uID] = ptm;
  // the thread may run (and a one-shot may try to remove itself) only after this lock is released
  ptm->thread = std::thread(TimerThread, ptm);
  return ptm->uID;
}

MMRESULT timeKillEvent(UINT uTimerID)
{
  std::shared_ptr<MMTimer> ptm;
  {
    std::lock_guard<std::mutex> lockTimers(_mxTimers);
    std::map<UINT, std::shared_ptr<MMTimer>>::iterator it = _mapTimers.find(uTimerID);
    if (it == _mapTimers.end()) {
      return MMSYSERR_INVALPARAM;
    }
    ptm = it->second;
    _mapTimers.erase(it);
  }
  {
    std::lock_guard<std::mutex> lock(ptm->mx);
    ptm->bStop = TRUE;
  }
  ptm->cv.notify_all();
  // killed from its own callback: the thread ends by itself after the callback returns
  if (ptm->thread.get_id() == std::this_thread::get_id()) {
    ptm->thread.detach();
  } else if (ptm->thread.joinable()) {
    ptm->thread.join();
  }
  return TIMERR_NOERROR;
}

/////////////////////////////////////////////////////////////////////
// files

#undef fopen
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdlib.h>
#include <unordered_map>
#include <vector>
#ifdef __APPLE__
  #include <mach-o/dyld.h>
#endif

namespace {

std::mutex _mxDirCache;
// directory (host path) -> its entries, by lowercase name
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> _mapDirCache;

std::string ToLower(const std::string &str)
{
  std::string strLower(str);
  for (size_t i = 0; i < strLower.size(); i++) {
    strLower[i] = (char)tolower((unsigned char)strLower[i]);
  }
  return strLower;
}

bool PathExists(const std::string &strPath)
{
  struct stat st;
  return lstat(strPath.c_str(), &st) == 0;
}

// the entry of a directory whose name matches ignoring case, or "" if none
std::string FindEntryIgnoringCase(const std::string &strDir, const std::string &strName)
{
  const std::string strKey = ToLower(strName);
  std::lock_guard<std::mutex> lock(_mxDirCache);
  for (int iPass = 0; iPass < 2; iPass++) {
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>::iterator it = _mapDirCache.find(strDir);
    // read the directory when not cached, or once more when the name is missing (it may be new)
    if (it == _mapDirCache.end() || iPass == 1) {
      std::unordered_map<std::string, std::string> mapEntries;
      DIR *pDir = opendir(strDir.empty() ? "." : strDir.c_str());
      if (pDir != NULL) {
        for (struct dirent *pde = readdir(pDir); pde != NULL; pde = readdir(pDir)) {
          mapEntries[ToLower(pde->d_name)] = pde->d_name;
        }
        closedir(pDir);
      }
      it = _mapDirCache.insert_or_assign(strDir, mapEntries).first;
    }
    std::unordered_map<std::string, std::string>::const_iterator itEntry = it->second.find(strKey);
    if (itEntry != it->second.end()) {
      return itEntry->second;
    }
  }
  return "";
}

}  // namespace

std::string LC_HostPath(const char *strPath)
{
  std::string strHost(strPath);
  for (size_t i = 0; i < strHost.size(); i++) {
    if (strHost[i] == '\\') strHost[i] = '/';
  }
  if (strHost.empty() || PathExists(strHost)) {
    return strHost;
  }

  // rebuild it component by component, fixing the case of those that exist with another case
  std::string strResult;
  size_t iPos = 0;
  if (strHost[0] == '/') {
    strResult = "/";
    iPos = 1;
  }
  bool bMissing = false;  // once a component is missing, the rest is kept as written
  while (iPos <= strHost.size()) {
    size_t iEnd = strHost.find('/', iPos);
    if (iEnd == std::string::npos) iEnd = strHost.size();
    const std::string strName = strHost.substr(iPos, iEnd - iPos);
    const bool bLast = iEnd == strHost.size();
    if (!strName.empty()) {
      std::string strCandidate = strResult + strName;
      if (!bMissing && strName != "." && strName != ".." && !PathExists(strCandidate)) {
        std::string strDir = strResult;  // "" for the current directory
        if (strDir.size() > 1) strDir.pop_back();  // without the trailing '/', except for the root
        const std::string strFound = FindEntryIgnoringCase(strDir, strName);
        if (strFound.empty()) {
          bMissing = true;
        } else {
          strCandidate = strResult + strFound;
        }
      }
      strResult = strCandidate;
      if (!bLast) strResult += "/";
    } else if (!bLast) {
      // keep repeated separators as they are
      strResult += "/";
    }
    iPos = iEnd + 1;
  }
  return strResult;
}

FILE *LC_fopen(const char *strFileName, const char *strMode)
{
  return fopen(LC_HostPath(strFileName).c_str(), strMode);
}

int _open(const char *strFileName, int iFlags)
{
  return open(LC_HostPath(strFileName).c_str(), iFlags);
}

namespace {

struct FindData {
  DIR *pDir;
  std::string strDir;      // host path of the directory, with a trailing '/'
  std::string strPattern;  // file name pattern (* and ?), matched ignoring case
};

bool FindNextEntry(FindData &fd, struct _finddata_t *pfd)
{
  for (struct dirent *pde = readdir(fd.pDir); pde != NULL; pde = readdir(fd.pDir)) {
    if (fnmatch(fd.strPattern.c_str(), pde->d_name, FNM_CASEFOLD) != 0) {
      continue;
    }
    struct stat st;
    memset(pfd, 0, sizeof(*pfd));
    if (stat((fd.strDir + pde->d_name).c_str(), &st) == 0) {
      pfd->attrib = S_ISDIR(st.st_mode) ? _A_SUBDIR : _A_ARCH;
      if (!(st.st_mode & S_IWUSR)) pfd->attrib |= _A_RDONLY;
      pfd->time_create = st.st_ctime;
      pfd->time_access = st.st_atime;
      pfd->time_write = st.st_mtime;
      pfd->size = (size_t)st.st_size;
    }
    strncpy(pfd->name, pde->d_name, sizeof(pfd->name) - 1);
    return true;
  }
  return false;
}

}  // namespace

intptr_t _findfirst(const char *strPattern, struct _finddata_t *pfd)
{
  std::string strHost(strPattern);
  for (size_t i = 0; i < strHost.size(); i++) {
    if (strHost[i] == '\\') strHost[i] = '/';
  }
  const size_t iSlash = strHost.rfind('/');
  FindData *pfdNew = new FindData;
  pfdNew->strPattern = (iSlash == std::string::npos) ? strHost : strHost.substr(iSlash + 1);
  pfdNew->strDir = (iSlash == std::string::npos) ? "" : LC_HostPath(strHost.substr(0, iSlash).c_str()) + "/";
  if (iSlash == 0) pfdNew->strDir = "/";
  pfdNew->pDir = opendir(pfdNew->strDir.empty() ? "." : pfdNew->strDir.c_str());
  if (pfdNew->pDir == NULL || !FindNextEntry(*pfdNew, pfd)) {
    if (pfdNew->pDir != NULL) closedir(pfdNew->pDir);
    delete pfdNew;
    errno = ENOENT;
    return -1;
  }
  return (intptr_t)pfdNew;
}

int _findnext(intptr_t hFind, struct _finddata_t *pfd)
{
  if (hFind == -1) return -1;
  return FindNextEntry(*(FindData*)hFind, pfd) ? 0 : -1;
}

int _findclose(intptr_t hFind)
{
  if (hFind == -1) return -1;
  FindData *pfd = (FindData*)hFind;
  closedir(pfd->pDir);
  delete pfd;
  return 0;
}

DWORD GetFileAttributes(const char *strFileName)
{
  struct stat st;
  if (stat(LC_HostPath(strFileName).c_str(), &st) != 0) {
    return INVALID_FILE_ATTRIBUTES;
  }
  DWORD dwAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
  if (!(st.st_mode & S_IWUSR)) dwAttributes |= FILE_ATTRIBUTE_READONLY;
  return dwAttributes;
}

BOOL CreateDirectory(const char *strPathName, void *pSecurityAttributes)
{
  (void)pSecurityAttributes;
  return _mkdir(strPathName) == 0;
}

int _mkdir(const char *strPathName)
{
  return mkdir(LC_HostPath(strPathName).c_str(), 0755);
}

BOOL DeleteFile(const char *strFileName)
{
  return unlink(LC_HostPath(strFileName).c_str()) == 0;
}

BOOL MoveFile(const char *strExisting, const char *strNew)
{
  // MoveFile fails if the destination exists; rename would replace it
  const std::string strHostNew = LC_HostPath(strNew);
  if (PathExists(strHostNew)) {
    return FALSE;
  }
  return rename(LC_HostPath(strExisting).c_str(), strHostNew.c_str()) == 0;
}

DWORD GetModuleFileName(HMODULE hModule, char *strFileName, DWORD ctSize)
{
  ASSERT(hModule == NULL);
  if (ctSize == 0) return 0;
  char strExe[PATH_MAX] = "";
#ifdef __APPLE__
  uint32_t ctExe = sizeof(strExe);
  if (_NSGetExecutablePath(strExe, &ctExe) != 0) {
    strFileName[0] = 0;
    return 0;
  }
#endif
  char strReal[PATH_MAX];
  const char *strPath = realpath(strExe, strReal) != NULL ? strReal : strExe;
  // as a Windows path, like every other path inside the engine
  DWORD ctCopied = 0;
  for (; strPath[ctCopied] != 0 && ctCopied < ctSize - 1; ctCopied++) {
    strFileName[ctCopied] = strPath[ctCopied] == '/' ? '\\' : strPath[ctCopied];
  }
  strFileName[ctCopied] = 0;
  return ctCopied;
}

/////////////////////////////////////////////////////////////////////
// memory

static DWORD ClampToDWORD(uint64_t ullValue)
{
  return ullValue > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (DWORD)ullValue;
}

void GlobalMemoryStatus(LPMEMORYSTATUS pms)
{
  memset(pms, 0, sizeof(*pms));
  pms->dwLength = sizeof(*pms);

  uint64_t ullTotal = 0;
  uint64_t ullAvail = 0;
#ifdef __APPLE__
  size_t ctSize = sizeof(ullTotal);
  sysctlbyname("hw.memsize", &ullTotal, &ctSize, NULL, 0);
  // free and inactive (reclaimable) pages, as Activity Monitor counts available memory
  vm_statistics64_data_t vms;
  mach_msg_type_number_t ctInfo = HOST_VM_INFO64_COUNT;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vms, &ctInfo) == KERN_SUCCESS) {
    ullAvail = (uint64_t)(vms.free_count + vms.inactive_count) * (uint64_t)vm_kernel_page_size;
  }
#endif
  if (ullAvail > ullTotal) {
    ullAvail = ullTotal;
  }
  pms->dwMemoryLoad = ullTotal > 0 ? (DWORD)((ullTotal - ullAvail) * 100 / ullTotal) : 0;
  pms->dwTotalPhys = ClampToDWORD(ullTotal);
  pms->dwAvailPhys = ClampToDWORD(ullAvail);
}

#endif  // !_WIN32
