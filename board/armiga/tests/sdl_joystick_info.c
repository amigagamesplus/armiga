#include <SDL3/SDL.h>
#include <stdio.h>

int main(void)
{
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD) < 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    int num = 0;
    SDL_JoystickID *joysticks = SDL_GetJoysticks(&num);
    printf("Joysticks found: %d\n", num);
    if (num == 0) { SDL_Quit(); return 0; }

    SDL_Joystick *joy = SDL_OpenJoystick(joysticks[0]);
    SDL_GUID guid = SDL_GetJoystickGUID(joy);
    char guid_str[33];
    SDL_GUIDToString(guid, guid_str, sizeof(guid_str));
    printf("Joystick 0: %s\n", SDL_GetJoystickName(joy));
    printf("  GUID: %s\n", guid_str);
    printf("  Axes: %d Buttons: %d Hats: %d\n",
           SDL_GetNumJoystickAxes(joy),
           SDL_GetNumJoystickButtons(joy),
           SDL_GetNumJoystickHats(joy));
    printf("  Is Gamepad: %s\n\n", SDL_IsGamepad(joysticks[0]) ? "YES" : "NO");

    printf("Move sticks and press buttons (Ctrl+C to exit):\n");
    SDL_Event e;
    while (1) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_JOYSTICK_AXIS_MOTION)
                printf("Axis %d: %d\n", e.jaxis.axis, e.jaxis.value);
            else if (e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN)
                printf("Button %d: DOWN\n", e.jbutton.button);
            else if (e.type == SDL_EVENT_JOYSTICK_HAT_MOTION)
                printf("Hat %d: %d\n", e.jhat.hat, e.jhat.value);
            else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
                printf("Gamepad Button %d: DOWN\n", e.gbutton.button);
            else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
                printf("Gamepad Axis %d: %d\n", e.gaxis.axis, e.gaxis.value);
        }
        SDL_Delay(16);
    }

    SDL_CloseJoystick(joy);
    SDL_free(joysticks);
    SDL_Quit();
    return 0;
}
