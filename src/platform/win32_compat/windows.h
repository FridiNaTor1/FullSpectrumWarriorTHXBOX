/*
 * Minimal Win32 compatibility shim for native Linux builds.
 *
 * This is intentionally small and only covers the subset used by the
 * xboxrecomp runtime. It is not a general Windows SDK replacement.
 */
#ifndef XBOXRECOMP_WINDOWS_COMPAT_H
#define XBOXRECOMP_WINDOWS_COMPAT_H

#ifdef _WIN32
#include_next <windows.h>
#else

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef __fastcall
#define __fastcall
#endif
#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef __declspec
#define __declspec(x) __##x
#endif

typedef int BOOL;
typedef uint8_t BYTE, UCHAR, BOOLEAN;
typedef int8_t CCHAR;
typedef uint16_t WORD, USHORT;
typedef int16_t SHORT;
typedef uint32_t DWORD, ULONG, UINT, ACCESS_MASK;
typedef uint32_t UINT32;
typedef int32_t LONG, HRESULT, INT;
typedef uint64_t DWORDLONG, ULONGLONG;
typedef int64_t LONGLONG;
typedef uintptr_t ULONG_PTR;
typedef intptr_t LONG_PTR;
typedef size_t SIZE_T, *PSIZE_T;
typedef void VOID, *PVOID, *LPVOID;
typedef const void *LPCVOID;
typedef char CHAR, *PCHAR, *LPSTR;
typedef wchar_t WCHAR, *PWCHAR, *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef const CHAR *LPCSTR;
typedef ULONG *PULONG;
typedef LONG *PLONG;
typedef USHORT *PUSHORT;
typedef UCHAR *PUCHAR;
typedef BYTE *PBYTE;

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct { DWORD LowPart; DWORD HighPart; };
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay;
    WORD wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;

typedef struct _RECT {
    LONG left, top, right, bottom;
} RECT;

typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID, IID;

typedef void *HINSTANCE;
typedef void *HWND;
typedef void *HMODULE;
typedef void *HANDLE;
typedef HANDLE *PHANDLE;

#define TRUE 1
#define FALSE 0
#define NULL_HANDLE ((HANDLE)0)
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu

#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005u)
#define E_OUTOFMEMORY ((HRESULT)0x8007000Eu)
#define E_INVALIDARG ((HRESULT)0x80070057u)
#define E_NOINTERFACE ((HRESULT)0x80004002u)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)

#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_PARAMETER 87
#define ERROR_ALREADY_EXISTS 183
#define ERROR_HANDLE_EOF 38
#define ERROR_DEVICE_NOT_CONNECTED 1167
#define ERROR_NO_SYSTEM_RESOURCES 1450
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_GEN_FAILURE 31
#define ERROR_IO_PENDING 997
#define ERROR_MORE_DATA 234
#define ERROR_NO_MORE_FILES 18
#define ERROR_NOT_SUPPORTED 50
#define ERROR_CANCELLED 1223
#define ERROR_COMMITMENT_LIMIT 1455
#define ERROR_MR_MID_NOT_FOUND 317

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define GENERIC_ALL 0x10000000u
#define FILE_READ_DATA 0x0001
#define FILE_WRITE_DATA 0x0002
#define FILE_APPEND_DATA 0x0004
#define FILE_READ_ATTRIBUTES 0x0080
#define FILE_WRITE_ATTRIBUTES 0x0100
#define SYNCHRONIZE 0x00100000u
#define DELETE 0x00010000u

#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define FILE_SHARE_DELETE 0x00000004u

#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5

#define FILE_ATTRIBUTE_READONLY 0x00000001u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000u
#define FILE_FLAG_NO_BUFFERING 0x20000000u

#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define MEM_COMMIT 0x00001000u
#define MEM_RESERVE 0x00002000u
#define MEM_RELEASE 0x00008000u
#define MEM_DECOMMIT 0x00004000u
#define FILE_MAP_ALL_ACCESS 0x000f001fu

#define WAIT_OBJECT_0 0
#define WAIT_ABANDONED_0 0x80
#define WAIT_TIMEOUT 258
#define WAIT_FAILED 0xFFFFFFFFu
#define WAIT_IO_COMPLETION 192

#define DUPLICATE_CLOSE_SOURCE 0x00000001u
#define DUPLICATE_SAME_ACCESS 0x00000002u
#define CREATE_SUSPENDED 0x00000004u

#define FILE_BEGIN SEEK_SET
#define FILE_CURRENT SEEK_CUR
#define FILE_END SEEK_END
#define FILE_NAME_NORMALIZED 0

#define CP_ACP 0
#define CP_UTF8 65001

#define THREAD_PRIORITY_IDLE (-15)
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15

#define WT_EXECUTEONLYONCE 0x00000008u

#define EXCEPTION_ACCESS_VIOLATION 0xC0000005u
#define EXCEPTION_CONTINUE_SEARCH 0

typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    DWORD Offset;
    DWORD OffsetHigh;
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION;

typedef struct _MEMORYSTATUSEX {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    DWORDLONG ullTotalPhys;
    DWORDLONG ullAvailPhys;
    DWORDLONG ullTotalPageFile;
    DWORDLONG ullAvailPageFile;
    DWORDLONG ullTotalVirtual;
    DWORDLONG ullAvailVirtual;
    DWORDLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;

typedef struct _BY_HANDLE_FILE_INFORMATION {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD dwVolumeSerialNumber;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD nNumberOfLinks;
    DWORD nFileIndexHigh;
    DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;

typedef struct _WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    WCHAR cFileName[MAX_PATH];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

typedef struct _FILE_NAME_INFO {
    DWORD FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFO;

typedef struct _FILE_DISPOSITION_INFO {
    BOOL DeleteFile;
} FILE_DISPOSITION_INFO;

typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard = 0 } GET_FILEEX_INFO_LEVELS;
typedef enum _FILE_INFO_BY_HANDLE_CLASS {
    FileBasicInfo = 0,
    FileStandardInfo = 1,
    FileNameInfo = 2,
    FileDispositionInfo = 4
} FILE_INFO_BY_HANDLE_CLASS;

typedef struct _CONTEXT {
    uint64_t Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi;
    uint64_t R8, R9, R10, R11, R12, R13, R14, R15;
    uint64_t Rip;
    uint32_t EFlags;
} CONTEXT, *PCONTEXT;

typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

typedef struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

typedef LONG (CALLBACK *PVECTORED_EXCEPTION_HANDLER)(PEXCEPTION_POINTERS);

typedef pthread_mutex_t CRITICAL_SECTION;
typedef CRITICAL_SECTION RTL_CRITICAL_SECTION, *PRTL_CRITICAL_SECTION;
typedef pthread_cond_t CONDITION_VARIABLE;

typedef void (CALLBACK *PTP_TIMER_CALLBACK)(PVOID, BOOLEAN);
typedef void *PTP_CALLBACK_INSTANCE;
typedef void (CALLBACK *PTP_SIMPLE_CALLBACK)(PTP_CALLBACK_INSTANCE, PVOID);

typedef struct xboxrecomp_handle {
    int type;
    int fd;
    int manual_reset;
    int signaled;
    int delete_on_close;
    size_t size;
    void *map_base;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    void *thread_arg;
    DWORD (*thread_proc)(void *);
    char path[PATH_MAX];
    DIR *dir;
    char dir_path[PATH_MAX];
} xboxrecomp_handle;

enum {
    XH_FILE = 1,
    XH_EVENT,
    XH_SEMAPHORE,
    XH_MAPPING,
    XH_THREAD,
    XH_FIND,
    XH_TIMER
};

static inline DWORD xboxrecomp_last_error_from_errno(void) {
    switch (errno) {
        case ENOENT: return ERROR_FILE_NOT_FOUND;
        case ENOTDIR: return ERROR_PATH_NOT_FOUND;
        case EACCES:
        case EPERM: return ERROR_ACCESS_DENIED;
        case EEXIST: return ERROR_ALREADY_EXISTS;
        default: return (DWORD)errno;
    }
}

static inline DWORD GetLastError(void) { return xboxrecomp_last_error_from_errno(); }
static inline void SetLastError(DWORD err) { errno = (int)err; }

static inline xboxrecomp_handle *xh_alloc(int type) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->type = type;
    h->fd = -1;
    pthread_mutex_init(&h->mutex, NULL);
    pthread_cond_init(&h->cond, NULL);
    return h;
}

static inline char *xw_to_utf8(const WCHAR *w, char *buf, size_t n) {
    if (!w) {
        if (buf && n) buf[0] = '\0';
        return buf;
    }
    size_t out = wcstombs(buf, w, n ? n - 1 : 0);
    if (out == (size_t)-1) {
        out = 0;
        while (out + 1 < n && w[out]) {
            buf[out] = (char)(w[out] & 0x7f);
            out++;
        }
    }
    if (n) buf[out] = '\0';
    for (size_t i = 0; buf && buf[i]; i++) {
        if (buf[i] == '\\') {
            buf[i] = '/';
        }
    }
    return buf;
}

static inline void xmkdir_parent_dirs(const char *path) {
    char tmp[PATH_MAX];
    size_t len;

    if (!path || !path[0]) return;
    len = strlen(path);
    if (len >= sizeof(tmp)) return;

    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

static inline void xutf8_to_w(const char *s, WCHAR *buf, size_t n) {
    if (!buf || n == 0) return;
    size_t out = mbstowcs(buf, s ? s : "", n - 1);
    if (out == (size_t)-1) {
        out = 0;
        while (out + 1 < n && s && s[out]) {
            buf[out] = (WCHAR)(unsigned char)s[out];
            out++;
        }
    }
    buf[out] = L'\0';
}

static inline int xprotect_to_posix(DWORD protect) {
    switch (protect & 0xff) {
        case PAGE_NOACCESS: return PROT_NONE;
        case PAGE_READONLY: return PROT_READ;
        case PAGE_EXECUTE: return PROT_EXEC;
        case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
        case PAGE_EXECUTE_READWRITE: return PROT_READ | PROT_WRITE | PROT_EXEC;
        default: return PROT_READ | PROT_WRITE;
    }
}

static inline LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD allocation_type, DWORD protect) {
    (void)allocation_type;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (address) {
#ifdef MAP_FIXED_NOREPLACE
        flags |= MAP_FIXED_NOREPLACE;
#else
        flags |= MAP_FIXED;
#endif
    }
    void *p = mmap(address, size, xprotect_to_posix(protect), flags, -1, 0);
    if (p == MAP_FAILED) {
        if (address) {
            p = mmap(NULL, size, xprotect_to_posix(protect), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        }
        if (p == MAP_FAILED) return NULL;
    }
    return p;
}

static inline BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD free_type) {
    (void)address; (void)size; (void)free_type;
    return TRUE;
}

static inline SIZE_T xboxrecomp_page_size(void) {
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? (SIZE_T)ps : 4096;
}

static inline BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD protect, DWORD *old_protect) {
    if (old_protect) *old_protect = PAGE_READWRITE;
    uintptr_t page = (uintptr_t)address & ~(xboxrecomp_page_size() - 1);
    SIZE_T end = ((uintptr_t)address + size + xboxrecomp_page_size() - 1) & ~(xboxrecomp_page_size() - 1);
    return mprotect((void *)page, end - page, xprotect_to_posix(protect)) == 0;
}

static inline SIZE_T VirtualQuery(LPCVOID address, MEMORY_BASIC_INFORMATION *mbi, SIZE_T length) {
    if (!mbi || length < sizeof(*mbi)) return 0;
    memset(mbi, 0, sizeof(*mbi));
    mbi->BaseAddress = (PVOID)((uintptr_t)address & ~(xboxrecomp_page_size() - 1));
    mbi->AllocationBase = mbi->BaseAddress;
    mbi->AllocationProtect = PAGE_READWRITE;
    mbi->RegionSize = xboxrecomp_page_size();
    mbi->State = MEM_COMMIT;
    mbi->Protect = PAGE_READWRITE;
    return sizeof(*mbi);
}

static inline HANDLE GetProcessHeap(void) { return (HANDLE)(uintptr_t)1; }
static inline LPVOID HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size) {
    (void)heap;
    return (flags ? calloc(1, size) : malloc(size));
}
static inline BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID mem) {
    (void)heap; (void)flags; free(mem); return TRUE;
}
static inline SIZE_T HeapSize(HANDLE heap, DWORD flags, LPCVOID mem) {
    (void)heap; (void)flags; return mem ? malloc_usable_size((void *)mem) : (SIZE_T)-1;
}

#define HEAP_ZERO_MEMORY 0x00000008u

static inline void InitializeCriticalSection(CRITICAL_SECTION *cs) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(cs, &attr);
    pthread_mutexattr_destroy(&attr);
}
static inline void DeleteCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_destroy(cs); }
static inline void EnterCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_lock(cs); }
static inline void LeaveCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_unlock(cs); }
static inline void InitializeConditionVariable(CONDITION_VARIABLE *cv) { pthread_cond_init(cv, NULL); }
static inline void WakeConditionVariable(CONDITION_VARIABLE *cv) { pthread_cond_signal(cv); }
static inline void WakeAllConditionVariable(CONDITION_VARIABLE *cv) { pthread_cond_broadcast(cv); }
static inline BOOL SleepConditionVariableCS(CONDITION_VARIABLE *cv, CRITICAL_SECTION *cs, DWORD ms) {
    if (ms == INFINITE) return pthread_cond_wait(cv, cs) == 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(cv, cs, &ts) == 0;
}

static inline LONG InterlockedIncrement(volatile LONG *v) { return __sync_add_and_fetch(v, 1); }
static inline LONG InterlockedDecrement(volatile LONG *v) { return __sync_sub_and_fetch(v, 1); }

static inline void Sleep(DWORD ms) { usleep((useconds_t)ms * 1000); }
static inline DWORD SleepEx(DWORD ms, BOOL alertable) { (void)alertable; Sleep(ms); return 0; }
static inline BOOL SwitchToThread(void) { return sched_yield() == 0; }

static inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *li) {
    if (li) li->QuadPart = 1000000000LL;
    return TRUE;
}
static inline BOOL QueryPerformanceCounter(LARGE_INTEGER *li) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (li) li->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}

static inline DWORD GetTickCount(void) {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return (DWORD)(li.QuadPart / 1000000LL);
}
static inline ULONGLONG GetTickCount64(void) {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return (ULONGLONG)(li.QuadPart / 1000000LL);
}

static inline void unix_time_to_filetime(time_t t, FILETIME *ft) {
    uint64_t v = ((uint64_t)t + 11644473600ULL) * 10000000ULL;
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

static inline void GetSystemTimeAsFileTime(LPFILETIME ft) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t v = ((uint64_t)ts.tv_sec + 11644473600ULL) * 10000000ULL + (uint64_t)(ts.tv_nsec / 100);
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

static inline void GetLocalTime(SYSTEMTIME *st) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t t = ts.tv_sec;
    struct tm tmv;
    localtime_r(&t, &tmv);
    st->wYear = (WORD)(tmv.tm_year + 1900);
    st->wMonth = (WORD)(tmv.tm_mon + 1);
    st->wDayOfWeek = (WORD)tmv.tm_wday;
    st->wDay = (WORD)tmv.tm_mday;
    st->wHour = (WORD)tmv.tm_hour;
    st->wMinute = (WORD)tmv.tm_min;
    st->wSecond = (WORD)tmv.tm_sec;
    st->wMilliseconds = (WORD)(ts.tv_nsec / 1000000L);
}

static inline BOOL SystemTimeToFileTime(const SYSTEMTIME *st, FILETIME *ft) {
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = st->wYear - 1900;
    tmv.tm_mon = st->wMonth - 1;
    tmv.tm_mday = st->wDay;
    tmv.tm_hour = st->wHour;
    tmv.tm_min = st->wMinute;
    tmv.tm_sec = st->wSecond;
    time_t t = timegm(&tmv);
    if (t == (time_t)-1) return FALSE;
    unix_time_to_filetime(t, ft);
    return TRUE;
}

static inline BOOL FileTimeToSystemTime(const FILETIME *ft, SYSTEMTIME *st) {
    uint64_t v = ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    time_t t = (time_t)(v / 10000000ULL - 11644473600ULL);
    struct tm tmv;
    if (!gmtime_r(&t, &tmv)) return FALSE;
    st->wYear = (WORD)(tmv.tm_year + 1900);
    st->wMonth = (WORD)(tmv.tm_mon + 1);
    st->wDayOfWeek = (WORD)tmv.tm_wday;
    st->wDay = (WORD)tmv.tm_mday;
    st->wHour = (WORD)tmv.tm_hour;
    st->wMinute = (WORD)tmv.tm_min;
    st->wSecond = (WORD)tmv.tm_sec;
    st->wMilliseconds = (WORD)((v / 10000ULL) % 1000ULL);
    return TRUE;
}

static inline BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX ms) {
    struct sysinfo si;
    if (!ms || sysinfo(&si) != 0) return FALSE;
    ms->ullTotalPhys = (DWORDLONG)si.totalram * si.mem_unit;
    ms->ullAvailPhys = (DWORDLONG)si.freeram * si.mem_unit;
    return TRUE;
}

static inline int MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int src_len, LPWSTR dst, int dst_len) {
    (void)cp; (void)flags;
    if (!src || !dst || dst_len <= 0) return 0;
    size_t n = src_len < 0 ? strlen(src) : (size_t)src_len;
    if (n >= (size_t)dst_len) n = (size_t)dst_len - 1;
    for (size_t i = 0; i < n; i++) dst[i] = (WCHAR)(unsigned char)src[i];
    dst[n] = L'\0';
    return (int)n;
}

static inline int WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR src, int src_len, LPSTR dst, int dst_len, LPCSTR def, BOOL *used) {
    (void)cp; (void)flags; (void)def; if (used) *used = FALSE;
    if (!src || !dst || dst_len <= 0) return 0;
    size_t n = src_len < 0 ? wcslen(src) : (size_t)src_len;
    if (n >= (size_t)dst_len) n = (size_t)dst_len - 1;
    for (size_t i = 0; i < n; i++) dst[i] = (char)(src[i] & 0xff);
    dst[n] = '\0';
    return (int)n;
}

static inline DWORD GetCurrentDirectoryW(DWORD len, LPWSTR out) {
    char cwd[PATH_MAX];
    if (!out || len == 0 || !getcwd(cwd, sizeof(cwd))) return 0;
    xutf8_to_w(cwd, out, len);
    return (DWORD)wcslen(out);
}

static inline int wcscat_s(LPWSTR dst, size_t dst_count, LPCWSTR src) {
    if (!dst || !src) return E_INVALIDARG;
    if (wcslen(dst) + wcslen(src) + 1 > dst_count) return E_INVALIDARG;
    wcscat(dst, src);
    return 0;
}

#define swprintf_s swprintf

static inline HANDLE CreateFileMappingA(HANDLE file, void *sa, DWORD protect, DWORD high, DWORD low, LPCSTR name) {
    (void)file; (void)sa; (void)protect; (void)high; (void)name;
    xboxrecomp_handle *h = xh_alloc(XH_MAPPING);
    if (!h) return NULL;
    h->size = low;
#ifdef SYS_memfd_create
    h->fd = (int)syscall(SYS_memfd_create, "xboxrecomp-memory", 0);
#else
    h->fd = -1;
#endif
    if (h->fd < 0) {
        char tmpl[] = "/tmp/xboxrecomp-memory-XXXXXX";
        h->fd = mkstemp(tmpl);
        unlink(tmpl);
    }
    if (h->fd < 0 || ftruncate(h->fd, (off_t)h->size) != 0) {
        if (h->fd >= 0) close(h->fd);
        free(h);
        return NULL;
    }
    return (HANDLE)h;
}

static inline LPVOID MapViewOfFileEx(HANDLE mapping, DWORD access, DWORD off_high, DWORD off_low, SIZE_T size, LPVOID base) {
    (void)access;
    xboxrecomp_handle *h = (xboxrecomp_handle *)mapping;
    if (!h || h->type != XH_MAPPING) return NULL;
    int flags = MAP_SHARED;
    if (base) {
#ifdef MAP_FIXED_NOREPLACE
        flags |= MAP_FIXED_NOREPLACE;
#else
        flags |= MAP_FIXED;
#endif
    }
    off_t off = ((off_t)off_high << 32) | off_low;
    void *p = mmap(base, size ? size : h->size, PROT_READ | PROT_WRITE, flags, h->fd, off);
    if (p == MAP_FAILED) return NULL;
    return p;
}

static inline BOOL UnmapViewOfFile(LPCVOID base) { return base ? (munmap((void *)base, 0) == 0 || TRUE) : FALSE; }

static inline HANDLE CreateEventW(void *sa, BOOL manual, BOOL initial, LPCWSTR name) {
    (void)sa; (void)name;
    xboxrecomp_handle *h = xh_alloc(XH_EVENT);
    if (!h) return NULL;
    h->manual_reset = manual;
    h->signaled = initial;
    return (HANDLE)h;
}

static inline BOOL SetEvent(HANDLE handle) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h) return FALSE;
    pthread_mutex_lock(&h->mutex);
    h->signaled = 1;
    if (h->manual_reset) pthread_cond_broadcast(&h->cond);
    else pthread_cond_signal(&h->cond);
    pthread_mutex_unlock(&h->mutex);
    return TRUE;
}

static inline BOOL ResetEvent(HANDLE handle) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h) return FALSE;
    pthread_mutex_lock(&h->mutex);
    h->signaled = 0;
    pthread_mutex_unlock(&h->mutex);
    return TRUE;
}

static inline HANDLE CreateSemaphoreW(void *sa, LONG initial, LONG maximum, LPCWSTR name) {
    (void)sa; (void)name;
    xboxrecomp_handle *h = xh_alloc(XH_SEMAPHORE);
    if (!h) return NULL;
    h->signaled = initial;
    h->manual_reset = maximum;
    return (HANDLE)h;
}

static inline BOOL ReleaseSemaphore(HANDLE handle, LONG release, PLONG prev) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h) return FALSE;
    pthread_mutex_lock(&h->mutex);
    if (prev) *prev = h->signaled;
    h->signaled += release;
    if (h->signaled > h->manual_reset) h->signaled = h->manual_reset;
    pthread_cond_broadcast(&h->cond);
    pthread_mutex_unlock(&h->mutex);
    return TRUE;
}

static inline DWORD WaitForSingleObjectEx(HANDLE handle, DWORD ms, BOOL alertable) {
    (void)alertable;
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h) return WAIT_FAILED;
    if (h->type == XH_THREAD) {
        pthread_join(h->thread, NULL);
        return WAIT_OBJECT_0;
    }
    pthread_mutex_lock(&h->mutex);
    if (!h->signaled && ms == 0) {
        pthread_mutex_unlock(&h->mutex);
        return WAIT_TIMEOUT;
    }
    while (!h->signaled) {
        if (ms == INFINITE) {
            pthread_cond_wait(&h->cond, &h->mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ms / 1000;
            ts.tv_nsec += (long)(ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            if (pthread_cond_timedwait(&h->cond, &h->mutex, &ts) == ETIMEDOUT) {
                pthread_mutex_unlock(&h->mutex);
                return WAIT_TIMEOUT;
            }
        }
    }
    if (h->type == XH_EVENT && !h->manual_reset) h->signaled = 0;
    if (h->type == XH_SEMAPHORE && h->signaled > 0) h->signaled--;
    pthread_mutex_unlock(&h->mutex);
    return WAIT_OBJECT_0;
}

static inline DWORD WaitForSingleObject(HANDLE handle, DWORD ms) { return WaitForSingleObjectEx(handle, ms, FALSE); }

static inline DWORD WaitForMultipleObjectsEx(DWORD count, HANDLE *handles, BOOL wait_all, DWORD ms, BOOL alertable) {
    (void)alertable;
    if (wait_all) {
        for (DWORD i = 0; i < count; i++) {
            DWORD r = WaitForSingleObjectEx(handles[i], ms, FALSE);
            if (r != WAIT_OBJECT_0) return r;
        }
        return WAIT_OBJECT_0;
    }
    for (;;) {
        for (DWORD i = 0; i < count; i++) {
            if (WaitForSingleObjectEx(handles[i], 0, FALSE) == WAIT_OBJECT_0) {
                return WAIT_OBJECT_0 + i;
            }
        }
        if (ms == 0) return WAIT_TIMEOUT;
        Sleep(1);
        if (ms != INFINITE && --ms == 0) return WAIT_TIMEOUT;
    }
}

static inline DWORD WaitForMultipleObjects(DWORD count, HANDLE *handles, BOOL wait_all, DWORD ms) {
    return WaitForMultipleObjectsEx(count, handles, wait_all, ms, FALSE);
}

static inline void *xthread_trampoline(void *arg) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)arg;
    if (h->thread_proc) h->thread_proc(h->thread_arg);
    return NULL;
}

typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);

static inline HANDLE CreateThread(void *sa, SIZE_T stack, LPTHREAD_START_ROUTINE proc, LPVOID arg, DWORD flags, DWORD *thread_id) {
    (void)sa; (void)stack; (void)flags;
    xboxrecomp_handle *h = xh_alloc(XH_THREAD);
    if (!h) return NULL;
    h->thread_proc = proc;
    h->thread_arg = arg;
    if (pthread_create(&h->thread, NULL, xthread_trampoline, h) != 0) {
        free(h);
        return NULL;
    }
    if (thread_id) *thread_id = (DWORD)(uintptr_t)h->thread;
    return (HANDLE)h;
}

static inline void ExitThread(DWORD code) { pthread_exit((void *)(uintptr_t)code); }
static inline DWORD GetCurrentThreadId(void) { return (DWORD)(uintptr_t)pthread_self(); }
static inline HANDLE GetCurrentProcess(void) { return (HANDLE)(uintptr_t)2; }
static inline int GetThreadPriority(HANDLE h) { (void)h; return THREAD_PRIORITY_NORMAL; }
static inline BOOL SetThreadPriority(HANDLE h, int p) { (void)h; (void)p; return TRUE; }
static inline BOOL QueueUserAPC(void (CALLBACK *fn)(ULONG_PTR), HANDLE h, ULONG_PTR data) { (void)h; if (fn) fn(data); return TRUE; }
static inline BOOL DuplicateHandle(HANDLE src_proc, HANDLE src, HANDLE dst_proc, PHANDLE dst, DWORD access, BOOL inherit, DWORD opts) {
    (void)src_proc; (void)dst_proc; (void)access; (void)inherit; (void)opts;
    if (!dst) return FALSE;
    *dst = src;
    return TRUE;
}

static inline BOOL CloseHandle(HANDLE handle) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    if (h->type == XH_FILE && h->fd >= 0) {
        close(h->fd);
        if (h->delete_on_close) unlink(h->path);
    } else if (h->type == XH_MAPPING && h->fd >= 0) {
        close(h->fd);
    } else if (h->type == XH_FIND && h->dir) {
        closedir(h->dir);
    }
    pthread_mutex_destroy(&h->mutex);
    pthread_cond_destroy(&h->cond);
    free(h);
    return TRUE;
}

static inline int xaccess_to_open(DWORD access) {
    if ((access & GENERIC_WRITE) || (access & FILE_WRITE_DATA)) {
        return ((access & GENERIC_READ) || (access & FILE_READ_DATA)) ? O_RDWR : O_WRONLY;
    }
    return O_RDONLY;
}

static inline HANDLE CreateFileW(LPCWSTR pathw, DWORD access, DWORD share, void *sa, DWORD disp, DWORD attrs, HANDLE templ) {
    (void)share; (void)sa; (void)attrs; (void)templ;
    char path[PATH_MAX];
    xw_to_utf8(pathw, path, sizeof(path));
    int flags = xaccess_to_open(access);
    switch (disp) {
        case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
        case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
        case OPEN_ALWAYS: flags |= O_CREAT; break;
        case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
        default: break;
    }
    if (flags & O_CREAT) {
        xmkdir_parent_dirs(path);
    }
    int fd = open(path, flags, 0666);
    if (fd < 0 && (attrs & FILE_FLAG_BACKUP_SEMANTICS)) fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return INVALID_HANDLE_VALUE;
    xboxrecomp_handle *h = xh_alloc(XH_FILE);
    if (!h) { close(fd); return INVALID_HANDLE_VALUE; }
    h->fd = fd;
    strncpy(h->path, path, sizeof(h->path) - 1);
    return (HANDLE)h;
}

static inline BOOL ReadFile(HANDLE handle, LPVOID buf, DWORD len, DWORD *read_out, LPOVERLAPPED ov) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h || h->type != XH_FILE) return FALSE;
    ssize_t r = ov ? pread(h->fd, buf, len, ((off_t)ov->OffsetHigh << 32) | ov->Offset) : read(h->fd, buf, len);
    if (r < 0) return FALSE;
    if (read_out) *read_out = (DWORD)r;
    return TRUE;
}

static inline BOOL WriteFile(HANDLE handle, LPCVOID buf, DWORD len, DWORD *written_out, LPOVERLAPPED ov) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h || h->type != XH_FILE) return FALSE;
    ssize_t r = ov ? pwrite(h->fd, buf, len, ((off_t)ov->OffsetHigh << 32) | ov->Offset) : write(h->fd, buf, len);
    if (r < 0) return FALSE;
    if (written_out) *written_out = (DWORD)r;
    return TRUE;
}

static inline BOOL SetFilePointerEx(HANDLE handle, LARGE_INTEGER distance, LARGE_INTEGER *new_pos, DWORD method) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h || h->type != XH_FILE) return FALSE;
    off_t pos = lseek(h->fd, (off_t)distance.QuadPart, method);
    if (pos < 0) return FALSE;
    if (new_pos) new_pos->QuadPart = (LONGLONG)pos;
    return TRUE;
}

static inline BOOL SetEndOfFile(HANDLE handle) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h || h->type != XH_FILE) return FALSE;
    off_t pos = lseek(h->fd, 0, SEEK_CUR);
    return pos >= 0 && ftruncate(h->fd, pos) == 0;
}

static inline void xstat_to_info(const struct stat *st, BY_HANDLE_FILE_INFORMATION *fi) {
    memset(fi, 0, sizeof(*fi));
    fi->dwFileAttributes = S_ISDIR(st->st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    unix_time_to_filetime(st->st_ctime, &fi->ftCreationTime);
    unix_time_to_filetime(st->st_atime, &fi->ftLastAccessTime);
    unix_time_to_filetime(st->st_mtime, &fi->ftLastWriteTime);
    fi->nFileSizeLow = (DWORD)st->st_size;
    fi->nFileSizeHigh = (DWORD)((uint64_t)st->st_size >> 32);
    fi->nNumberOfLinks = (DWORD)st->st_nlink;
}

static inline BOOL GetFileInformationByHandle(HANDLE handle, BY_HANDLE_FILE_INFORMATION *fi) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    struct stat st;
    if (!h || h->type != XH_FILE || fstat(h->fd, &st) != 0) return FALSE;
    xstat_to_info(&st, fi);
    return TRUE;
}

static inline BOOL GetFileInformationByHandleEx(HANDLE handle, FILE_INFO_BY_HANDLE_CLASS cls, LPVOID out, DWORD out_size) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h || h->type != XH_FILE || !out) return FALSE;
    if (cls == FileNameInfo && out_size >= sizeof(FILE_NAME_INFO)) {
        FILE_NAME_INFO *ni = (FILE_NAME_INFO *)out;
        xutf8_to_w(h->path, ni->FileName, (out_size - sizeof(DWORD)) / sizeof(WCHAR));
        ni->FileNameLength = (DWORD)(wcslen(ni->FileName) * sizeof(WCHAR));
        return TRUE;
    }
    return FALSE;
}

static inline BOOL SetFileInformationByHandle(HANDLE handle, FILE_INFO_BY_HANDLE_CLASS cls, LPVOID in, DWORD in_size) {
    (void)in_size;
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h || h->type != XH_FILE) return FALSE;
    if (cls == FileDispositionInfo) {
        FILE_DISPOSITION_INFO *di = (FILE_DISPOSITION_INFO *)in;
        h->delete_on_close = di && di->DeleteFile;
    }
    return TRUE;
}

static inline BOOL SetFileTime(HANDLE handle, const FILETIME *ct, const FILETIME *at, const FILETIME *wt) {
    (void)handle; (void)ct; (void)at; (void)wt; return TRUE;
}

static inline BOOL FlushFileBuffers(HANDLE handle) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    return h && h->type == XH_FILE ? fsync(h->fd) == 0 : FALSE;
}

static inline DWORD GetFinalPathNameByHandleW(HANDLE handle, LPWSTR out, DWORD len, DWORD flags) {
    (void)flags;
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h || h->type != XH_FILE || !out) return 0;
    xutf8_to_w(h->path, out, len);
    return (DWORD)wcslen(out);
}

static inline BOOL DeleteFileW(LPCWSTR pathw) {
    char path[PATH_MAX];
    return unlink(xw_to_utf8(pathw, path, sizeof(path))) == 0;
}
static inline BOOL RemoveDirectoryW(LPCWSTR pathw) {
    char path[PATH_MAX];
    return rmdir(xw_to_utf8(pathw, path, sizeof(path))) == 0;
}
static inline BOOL CreateDirectoryW(LPCWSTR pathw, void *sa) {
    (void)sa;
    char path[PATH_MAX];
    return mkdir(xw_to_utf8(pathw, path, sizeof(path)), 0777) == 0 || errno == EEXIST;
}

static inline BOOL GetFileAttributesExW(LPCWSTR pathw, GET_FILEEX_INFO_LEVELS level, WIN32_FILE_ATTRIBUTE_DATA *fad) {
    (void)level;
    char path[PATH_MAX];
    struct stat st;
    if (stat(xw_to_utf8(pathw, path, sizeof(path)), &st) != 0) return FALSE;
    memset(fad, 0, sizeof(*fad));
    fad->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    unix_time_to_filetime(st.st_ctime, &fad->ftCreationTime);
    unix_time_to_filetime(st.st_atime, &fad->ftLastAccessTime);
    unix_time_to_filetime(st.st_mtime, &fad->ftLastWriteTime);
    fad->nFileSizeLow = (DWORD)st.st_size;
    fad->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
    return TRUE;
}

static inline BOOL GetDiskFreeSpaceExW(LPCWSTR pathw, PULARGE_INTEGER free_bytes, PULARGE_INTEGER total_bytes, PULARGE_INTEGER total_free) {
    (void)pathw;
    struct statvfs st;
    if (statvfs(".", &st) != 0) return FALSE;
    ULONGLONG freev = (ULONGLONG)st.f_bavail * st.f_frsize;
    ULONGLONG totalv = (ULONGLONG)st.f_blocks * st.f_frsize;
    if (free_bytes) free_bytes->QuadPart = freev;
    if (total_bytes) total_bytes->QuadPart = totalv;
    if (total_free) total_free->QuadPart = freev;
    return TRUE;
}

static inline int xwild_match(const char *name, const char *pat) {
    if (!pat || strcmp(pat, "*") == 0) return 1;
    const char *star = strchr(pat, '*');
    if (!star) return strcmp(name, pat) == 0;
    size_t pre = (size_t)(star - pat);
    size_t suf = strlen(star + 1);
    size_t n = strlen(name);
    return strncmp(name, pat, pre) == 0 && n >= pre + suf && strcmp(name + n - suf, star + 1) == 0;
}

static inline HANDLE FindFirstFileW(LPCWSTR patternw, WIN32_FIND_DATAW *data) {
    char pattern[PATH_MAX], dir[PATH_MAX], pat[PATH_MAX];
    xw_to_utf8(patternw, pattern, sizeof(pattern));
    char *slash = strrchr(pattern, '/');
    char *bslash = strrchr(pattern, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
    if (slash) {
        size_t n = (size_t)(slash - pattern);
        memcpy(dir, pattern, n);
        dir[n] = '\0';
        strncpy(pat, slash + 1, sizeof(pat) - 1);
    } else {
        strcpy(dir, ".");
        strncpy(pat, pattern, sizeof(pat) - 1);
    }
    DIR *d = opendir(dir);
    if (!d) return INVALID_HANDLE_VALUE;
    xboxrecomp_handle *h = xh_alloc(XH_FIND);
    if (!h) { closedir(d); return INVALID_HANDLE_VALUE; }
    h->dir = d;
    strncpy(h->dir_path, dir, sizeof(h->dir_path) - 1);
    strncpy(h->path, pat, sizeof(h->path) - 1);
    while (1) {
        struct dirent *de = readdir(d);
        if (!de) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
        if (xwild_match(de->d_name, h->path)) {
            char full[PATH_MAX];
            struct stat st;
            snprintf(full, sizeof(full), "%s/%s", h->dir_path, de->d_name);
            stat(full, &st);
            memset(data, 0, sizeof(*data));
            data->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            unix_time_to_filetime(st.st_mtime, &data->ftLastWriteTime);
            data->nFileSizeLow = (DWORD)st.st_size;
            data->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
            xutf8_to_w(de->d_name, data->cFileName, MAX_PATH);
            return (HANDLE)h;
        }
    }
}

static inline BOOL FindNextFileW(HANDLE handle, WIN32_FIND_DATAW *data) {
    xboxrecomp_handle *h = (xboxrecomp_handle *)handle;
    if (!h || h->type != XH_FIND || !h->dir) return FALSE;
    while (1) {
        struct dirent *de = readdir(h->dir);
        if (!de) return FALSE;
        if (xwild_match(de->d_name, h->path)) {
            char full[PATH_MAX];
            struct stat st;
            snprintf(full, sizeof(full), "%s/%s", h->dir_path, de->d_name);
            stat(full, &st);
            memset(data, 0, sizeof(*data));
            data->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            unix_time_to_filetime(st.st_mtime, &data->ftLastWriteTime);
            data->nFileSizeLow = (DWORD)st.st_size;
            data->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
            xutf8_to_w(de->d_name, data->cFileName, MAX_PATH);
            return TRUE;
        }
    }
}

static inline BOOL FindClose(HANDLE handle) { return CloseHandle(handle); }

static inline HMODULE LoadLibraryA(LPCSTR name) { (void)name; return NULL; }
static inline void *GetProcAddress(HMODULE m, LPCSTR name) { (void)m; (void)name; return NULL; }
static inline HWND FindWindowA(LPCSTR class_name, LPCSTR window_name) { (void)class_name; (void)window_name; return NULL; }
static inline void ExitProcess(UINT code) { exit((int)code); }
static inline int MessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type) {
    (void)hwnd; (void)type;
    fprintf(stderr, "%s: %s\n", caption ? caption : "MessageBox", text ? text : "");
    return 0;
}
static inline PVOID AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler) {
    (void)first; (void)handler; return NULL;
}

static inline BOOL CreateTimerQueueTimer(PHANDLE out, HANDLE queue, PTP_TIMER_CALLBACK cb, PVOID param, DWORD due_ms, DWORD period_ms, DWORD flags) {
    (void)queue; (void)cb; (void)param; (void)due_ms; (void)period_ms; (void)flags;
    xboxrecomp_handle *h = xh_alloc(XH_TIMER);
    if (!h) return FALSE;
    *out = (HANDLE)h;
    return TRUE;
}
static inline HANDLE CreateTimerQueue(void) { return (HANDLE)xh_alloc(XH_TIMER); }
static inline BOOL DeleteTimerQueueTimer(HANDLE queue, HANDLE timer, HANDLE event) {
    (void)queue; (void)event; return CloseHandle(timer);
}
static inline BOOL TrySubmitThreadpoolCallback(PTP_SIMPLE_CALLBACK cb, PVOID ctx, void *env) {
    (void)env;
    if (cb) cb(NULL, ctx);
    return TRUE;
}

static inline void RtlUnwind(PVOID target_frame, PVOID target_ip, PVOID exception_record, PVOID return_value) {
    (void)target_frame; (void)target_ip; (void)exception_record; (void)return_value;
}
static inline void RaiseException(DWORD code, DWORD flags, DWORD n, const ULONG_PTR *args) {
    (void)args;
    fprintf(stderr, "RaiseException(code=0x%08x, flags=0x%08x, params=%u)\n", code, flags, n);
    abort();
}

static inline void *_aligned_malloc(size_t size, size_t alignment) {
    void *p = NULL;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    return posix_memalign(&p, alignment, size) == 0 ? p : NULL;
}

static inline void _aligned_free(void *p) {
    free(p);
}

static inline void SecureZeroMemory(void *p, size_t n) {
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
}

static inline unsigned int _clearfp(void) {
    return 0;
}

#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif

#endif /* !_WIN32 */

#endif /* XBOXRECOMP_WINDOWS_COMPAT_H */
