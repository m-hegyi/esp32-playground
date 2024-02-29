#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OUTPUT_GPIO GPIO_NUM_3
#define INPUT_GPIO GPIO_NUM_5

void app_main(void)
{
    gpio_set_direction(OUTPUT_GPIO, GPIO_MODE_OUTPUT);
    // gpio_set_direction(INPUT_GPIO, GPIO_MODE_INPUT);

    bool light_state = false;

    while (true)
    {
        gpio_set_level(OUTPUT_GPIO, light_state);
        light_state = !light_state;
        vTaskDelay(100);
    }
}
