/*
 * kernel_path.c - Xbox→Windows Path Translation
 *
 * Translates Xbox device-style paths to Windows filesystem paths:
 *   \Device\CdRom0\  → <game_dir>\Burnout 3 Takedown\
 *   D:\               → <game_dir>\Burnout 3 Takedown\
 *   T:\               → <save_dir>\TitleData\
 *   U:\               → <save_dir>\UserData\
 *   Z:\               → <save_dir>\Cache\
 */

#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <shlobj.h>

/* Base directories - set at init */
static WCHAR s_game_dir[MAX_PATH];    /* Path to game disc content */
static WCHAR s_save_dir[MAX_PATH];    /* Base for save/cache directories */
static BOOL  s_initialized = FALSE;

static void create_directory_tree_w(const WCHAR* path)
{
    WCHAR partial[MAX_PATH];
    size_t len;

    if (!path || !path[0])
        return;

    wcsncpy(partial, path, MAX_PATH - 1);
    partial[MAX_PATH - 1] = L'\0';
    len = wcslen(partial);
    for (size_t i = 1; i < len; i++) {
        if (partial[i] == L'\\' || partial[i] == L'/') {
            WCHAR saved = partial[i];
            partial[i] = L'\0';
            if (wcslen(partial) > 0)
                CreateDirectoryW(partial, NULL);
            partial[i] = saved;
        }
    }
    CreateDirectoryW(partial, NULL);
}

static BOOL xbox_path_is_sep(WCHAR ch)
{
    return ch == L'\\' || ch == L'/';
}

static int xbox_wcsicmp(const WCHAR* a, const WCHAR* b)
{
    while (*a && *b) {
        WCHAR ca = (WCHAR)towlower(*a);
        WCHAR cb = (WCHAR)towlower(*b);
        if (ca != cb)
            return (int)ca - (int)cb;
        a++;
        b++;
    }
    return (int)towlower(*a) - (int)towlower(*b);
}

static BOOL xbox_path_exists_w(const WCHAR* path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    return GetFileAttributesExW(path, GetFileExInfoStandard, &fad);
}

static BOOL copy_wide_path(WCHAR* dst, DWORD dst_count, const WCHAR* src)
{
    size_t len;
    if (!dst || !src || dst_count == 0)
        return FALSE;
    len = wcslen(src);
    if (len + 1 > dst_count)
        return FALSE;
    wmemcpy(dst, src, len + 1);
    return TRUE;
}

static BOOL append_path_component(WCHAR* path, DWORD buf_size, const WCHAR* component)
{
    size_t len = wcslen(path);
    if (len == 0) {
        return copy_wide_path(path, buf_size, component);
    }

    if (xbox_path_is_sep(path[len - 1]) || path[len - 1] == L':') {
        return wcscat_s(path, buf_size, component) == 0;
    }

    return wcscat_s(path, buf_size, L"\\") == 0 &&
           wcscat_s(path, buf_size, component) == 0;
}

static BOOL find_case_insensitive_child(const WCHAR* parent, const WCHAR* wanted,
                                        WCHAR* actual, DWORD actual_size)
{
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find_handle;

    if (!parent || parent[0] == 0) {
        swprintf_s(pattern, MAX_PATH, L".\\*");
    } else if (xbox_path_is_sep(parent[wcslen(parent) - 1])) {
        swprintf_s(pattern, MAX_PATH, L"%ls*", parent);
    } else {
        swprintf_s(pattern, MAX_PATH, L"%ls\\*", parent);
    }

    find_handle = FindFirstFileW(pattern, &fd);
    if (find_handle == INVALID_HANDLE_VALUE)
        return FALSE;

    do {
        if (xbox_wcsicmp(fd.cFileName, wanted) == 0) {
            copy_wide_path(actual, actual_size, fd.cFileName);
            FindClose(find_handle);
            return TRUE;
        }
    } while (FindNextFileW(find_handle, &fd));

    FindClose(find_handle);
    return FALSE;
}

static BOOL resolve_case_insensitive_path(WCHAR* path, DWORD buf_size)
{
    WCHAR resolved[MAX_PATH] = L"";
    const WCHAR* p = path;

    if (xbox_path_exists_w(path))
        return TRUE;

    if (!path || !path[0])
        return FALSE;

    if (path[1] == L':') {
        resolved[0] = path[0];
        resolved[1] = L':';
        resolved[2] = L'\0';
        p = path + 2;
        if (xbox_path_is_sep(*p)) {
            wcscat_s(resolved, MAX_PATH, L"\\");
            while (xbox_path_is_sep(*p)) p++;
        }
    } else if (xbox_path_is_sep(path[0])) {
        copy_wide_path(resolved, MAX_PATH, L"\\");
        while (xbox_path_is_sep(*p)) p++;
    }

    while (*p) {
        WCHAR component[MAX_PATH];
        WCHAR actual[MAX_PATH];
        WCHAR candidate[MAX_PATH];
        size_t ci = 0;

        while (xbox_path_is_sep(*p)) p++;
        if (!*p)
            break;

        while (*p && !xbox_path_is_sep(*p) && ci + 1 < MAX_PATH) {
            component[ci++] = *p++;
        }
        component[ci] = L'\0';

        copy_wide_path(candidate, MAX_PATH, resolved);
        if (!append_path_component(candidate, MAX_PATH, component))
            return FALSE;

        if (xbox_path_exists_w(candidate)) {
            copy_wide_path(resolved, MAX_PATH, candidate);
            continue;
        }

        if (!find_case_insensitive_child(resolved, component, actual, MAX_PATH))
            return FALSE;

        if (!append_path_component(resolved, MAX_PATH, actual))
            return FALSE;
    }

    if (!xbox_path_exists_w(resolved))
        return FALSE;

    copy_wide_path(path, buf_size, resolved);
    return TRUE;
}

void xbox_path_init(const char* game_dir, const char* save_dir)
{
#ifndef __linux__
    WCHAR save_base[MAX_PATH];
#endif
    const char* save_dir_env;

    if (game_dir) {
        MultiByteToWideChar(CP_UTF8, 0, game_dir, -1, s_game_dir, MAX_PATH);
    } else {
        /* Default: current directory + "Burnout 3 Takedown" */
        GetCurrentDirectoryW(MAX_PATH, s_game_dir);
        wcscat_s(s_game_dir, MAX_PATH, L"\\Burnout 3 Takedown");
    }

    save_dir_env = getenv("FSW_TH_SAVE_DIR");
    if (save_dir && save_dir[0]) {
        MultiByteToWideChar(CP_UTF8, 0, save_dir, -1, s_save_dir, MAX_PATH);
    } else if (save_dir_env && save_dir_env[0]) {
        MultiByteToWideChar(CP_UTF8, 0, save_dir_env, -1, s_save_dir, MAX_PATH);
    } else {
#ifdef __linux__
        char save_path[MAX_PATH];
        const char* home = getenv("HOME");
        const char* xdg_config = getenv("XDG_CONFIG_HOME");
        if (home && home[0]) {
            snprintf(save_path, sizeof(save_path), "%s/.config/FSWTH/saves", home);
        } else if (xdg_config && xdg_config[0]) {
            snprintf(save_path, sizeof(save_path), "%s/FSWTH/saves", xdg_config);
        } else {
            snprintf(save_path, sizeof(save_path), "./FSWTH/saves");
        }
        MultiByteToWideChar(CP_UTF8, 0, save_path, -1, s_save_dir, MAX_PATH);
#else
        /* Default: $XDG_CONFIG_HOME/FSWTH/saves, or ~/.config/FSWTH/saves. */
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, save_base))) {
            swprintf_s(s_save_dir, MAX_PATH, L"%ls\\FSWTH\\saves", save_base);
        } else {
            GetCurrentDirectoryW(MAX_PATH, s_save_dir);
            wcscat_s(s_save_dir, MAX_PATH, L"\\SaveData");
        }
#endif
    }

    /* Ensure trailing backslashes are stripped for consistent concatenation */
    size_t len = wcslen(s_game_dir);
    if (len > 0 && s_game_dir[len - 1] == L'\\')
        s_game_dir[len - 1] = L'\0';

    len = wcslen(s_save_dir);
    if (len > 0 && s_save_dir[len - 1] == L'\\')
        s_save_dir[len - 1] = L'\0';

    create_directory_tree_w(s_save_dir);
    s_initialized = TRUE;
    xbox_log(XBOX_LOG_INFO, XBOX_LOG_PATH, "Path init: game=%S, save=%S", s_game_dir, s_save_dir);
}

/*
 * Helper: check if an ANSI string starts with a prefix (case-insensitive).
 * Returns the number of chars consumed from the prefix, or 0 if no match.
 */
static int match_prefix(const char* path, const char* prefix)
{
    int i = 0;
    while (prefix[i]) {
        if (tolower((unsigned char)path[i]) != tolower((unsigned char)prefix[i]))
            return 0;
        i++;
    }
    return i;
}

BOOL xbox_translate_path(const char* xbox_path, WCHAR* win_path_buf, DWORD buf_size)
{
    const char* remainder = NULL;
    const WCHAR* base_dir = NULL;
    const WCHAR* sub_dir = NULL;
    int skip;

    if (!xbox_path || !win_path_buf || buf_size == 0)
        return FALSE;

    if (!s_initialized)
        xbox_path_init(NULL, NULL);

    /* \Device\CdRom0\ → game disc */
    skip = match_prefix(xbox_path, "\\Device\\CdRom0\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_game_dir;
        sub_dir = NULL;
        goto translate;
    }

    /* \Device\Harddisk0\Partition1\ → game disc (alternative) */
    skip = match_prefix(xbox_path, "\\Device\\Harddisk0\\Partition1\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_game_dir;
        sub_dir = NULL;
        goto translate;
    }

    /* C:\ → system partition (dashboard files, XIP archives) */
    skip = match_prefix(xbox_path, "C:\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_game_dir;
        sub_dir = NULL;
        goto translate;
    }

    /* c:\ (lowercase variant) */
    skip = match_prefix(xbox_path, "c:\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_game_dir;
        sub_dir = NULL;
        goto translate;
    }

    /* D:\ → game disc */
    skip = match_prefix(xbox_path, "D:\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_game_dir;
        sub_dir = NULL;
        goto translate;
    }

    /* d:\ (lowercase variant) */
    skip = match_prefix(xbox_path, "d:\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_game_dir;
        sub_dir = NULL;
        goto translate;
    }

    /* T:\ → TitleData (save games) */
    skip = match_prefix(xbox_path, "T:\\");
    if (!skip) skip = match_prefix(xbox_path, "t:\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_save_dir;
        sub_dir = L"\\TitleData";
        goto translate;
    }

    /* U:\ → UserData */
    skip = match_prefix(xbox_path, "U:\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_save_dir;
        sub_dir = L"\\UserData";
        goto translate;
    }

    /* Z:\ → Cache */
    skip = match_prefix(xbox_path, "Z:\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_save_dir;
        sub_dir = L"\\Cache";
        goto translate;
    }

    /* \??\D:\ variant (NT object manager prefix) */
    skip = match_prefix(xbox_path, "\\??\\D:\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_game_dir;
        sub_dir = NULL;
        goto translate;
    }

    skip = match_prefix(xbox_path, "\\??\\T:\\");
    if (skip) {
        remainder = xbox_path + skip;
        base_dir = s_save_dir;
        sub_dir = L"\\TitleData";
        goto translate;
    }

    /* Unrecognized path - try to use as-is by converting to wide */
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_PATH, "Unrecognized Xbox path: %s", xbox_path);
    MultiByteToWideChar(CP_ACP, 0, xbox_path, -1, win_path_buf, buf_size);
    return TRUE;

translate:
    {
        WCHAR remainder_wide[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, remainder, -1, remainder_wide, MAX_PATH);

        /* Convert forward slashes to backslashes in remainder */
        for (WCHAR* p = remainder_wide; *p; p++) {
            if (*p == L'/') *p = L'\\';
        }

        if (sub_dir) {
            swprintf_s(win_path_buf, buf_size, L"%ls%ls\\%ls", base_dir, sub_dir, remainder_wide);
        } else {
            swprintf_s(win_path_buf, buf_size, L"%ls\\%ls", base_dir, remainder_wide);
            resolve_case_insensitive_path(win_path_buf, buf_size);
        }

        /* Ensure save directories exist */
        if (sub_dir) {
            WCHAR dir_path[MAX_PATH];
            swprintf_s(dir_path, MAX_PATH, L"%ls%ls", base_dir, sub_dir);
            create_directory_tree_w(s_save_dir);
            create_directory_tree_w(dir_path);
        }

        XBOX_TRACE(XBOX_LOG_PATH, "%s -> %S", xbox_path, win_path_buf);
        return TRUE;
    }
}
