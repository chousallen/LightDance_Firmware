#pragma once

#include <stdint.h>

#define NOTIFICATION_UPDATE (1 << 0)
#define NOTIFICATION_EVENT (1 << 1)

typedef enum {
    EVENT_PLAY,
    EVENT_TEST,
    EVENT_PAUSE,
    EVENT_STOP,
    EVENT_RELEASE,
    EVENT_LOAD,
    EVENT_EXIT,
    EVENT_SEEK,
} event_t;

typedef enum {
    SOLID_RGB = 0,
    BREATH_RGB,
} TestMode;

typedef struct {
    TestMode mode;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} TestData;

struct Event {
    event_t type;

    union {
        uint32_t data;
        TestData test_data;
    };
};
