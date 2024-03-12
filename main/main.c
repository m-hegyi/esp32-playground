#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ha/esp_zigbee_ha_standard.h"

#define OUTPUT_GPIO GPIO_NUM_2
#define INPUT_GPIO GPIO_NUM_3

#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE in idf.py menuconfig to compile (End Device) source code.
#endif

static QueueHandle_t gpio_event_queue = NULL;

bool light = false;

static void IRAM_ATTR gpio_isr_handler(void *args)
{
    uint32_t gpio_num = (uint32_t)args;
    xQueueSendFromISR(gpio_event_queue, &gpio_num, NULL);
}

static void get_time()
{
    int64_t time = esp_timer_get_time();

    ESP_LOGI("TIME", "%lli", time);
}

static void gpio_task(void *args)
{
    uint32_t pv_buffer;
    for (;;)
    {
        if (xQueueReceive(gpio_event_queue, &pv_buffer, portMAX_DELAY))
        {
            if (gpio_get_level(INPUT_GPIO) == 0)
            {
                light = !light;
                gpio_set_level(OUTPUT_GPIO, light ? 1 : 0);
                ESP_LOGI("BUTTON", "change");
            }
            get_time();
        }
    }
}

void app_main(void)
{

    gpio_config_t io_config = {};

    io_config.intr_type = GPIO_INTR_DISABLE;
    io_config.mode = GPIO_MODE_OUTPUT;
    io_config.pull_down_en = 0;
    io_config.pin_bit_mask = (1ULL << OUTPUT_GPIO);
    io_config.pull_up_en = 0;
    gpio_config(&io_config);

    io_config.intr_type = GPIO_INTR_POSEDGE;
    io_config.mode = GPIO_MODE_INPUT;
    io_config.pin_bit_mask = (1ULL << INPUT_GPIO);
    io_config.pull_up_en = 1;
    gpio_config(&io_config);

    gpio_set_intr_type(INPUT_GPIO, GPIO_INTR_ANYEDGE);

    gpio_event_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 10, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    gpio_isr_handler_add(INPUT_GPIO, gpio_isr_handler, (void *)INPUT_GPIO);

    gpio_set_level(OUTPUT_GPIO, 1);

    while (true)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
