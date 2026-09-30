/**
 * @file esp_ble_hid.c
 * @brief Bluetooth LE HID host (NimBLE + esp_hidh) for the ESP32-S3 Quake port.
 *
 * Scans for BLE HID devices (keyboards, gamepads, mice), connects to the first
 * one found (bonded devices first), and reconnects automatically. To pair a
 * new device, put it in pairing mode: it will be picked up by the next scan.
 * Only Bluetooth LE is supported by the ESP32-S3 (no Bluetooth Classic).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_hidh.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "esp_input.h"
#include "board_config.h"

#define HID_SERVICE_UUID            0x1812
#define SCAN_TIME_MS                4000
#define RECONNECT_DELAY_MS          1000
#define BLE_HID_TASK_CORE           0

static const char *TAG = "blehid";

typedef struct
{
    ble_addr_t addr;
    int8_t rssi;
    bool bonded;
    bool valid;
    char name[24];
} candidate_t;

static SemaphoreHandle_t scanDone;
static SemaphoreHandle_t deviceClosed;
static candidate_t candidate;
static volatile bool connected;
static volatile bool noReportFields;
static int scanAdverts;                 // advertising reports seen in the current scan (diagnostics)
static uint8_t ownAddrType;
static struct ble_gap_event_listener gapListener;

void ble_store_config_init(void);

static bool isBonded(const ble_addr_t *addr)
{
    struct ble_store_key_sec key = { 0 };
    struct ble_store_value_sec value;
    key.peer_addr = *addr;
    return ble_store_read_peer_sec(&key, &value) == 0;
}

static void considerDevice(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    bool isHid = false;
    // directed advertising towards us: a bonded device wants to reconnect
    if (disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)
    {
        isHid = true;
    }
    memset(&fields, 0, sizeof(fields));
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) == 0)
    {
        for (int i = 0; i < fields.num_uuids16; i++)
        {
            if (ble_uuid_u16(&fields.uuids16[i].u) == HID_SERVICE_UUID)
            {
                isHid = true;
            }
        }
        // appearance: HID category (0x03C0 - 0x03CF)
        if (fields.appearance_is_present && (fields.appearance & 0xFFC0) == 0x03C0)
        {
            isHid = true;
        }
    }
    bool bonded = isBonded(&disc->addr);
    scanAdverts++;
#if BLE_HID_DEBUG
    if (disc->rssi > -80)
    {
        // diagnostics: every nearby advertiser
        char name[24] = "";
        if (fields.name && fields.name_len)
        {
            int n = fields.name_len < sizeof(name) - 1 ? fields.name_len : sizeof(name) - 1;
            memcpy(name, fields.name, n);
            name[n] = 0;
        }
        ESP_LOGI(TAG, "adv %02x:%02x:%02x:%02x:%02x:%02x type %d evt %d rssi %d uuids16 %d appearance %s0x%04x \"%s\"%s%s",
            disc->addr.val[5], disc->addr.val[4], disc->addr.val[3], disc->addr.val[2], disc->addr.val[1], disc->addr.val[0],
            disc->addr.type, disc->event_type, disc->rssi, fields.num_uuids16, fields.appearance_is_present ? "" : "-",
            fields.appearance, name, isHid ? " HID" : "", bonded ? " bonded" : "");
    }
#endif
    if (!isHid && !bonded)
    {
        return;
    }
    if (bonded && disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND && disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)
    {
        return;     // not connectable
    }
    // prefer bonded devices, then the strongest signal
    if (!candidate.valid || (bonded && !candidate.bonded) || (bonded == candidate.bonded && disc->rssi > candidate.rssi))
    {
        candidate.addr = disc->addr;
        candidate.rssi = disc->rssi;
        candidate.bonded = bonded;
        candidate.valid = true;
        if (fields.name && fields.name_len)
        {
            int n = fields.name_len < sizeof(candidate.name) - 1 ? fields.name_len : sizeof(candidate.name) - 1;
            memcpy(candidate.name, fields.name, n);
            candidate.name[n] = 0;
        }
        else if (!bonded || !candidate.name[0])
        {
            strcpy(candidate.name, "?");
        }
    }
}

static int scanEvent(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
        case BLE_GAP_EVENT_DISC:
            considerDevice(&event->disc);
            break;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            xSemaphoreGive(scanDone);
            break;
        default:
            break;
    }
    return 0;
}

// Listener for events of the connection managed by esp_hidh
static int gapListenerEvent(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0)
            {
                // HID devices normally require an encrypted link: start pairing / encryption at once.
                ble_gap_security_initiate(event->connect.conn_handle);
            }
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(TAG, "Encryption %s (status %d)", event->enc_change.status ? "failed" : "enabled", event->enc_change.status);
            break;
        default:
            break;
    }
    return 0;
}

static void hidhCallback(void *handlerArgs, esp_event_base_t base, int32_t id, void *eventData)
{
    esp_hidh_event_t event = (esp_hidh_event_t) id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t*) eventData;
    switch (event)
    {
        case ESP_HIDH_OPEN_EVENT:
        {
            if (param->open.status != ESP_OK)
            {
                ESP_LOGE(TAG, "Open failed");
                break;
            }
            size_t numMaps = 0;
            esp_hid_raw_report_map_t *maps = NULL;
            esp_hidh_dev_report_maps_get(param->open.dev, &numMaps, &maps);
            const uint8_t *data[4];
            uint16_t lengths[4];
            if (numMaps > 4)
            {
                numMaps = 4;
            }
            for (size_t i = 0; i < numMaps; i++)
            {
                data[i] = maps[i].data;
                lengths[i] = maps[i].len;
            }
            inputHidOpened(data, lengths, numMaps);
            const char *name = esp_hidh_dev_name_get(param->open.dev);
            ESP_LOGI(TAG, "Connected to %s, %u report maps", name ? name : "?", (unsigned) numMaps);
            noReportFields = !inputHidHasFields();
            connected = true;
            break;
        }
        case ESP_HIDH_INPUT_EVENT:
            inputHidReport(param->input.map_index, param->input.report_id, param->input.data, param->input.length);
            break;
        case ESP_HIDH_BATTERY_EVENT:
            ESP_LOGI(TAG, "Battery: %d%%", param->battery.level);
            break;
        case ESP_HIDH_CLOSE_EVENT:
            ESP_LOGI(TAG, "Disconnected (reason %d)", param->close.reason);
            inputHidClosed();
            connected = false;
            xSemaphoreGive(deviceClosed);
            break;
        default:
            break;
    }
}

static void nimbleHostTask(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void bleHidTask(void *arg)
{
    // wait for the host to be synchronized with the controller
    while (!ble_hs_synced())
    {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &ownAddrType);
    ble_gap_event_listener_register(&gapListener, gapListenerEvent, NULL);
    ESP_LOGI(TAG, "Host synced, own address type %d, scanning", ownAddrType);
    while (1)
    {
        // scan
        memset(&candidate, 0, sizeof(candidate));
        struct ble_gap_disc_params params = { 0 };
        params.filter_duplicates = 1;
        params.passive = 0;
        params.itvl = 0x50;
        params.window = 0x30;
        scanAdverts = 0;
        int rc = ble_gap_disc(ownAddrType, SCAN_TIME_MS, &params, scanEvent, NULL);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "ble_gap_disc() failed: %d", rc);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }
        xSemaphoreTake(scanDone, portMAX_DELAY);
        if (!candidate.valid)
        {
            ESP_LOGD(TAG, "Scan done: %d adverts, no HID device", scanAdverts);
            continue;
        }
        ESP_LOGI(TAG, "Connecting to %s (%02x:%02x:%02x:%02x:%02x:%02x)%s", candidate.name,
            candidate.addr.val[5], candidate.addr.val[4], candidate.addr.val[3],
            candidate.addr.val[2], candidate.addr.val[1], candidate.addr.val[0], candidate.bonded ? ", bonded" : "");
        xSemaphoreTake(deviceClosed, 0);
        esp_hidh_dev_t *dev = esp_hidh_dev_open(candidate.addr.val, ESP_HID_TRANSPORT_BLE, candidate.addr.type);
        if (!dev)
        {
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }
        // Report map unreadable (typically because the link was not encrypted yet on the
        // very first connection): now we are bonded, so reconnect.
        vTaskDelay(pdMS_TO_TICKS(500));
        if (connected && noReportFields)
        {
            ESP_LOGW(TAG, "No usable HID report map, reconnecting");
            esp_hidh_dev_close(dev);
        }
        xSemaphoreTake(deviceClosed, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
    }
}

void bleHidInit(void)
{
#if BLE_HID_ENABLED
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    scanDone = xSemaphoreCreateBinary();
    deviceClosed = xSemaphoreCreateBinary();
    //
    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "nimble_port_init() failed: %d", ret);
        return;
    }
    // Security: "just works" bonding, accepted by virtually all HID devices
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    //
    esp_hidh_config_t config =
    {
        .callback = hidhCallback,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&config));
    nimble_port_freertos_init(nimbleHostTask);
    xTaskCreatePinnedToCoreWithCaps(bleHidTask, "blehid", 4096, NULL, 5, NULL, BLE_HID_TASK_CORE, MALLOC_CAP_SPIRAM);
#endif
}
