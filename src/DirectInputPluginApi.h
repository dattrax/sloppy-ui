#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SloppyInputInstance SloppyInputInstance;

typedef struct SloppyInputEvent {
    int32_t key;
    int32_t pressed;
} SloppyInputEvent;

typedef struct SloppyInputConfig {
    uint32_t structSize;
} SloppyInputConfig;

typedef SloppyInputInstance* (*SloppyInputCreateFn)(const SloppyInputConfig* config);
typedef int (*SloppyInputPollEventFn)(SloppyInputInstance* instance, SloppyInputEvent* eventOut);
typedef void (*SloppyInputDestroyFn)(SloppyInputInstance* instance);

#ifdef __cplusplus
}
#endif
