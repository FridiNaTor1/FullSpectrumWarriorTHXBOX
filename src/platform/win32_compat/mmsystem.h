#ifndef XBOXRECOMP_MMSYSTEM_COMPAT_H
#define XBOXRECOMP_MMSYSTEM_COMPAT_H

#ifdef _WIN32
#include_next <mmsystem.h>
#else

#include <windows.h>

typedef unsigned int MMRESULT;
typedef void *HWAVEOUT;

#define MMSYSERR_NOERROR 0
#define CALLBACK_NULL 0
#define WAVE_MAPPER ((UINT)-1)
#define WAVE_FORMAT_PCM 1
#define WHDR_PREPARED 0x00000002
#define WHDR_DONE 0x00000001
#define WHDR_INQUEUE 0x00000010

typedef struct WAVEFORMATEX {
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
    WORD wBitsPerSample;
    WORD cbSize;
} WAVEFORMATEX;

typedef struct WAVEHDR {
    LPSTR lpData;
    DWORD dwBufferLength;
    DWORD dwBytesRecorded;
    DWORD dwUser;
    DWORD dwFlags;
    DWORD dwLoops;
    struct WAVEHDR *lpNext;
    DWORD reserved;
} WAVEHDR;

static inline MMRESULT waveOutOpen(HWAVEOUT *out, UINT dev, const WAVEFORMATEX *fmt, DWORD cb, DWORD inst, DWORD flags) {
    (void)dev; (void)fmt; (void)cb; (void)inst; (void)flags;
    if (out) *out = (HWAVEOUT)1;
    return MMSYSERR_NOERROR;
}
static inline MMRESULT waveOutPrepareHeader(HWAVEOUT h, WAVEHDR *hdr, UINT size) {
    (void)h; (void)size; if (hdr) hdr->dwFlags |= WHDR_PREPARED | WHDR_DONE; return MMSYSERR_NOERROR;
}
static inline MMRESULT waveOutUnprepareHeader(HWAVEOUT h, WAVEHDR *hdr, UINT size) {
    (void)h; (void)size; if (hdr) hdr->dwFlags &= ~WHDR_PREPARED; return MMSYSERR_NOERROR;
}
static inline MMRESULT waveOutWrite(HWAVEOUT h, WAVEHDR *hdr, UINT size) {
    (void)h; (void)size; if (hdr) hdr->dwFlags |= WHDR_DONE; return MMSYSERR_NOERROR;
}
static inline MMRESULT waveOutReset(HWAVEOUT h) { (void)h; return MMSYSERR_NOERROR; }
static inline MMRESULT waveOutClose(HWAVEOUT h) { (void)h; return MMSYSERR_NOERROR; }

#endif

#endif
