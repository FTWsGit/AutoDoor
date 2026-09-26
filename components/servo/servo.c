#include "driver/gpio.h"
#include "driver/ledc.h"

#include "servo.h"


void servo_init() {
    gpio_set_direction(SERVO_GPIO_NUM, GPIO_MODE_OUTPUT);

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .clk_cfg = LEDC_AUTO_CLK,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 50,
        .timer_num = LEDC_TIMER_0
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL_0,
        .duty = (SERVO_STOP_US * 8192) / SERVO_ALL_US,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .gpio_num = SERVO_GPIO_NUM, 
        .timer_sel = LEDC_TIMER_0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void servo_switch(servo_direction_t direct, int speed) {
    switch (direct) 
    {
    case SERVO_STOP:
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (SERVO_STOP_US * 8192) / SERVO_ALL_US);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        break;
    case SERVO_LEFT:
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, ((SERVO_STOP_US - speed) * 8192) / SERVO_ALL_US);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        break;
    case SERVO_RIGHT:
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, ((SERVO_STOP_US + speed) * 8192) / SERVO_ALL_US);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        break;
    }
}
