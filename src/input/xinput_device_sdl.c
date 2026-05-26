/*
 * Xbox Input -> SDL2 GameController compatibility layer.
 *
 * SDL's controller database handles DualSense and most other modern pads,
 * then this layer maps that normalized state back to the original Xbox
 * controller layout expected by recompiled titles.
 */

#include "xinput_xbox.h"

#include <SDL.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct SDLInputSlot {
    SDL_GameController *controller;
    SDL_JoystickID instance_id;
    XBOX_INPUT_STATE last_state;
    DWORD packet;
    BOOL connected;
} SDLInputSlot;

static SDLInputSlot g_slots[XBOX_MAX_CONTROLLERS];
static BOOL g_sdl_ready = FALSE;
static BOOL g_input_debug = FALSE;
static BOOL g_input_debug_checked = FALSE;

static BYTE axis_trigger_to_byte(Sint16 value)
{
    if (value <= 0) return 0;
    return (BYTE)((value * 255 + 16383) / 32767);
}

static SHORT axis_stick_to_short(Sint16 value, BOOL invert)
{
    int v = value;
    if (invert) v = -v;
    if (v < -32768) v = -32768;
    if (v > 32767) v = 32767;
    return (SHORT)v;
}

static int find_free_slot(void)
{
    for (int i = 0; i < XBOX_MAX_CONTROLLERS; i++) {
        if (!g_slots[i].connected) return i;
    }
    return -1;
}

static int find_slot_by_instance(SDL_JoystickID id)
{
    for (int i = 0; i < XBOX_MAX_CONTROLLERS; i++) {
        if (g_slots[i].connected && g_slots[i].instance_id == id) return i;
    }
    return -1;
}

static void close_slot(int slot)
{
    if (slot < 0 || slot >= XBOX_MAX_CONTROLLERS) return;
    if (g_slots[slot].controller) {
        SDL_GameControllerClose(g_slots[slot].controller);
    }
    memset(&g_slots[slot], 0, sizeof(g_slots[slot]));
    g_slots[slot].instance_id = -1;
}

static void open_device(int device_index)
{
    if (!SDL_IsGameController(device_index)) return;

    int slot = find_free_slot();
    if (slot < 0) return;

    SDL_GameController *controller = SDL_GameControllerOpen(device_index);
    if (!controller) return;

    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller);
    g_slots[slot].controller = controller;
    g_slots[slot].instance_id = joystick ? SDL_JoystickInstanceID(joystick) : -1;
    g_slots[slot].connected = TRUE;
    g_slots[slot].packet = 1;

    fprintf(stderr, "[INPUT] SDL controller %d mapped to Xbox port %d: %s\n",
            device_index, slot, SDL_GameControllerName(controller));
}

static void pump_events(void)
{
    if (!g_sdl_ready) return;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_CONTROLLERDEVICEADDED:
            open_device(event.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            close_slot(find_slot_by_instance(event.cdevice.which));
            break;
        default:
            break;
        }
    }

    SDL_GameControllerUpdate();
}

void xbox_InputInit(void)
{
    if (g_sdl_ready) return;

    if (!g_input_debug_checked) {
        const char *debug = getenv("XBOXRECOMP_INPUT_DEBUG");
        g_input_debug = (debug && debug[0] && strcmp(debug, "0") != 0);
        g_input_debug_checked = TRUE;
    }

    for (int i = 0; i < XBOX_MAX_CONTROLLERS; i++) {
        g_slots[i].instance_id = -1;
    }

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0) {
        fprintf(stderr, "[INPUT] SDL input init failed: %s\n", SDL_GetError());
        return;
    }

    SDL_GameControllerEventState(SDL_ENABLE);
    g_sdl_ready = TRUE;

    int count = SDL_NumJoysticks();
    for (int i = 0; i < count; i++) {
        open_device(i);
    }
}

DWORD xbox_InputGetState(DWORD dwPort, XBOX_INPUT_STATE *pState)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pState) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    if (!g_sdl_ready) xbox_InputInit();
    pump_events();

    SDLInputSlot *slot = &g_slots[dwPort];
    if (!slot->connected || !slot->controller) {
        memset(pState, 0, sizeof(*pState));
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    XBOX_INPUT_STATE state;
    memset(&state, 0, sizeof(state));

    SDL_GameController *c = slot->controller;

    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP))    state.Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_UP;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  state.Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_DOWN;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  state.Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_LEFT;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) state.Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START))      state.Gamepad.wButtons |= XBOX_GAMEPAD_START;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK))       state.Gamepad.wButtons |= XBOX_GAMEPAD_BACK;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSTICK))  state.Gamepad.wButtons |= XBOX_GAMEPAD_LEFT_THUMB;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) state.Gamepad.wButtons |= XBOX_GAMEPAD_RIGHT_THUMB;

    state.Gamepad.bAnalogButtons[XBOX_BUTTON_A] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A) ? 255 : 0;
    state.Gamepad.bAnalogButtons[XBOX_BUTTON_B] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B) ? 255 : 0;
    state.Gamepad.bAnalogButtons[XBOX_BUTTON_X] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X) ? 255 : 0;
    state.Gamepad.bAnalogButtons[XBOX_BUTTON_Y] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y) ? 255 : 0;
    state.Gamepad.bAnalogButtons[XBOX_BUTTON_BLACK] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? 255 : 0;
    state.Gamepad.bAnalogButtons[XBOX_BUTTON_WHITE] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? 255 : 0;
    state.Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER] =
        axis_trigger_to_byte(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    state.Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER] =
        axis_trigger_to_byte(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

    state.Gamepad.sThumbLX = axis_stick_to_short(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), FALSE);
    state.Gamepad.sThumbLY = axis_stick_to_short(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY), TRUE);
    state.Gamepad.sThumbRX = axis_stick_to_short(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX), FALSE);
    state.Gamepad.sThumbRY = axis_stick_to_short(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY), TRUE);

    if (memcmp(&state.Gamepad, &slot->last_state.Gamepad, sizeof(state.Gamepad)) != 0) {
        slot->packet++;
        if (g_input_debug) {
            fprintf(stderr,
                    "[INPUT] port %u buttons=0x%04X A=%u B=%u X=%u Y=%u LT=%u RT=%u "
                    "LX=%d LY=%d RX=%d RY=%d\n",
                    (unsigned)dwPort,
                    state.Gamepad.wButtons,
                    state.Gamepad.bAnalogButtons[XBOX_BUTTON_A],
                    state.Gamepad.bAnalogButtons[XBOX_BUTTON_B],
                    state.Gamepad.bAnalogButtons[XBOX_BUTTON_X],
                    state.Gamepad.bAnalogButtons[XBOX_BUTTON_Y],
                    state.Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER],
                    state.Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER],
                    state.Gamepad.sThumbLX,
                    state.Gamepad.sThumbLY,
                    state.Gamepad.sThumbRX,
                    state.Gamepad.sThumbRY);
        }
    }
    state.dwPacketNumber = slot->packet;
    slot->last_state = state;
    *pState = state;

    return ERROR_SUCCESS;
}

DWORD xbox_InputSetState(DWORD dwPort, const XBOX_VIBRATION *pVibration)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pVibration) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    if (!g_sdl_ready) xbox_InputInit();
    pump_events();

    SDLInputSlot *slot = &g_slots[dwPort];
    if (!slot->connected || !slot->controller) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    if (SDL_GameControllerRumble(slot->controller,
                                 pVibration->wLeftMotorSpeed,
                                 pVibration->wRightMotorSpeed,
                                 120) != 0) {
        return ERROR_NOT_SUPPORTED;
    }

    return ERROR_SUCCESS;
}

BOOL xbox_InputIsConnected(DWORD dwPort)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS) return FALSE;
    if (!g_sdl_ready) xbox_InputInit();
    pump_events();
    return g_slots[dwPort].connected;
}

DWORD xbox_InputGetCapabilities(DWORD dwPort, DWORD dwFlags, XBOX_INPUT_CAPABILITIES *pCaps)
{
    (void)dwFlags;
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pCaps) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    if (!xbox_InputIsConnected(dwPort)) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    memset(pCaps, 0, sizeof(*pCaps));
    pCaps->Type = 1;
    pCaps->SubType = 1;
    pCaps->Flags = 0;
    memset(pCaps->Gamepad.bAnalogButtons, 255, sizeof(pCaps->Gamepad.bAnalogButtons));
    pCaps->Gamepad.wButtons = XBOX_GAMEPAD_DPAD_UP | XBOX_GAMEPAD_DPAD_DOWN |
                              XBOX_GAMEPAD_DPAD_LEFT | XBOX_GAMEPAD_DPAD_RIGHT |
                              XBOX_GAMEPAD_START | XBOX_GAMEPAD_BACK |
                              XBOX_GAMEPAD_LEFT_THUMB | XBOX_GAMEPAD_RIGHT_THUMB;
    pCaps->Gamepad.sThumbLX = 32767;
    pCaps->Gamepad.sThumbLY = 32767;
    pCaps->Gamepad.sThumbRX = 32767;
    pCaps->Gamepad.sThumbRY = 32767;
    pCaps->Vibration.wLeftMotorSpeed = 65535;
    pCaps->Vibration.wRightMotorSpeed = 65535;
    return ERROR_SUCCESS;
}
