#ifndef INPUT_INPUT_DOS_H
#define INPUT_INPUT_DOS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool InputDOS_WaitForEvent(void);
extern void InputDOS_FlushEvents(void);
extern void InputDOS_PollAutomation(int64_t ticks);
extern void InputDOS_GetCursorPosition(int *x, int *y);

#ifdef __cplusplus
}
#endif

#endif
