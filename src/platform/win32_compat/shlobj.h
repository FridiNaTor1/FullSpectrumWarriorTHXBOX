#ifndef XBOXRECOMP_SHLOBJ_COMPAT_H
#define XBOXRECOMP_SHLOBJ_COMPAT_H

#ifdef _WIN32
#include_next <shlobj.h>
#else

#include <windows.h>

#define CSIDL_LOCAL_APPDATA 0x001c

static inline HRESULT SHGetFolderPathW(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPWSTR path)
{
    (void)hwnd; (void)csidl; (void)token; (void)flags;
    const char *base = getenv("XDG_CONFIG_HOME");
    char fallback[PATH_MAX];
    if (!base || !*base) {
        const char *home = getenv("HOME");
        snprintf(fallback, sizeof(fallback), "%s/.config", home ? home : ".");
        base = fallback;
    }
    xutf8_to_w(base, path, MAX_PATH);
    return S_OK;
}

#endif

#endif
