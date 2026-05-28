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
static BOOL g_keyboard_controller = TRUE;

typedef struct ScriptInputEvent {
    Uint32 start_ms;
    Uint32 duration_ms;
    WORD buttons;
    BYTE analog[8];
    SHORT lx;
    SHORT ly;
} ScriptInputEvent;

#define MAX_SCRIPT_INPUT_EVENTS 128

static ScriptInputEvent g_script_events[MAX_SCRIPT_INPUT_EVENTS];
static int g_script_event_count = 0;
static BOOL g_script_checked = FALSE;
static Uint32 g_script_start_ticks = 0;

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

static void script_add_button(ScriptInputEvent *event, const char *name)
{
    if (strcmp(name, "start") == 0) {
        event->buttons |= XBOX_GAMEPAD_START;
        event->analog[XBOX_BUTTON_A] = 255;
    } else if (strcmp(name, "back") == 0) {
        event->buttons |= XBOX_GAMEPAD_BACK;
    } else if (strcmp(name, "up") == 0) {
        event->buttons |= XBOX_GAMEPAD_DPAD_UP;
        event->ly = 32767;
    } else if (strcmp(name, "down") == 0) {
        event->buttons |= XBOX_GAMEPAD_DPAD_DOWN;
        event->ly = -32768;
    } else if (strcmp(name, "left") == 0) {
        event->buttons |= XBOX_GAMEPAD_DPAD_LEFT;
        event->lx = -32768;
    } else if (strcmp(name, "right") == 0) {
        event->buttons |= XBOX_GAMEPAD_DPAD_RIGHT;
        event->lx = 32767;
    } else if (strcmp(name, "a") == 0 || strcmp(name, "accept") == 0) {
        event->analog[XBOX_BUTTON_A] = 255;
    } else if (strcmp(name, "b") == 0 || strcmp(name, "cancel") == 0) {
        event->analog[XBOX_BUTTON_B] = 255;
    } else if (strcmp(name, "x") == 0) {
        event->analog[XBOX_BUTTON_X] = 255;
    } else if (strcmp(name, "y") == 0) {
        event->analog[XBOX_BUTTON_Y] = 255;
    } else if (strcmp(name, "white") == 0 || strcmp(name, "lb") == 0) {
        event->analog[XBOX_BUTTON_WHITE] = 255;
    } else if (strcmp(name, "black") == 0 || strcmp(name, "rb") == 0) {
        event->analog[XBOX_BUTTON_BLACK] = 255;
    } else if (strcmp(name, "lt") == 0) {
        event->analog[XBOX_BUTTON_LTRIGGER] = 255;
    } else if (strcmp(name, "rt") == 0) {
        event->analog[XBOX_BUTTON_RTRIGGER] = 255;
    }
}

static void script_parse_events(void)
{
    const char *script = getenv("FSW_TH_INPUT_SCRIPT");
    char *copy;
    char *save_outer = NULL;
    char *token;

    if (g_script_checked) {
        return;
    }
    g_script_checked = TRUE;
    if (!script || !script[0]) {
        return;
    }

    copy = (char *)malloc(strlen(script) + 1);
    if (!copy) {
        return;
    }
    strcpy(copy, script);

    token = strtok_r(copy, ",;", &save_outer);
    while (token && g_script_event_count < MAX_SCRIPT_INPUT_EVENTS) {
        char *at = strchr(token, '@');
        char *colon = at ? strchr(at + 1, ':') : NULL;
        if (at && colon) {
            ScriptInputEvent event;
            char *save_buttons = NULL;
            char *button;
            memset(&event, 0, sizeof(event));
            *at = 0;
            *colon = 0;
            event.start_ms = (Uint32)strtoul(at + 1, NULL, 0);
            event.duration_ms = (Uint32)strtoul(colon + 1, NULL, 0);
            if (event.duration_ms == 0) {
                event.duration_ms = 180;
            }
            button = strtok_r(token, "+|", &save_buttons);
            while (button) {
                script_add_button(&event, button);
                button = strtok_r(NULL, "+|", &save_buttons);
            }
            g_script_events[g_script_event_count++] = event;
        }
        token = strtok_r(NULL, ",;", &save_outer);
    }

    free(copy);
    fprintf(stderr, "[INPUT] loaded %d scripted input events\n", g_script_event_count);
}

static void apply_script_input(XBOX_INPUT_STATE *state)
{
    Uint32 now;

    script_parse_events();
    if (g_script_event_count == 0) {
        return;
    }
    if (g_script_start_ticks == 0) {
        g_script_start_ticks = SDL_GetTicks();
    }
    now = SDL_GetTicks() - g_script_start_ticks;

    for (int i = 0; i < g_script_event_count; i++) {
        const ScriptInputEvent *event = &g_script_events[i];
        if (now >= event->start_ms && now < event->start_ms + event->duration_ms) {
            state->Gamepad.wButtons |= event->buttons;
            for (int b = 0; b < 8; b++) {
                if (event->analog[b] > state->Gamepad.bAnalogButtons[b]) {
                    state->Gamepad.bAnalogButtons[b] = event->analog[b];
                }
            }
            if (event->lx != 0) {
                state->Gamepad.sThumbLX = event->lx;
            }
            if (event->ly != 0) {
                state->Gamepad.sThumbLY = event->ly;
            }
        }
    }
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

static void fill_keyboard_state(XBOX_INPUT_STATE *state)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (!keys) return;

    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W])       state->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_UP;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S])     state->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_DOWN;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])     state->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_LEFT;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D])    state->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (keys[SDL_SCANCODE_RETURN])                           state->Gamepad.wButtons |= XBOX_GAMEPAD_START;
    if (keys[SDL_SCANCODE_BACKSPACE])                        state->Gamepad.wButtons |= XBOX_GAMEPAD_BACK;

    state->Gamepad.bAnalogButtons[XBOX_BUTTON_A] =
        (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_RETURN]) ? 255 : 0;
    state->Gamepad.bAnalogButtons[XBOX_BUTTON_B] =
        (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_RSHIFT]) ? 255 : 0;
    state->Gamepad.bAnalogButtons[XBOX_BUTTON_X] =
        keys[SDL_SCANCODE_X] ? 255 : 0;
    state->Gamepad.bAnalogButtons[XBOX_BUTTON_Y] =
        keys[SDL_SCANCODE_Y] ? 255 : 0;
    state->Gamepad.bAnalogButtons[XBOX_BUTTON_WHITE] =
        keys[SDL_SCANCODE_Q] ? 255 : 0;
    state->Gamepad.bAnalogButtons[XBOX_BUTTON_BLACK] =
        keys[SDL_SCANCODE_E] ? 255 : 0;
    state->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER] =
        keys[SDL_SCANCODE_Z] ? 255 : 0;
    state->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER] =
        keys[SDL_SCANCODE_C] ? 255 : 0;

    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
        state->Gamepad.sThumbLX = -32768;
    } else if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) {
        state->Gamepad.sThumbLX = 32767;
    }
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
        state->Gamepad.sThumbLY = 32767;
    } else if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
        state->Gamepad.sThumbLY = -32768;
    }
}

void xbox_InputInit(void)
{
    if (g_sdl_ready) return;

    if (!g_input_debug_checked) {
        const char *debug = getenv("XBOXRECOMP_INPUT_DEBUG");
        const char *keyboard = getenv("XBOXRECOMP_KEYBOARD_CONTROLLER");
        g_input_debug = (debug && debug[0] && strcmp(debug, "0") != 0);
        g_keyboard_controller = !(keyboard && keyboard[0] && strcmp(keyboard, "0") == 0);
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
        if (g_keyboard_controller && dwPort == 0) {
            XBOX_INPUT_STATE state;
            memset(&state, 0, sizeof(state));
            fill_keyboard_state(&state);
            apply_script_input(&state);
            if (memcmp(&state.Gamepad, &slot->last_state.Gamepad, sizeof(state.Gamepad)) != 0) {
                slot->packet++;
                if (g_input_debug) {
                    fprintf(stderr,
                            "[INPUT] keyboard port 0 buttons=0x%04X A=%u B=%u X=%u Y=%u LX=%d LY=%d\n",
                            state.Gamepad.wButtons,
                            state.Gamepad.bAnalogButtons[XBOX_BUTTON_A],
                            state.Gamepad.bAnalogButtons[XBOX_BUTTON_B],
                            state.Gamepad.bAnalogButtons[XBOX_BUTTON_X],
                            state.Gamepad.bAnalogButtons[XBOX_BUTTON_Y],
                            state.Gamepad.sThumbLX,
                            state.Gamepad.sThumbLY);
                }
            }
            state.dwPacketNumber = slot->packet;
            slot->last_state = state;
            *pState = state;
            return ERROR_SUCCESS;
        }
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

    apply_script_input(&state);

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
    if (g_keyboard_controller && dwPort == 0) return TRUE;
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
