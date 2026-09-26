#pragma once

#include "driver/gpio.h"

#define SERVO_STOP_US 1500
#define SERVO_ALL_US  20000

#define SERVO_GPIO_NUM GPIO_NUM_1

typedef enum {
    SERVO_STOP = 0,
    SERVO_LEFT,
    SERVO_RIGHT
} servo_direction_t;

void servo_init(void);
void servo_switch(servo_direction_t direct, int speed);
