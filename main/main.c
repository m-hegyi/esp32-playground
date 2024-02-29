#include "driver/gpio.h"

#define CURRENT_PIN GPIO_NUM_3

void app_main(void)
{
    gpio_set_direction(CURRENT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(CURRENT_PIN, 1);
}
