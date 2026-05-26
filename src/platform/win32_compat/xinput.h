#ifndef XBOXRECOMP_XINPUT_COMPAT_H
#define XBOXRECOMP_XINPUT_COMPAT_H

#ifdef _WIN32
#include_next <xinput.h>
#else

#include <windows.h>

#define XINPUT_GAMEPAD_DPAD_UP        0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN      0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT      0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT     0x0008
#define XINPUT_GAMEPAD_START          0x0010
#define XINPUT_GAMEPAD_BACK           0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB     0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB    0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER  0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A              0x1000
#define XINPUT_GAMEPAD_B              0x2000
#define XINPUT_GAMEPAD_X              0x4000
#define XINPUT_GAMEPAD_Y              0x8000

typedef struct XINPUT_GAMEPAD {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} XINPUT_GAMEPAD;

typedef struct XINPUT_STATE {
    DWORD dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;

typedef struct XINPUT_VIBRATION {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} XINPUT_VIBRATION;

typedef struct XINPUT_CAPABILITIES {
    BYTE Type;
    BYTE SubType;
    WORD Flags;
    XINPUT_GAMEPAD Gamepad;
    XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES;

static inline DWORD XInputGetState(DWORD user, XINPUT_STATE *state) {
    (void)user;
    if (state) memset(state, 0, sizeof(*state));
    return ERROR_DEVICE_NOT_CONNECTED;
}

static inline DWORD XInputSetState(DWORD user, XINPUT_VIBRATION *vibration) {
    (void)user; (void)vibration;
    return ERROR_DEVICE_NOT_CONNECTED;
}

static inline DWORD XInputGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES *caps) {
    (void)user; (void)flags;
    if (caps) memset(caps, 0, sizeof(*caps));
    return ERROR_DEVICE_NOT_CONNECTED;
}

#endif

#endif
