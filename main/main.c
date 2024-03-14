#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "nvs_flash.h"
#include "main.h"

#define OUTPUT_GPIO GPIO_NUM_2
#define INPUT_GPIO GPIO_NUM_3

#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE in idf.py menuconfig to compile (End Device) source code.
#endif

char model_id[] = {14, 'E', 'S', 'P', '3', '2', 'C', '6', '.', 'L', 'i', 'g', 'h', 't', '1'};
char manufacture_name[] = {9, 'E', 's', 'p', 'r', 'e', 's', 's', 'i', 'f'};

static QueueHandle_t gpio_event_queue = NULL;

static const char *ZB_TAG = "ZIGBEE";

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
                ESP_EARLY_LOGI("BUTTON", "change");
            }
            // get_time();
        }
    }
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(
        esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK,
        ,
        ZB_TAG,
        "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_app_signal = signal_struct->p_app_signal;
    esp_err_t error_status = signal_struct->esp_err_status;

    esp_zb_app_signal_type_t signal_type = *p_app_signal;

    switch (signal_type)
    {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(ZB_TAG, "Initialize zigbee stack.");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (error_status == ESP_OK)
        {
            ESP_LOGI(ZB_TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");

            if (esp_zb_bdb_is_factory_new())
            {
                ESP_LOGI(ZB_TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_NETWORK_STEERING);
            }
            else
            {
                ESP_LOGI(ZB_TAG, "Device rebooted");
            }
        }
        else
        {
            ESP_LOGW(ZB_TAG, "Failed to initialize Zigbee stack, status: %s", esp_err_to_name(error_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (error_status == ESP_OK)
        {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(ZB_TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
        }
        else
        {
            ESP_LOGW(ZB_TAG, "Network steering was not successful, status: %s", esp_err_to_name(error_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(ZB_TAG, "Signal type: %hd", signal_type);
    }
}

static esp_err_t zigbee_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, ZB_TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS,
                        ESP_ERR_INVALID_ARG,
                        ZB_TAG,
                        "Received message: error status(%d)",
                        message->info.status);

    ESP_LOGI(ZB_TAG, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);

    if (message->info.dst_endpoint == HA_ESP_LIGHT_ENDPOINT)
    {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF)
        {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL)
            {
                light = message->attribute.data.value ? *(bool *)message->attribute.data.value : light;
                ESP_LOGI(ZB_TAG, "Light sets to %s", light ? "On" : "Off");
                gpio_set_level(OUTPUT_GPIO, light ? 1 : 0);
            }
        }

        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY)
        {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID)
            {
                ESP_LOGI(ZB_TAG, "Identify value %d", *(uint16_t *)message->attribute.data.value);
            }
        }
    }

    return ret;
}

static esp_err_t zigbee_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id)
    {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zigbee_attribute_handler(message);
        break;
    default:
        ESP_LOGW(ZB_TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}

void zigbee_task(void *args)
{
    esp_zb_cfg_t nwk_config = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED, // end device
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = 3000

        }};
    esp_zb_init(&nwk_config);

    uint8_t version_id = 0;
    uint8_t zcl_version_id = 4;

    esp_zb_attribute_list_t *esp_zb_basic_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &version_id);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &zcl_version_id);
    esp_zb_cluster_update_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &zcl_version_id);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, &model_id[0]);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, &manufacture_name[0]);

    esp_zb_attribute_list_t *esp_zb_identify_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY);
    esp_zb_identify_cluster_add_attr(esp_zb_identify_cluster, ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID, &version_id);

    /* group cluster create with fully customized */
    esp_zb_attribute_list_t *esp_zb_groups_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_GROUPS);
    esp_zb_groups_cluster_add_attr(esp_zb_groups_cluster, ESP_ZB_ZCL_ATTR_GROUPS_NAME_SUPPORT_ID, &version_id);

    /* scenes cluster create with standard cluster + customized */
    esp_zb_attribute_list_t *esp_zb_scenes_cluster = esp_zb_scenes_cluster_create(NULL);
    esp_zb_cluster_update_attr(esp_zb_scenes_cluster, ESP_ZB_ZCL_ATTR_SCENES_NAME_SUPPORT_ID, &version_id);

    /* on-off cluster create with standard cluster config*/
    esp_zb_on_off_cluster_cfg_t on_off_cfg;
    on_off_cfg.on_off = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE;
    esp_zb_attribute_list_t *esp_zb_on_off_cluster = esp_zb_on_off_cluster_create(&on_off_cfg);

    /* create cluster lists for this endpoint */
    esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* update basic cluster in the existed cluster list */
    // esp_zb_cluster_list_update_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(esp_zb_cluster_list, esp_zb_identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_groups_cluster(esp_zb_cluster_list, esp_zb_groups_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_scenes_cluster(esp_zb_cluster_list, esp_zb_scenes_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_on_off_cluster(esp_zb_cluster_list, esp_zb_on_off_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t esp_zb_ep_config = {
        .endpoint = HA_ESP_LIGHT_ENDPOINT,
        .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
    };
    /* add created endpoint (cluster_list) to endpoint list */
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, esp_zb_ep_config);
    esp_zb_device_register(esp_zb_ep_list);

    // esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
    // esp_zb_ep_list_t *esp_zb_light_ep = esp_zb_on_off_light_ep_create(HA_ESP_LIGHT_ENDPOINT, &light_cfg);
    // esp_zb_on_off_switch_cfg_t switch_cfg = ESP_ZB_DEFAULT_ON_OFF_SWITCH_CONFIG();
    // esp_zb_ep_list_t *esp_zb_switch_ep = esp_zb_on_off_switch_ep_create(1, &switch_cfg);

    // esp_zb_light_ep->next = esp_zb_switch_ep;

    // esp_zb_device_register(esp_zb_switch_ep);
    esp_zb_core_action_handler_register(zigbee_action_handler);

    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
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

    ESP_ERROR_CHECK(nvs_flash_init());

    xTaskCreate(zigbee_task, "Zigbee Task", 4096, NULL, 5, NULL);
}
