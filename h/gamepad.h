#ifndef __GAMEPAD_H
#define __GAMEPAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAMEPAD_MAX_CONTROLLERS 4

typedef enum eGamepadButton
{
   kGamepadButtonA = 0,
   kGamepadButtonB,
   kGamepadButtonX,
   kGamepadButtonY,
   kGamepadButtonLeftShoulder,
   kGamepadButtonRightShoulder,
   kGamepadButtonView,
   kGamepadButtonMenu,
   kGamepadButtonLeftStick,
   kGamepadButtonRightStick,
   kGamepadButtonDpadUp,
   kGamepadButtonDpadDown,
   kGamepadButtonDpadLeft,
   kGamepadButtonDpadRight,
   kGamepadButtonLeftTrigger,
   kGamepadButtonRightTrigger,
   kGamepadButtonCount
} eGamepadButton;

typedef struct sGamepadState
{
   int connected;
   unsigned controller_index;
   unsigned packet_number;

   uint32_t buttons;
   uint32_t buttons_pressed;
   uint32_t buttons_released;

   float left_x;
   float left_y;
   float right_x;
   float right_y;
   float left_trigger;
   float right_trigger;
} sGamepadState;

int GamepadInit(void);
void GamepadTerm(void);
void GamepadUpdate(void);
const sGamepadState *GamepadGetState(unsigned index);
unsigned GamepadGetActiveIndex(void);
int GamepadOwnsInput(void);
const char *GamepadGetBackendName(void);

void GamepadSetActive(int active);
void GamepadSetRumble(unsigned index, float low, float high);
void GamepadStopAllRumble(void);

#ifdef __cplusplus
}
#endif

#endif
