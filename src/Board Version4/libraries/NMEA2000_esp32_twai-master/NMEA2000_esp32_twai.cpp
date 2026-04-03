/*
2022 Copyright (c) Sergei Podshivalov  All right reserved.

Author: Sergei Podshivalov

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 Inherited object for use NMEA2000 library for ESP32 Boards.
See also https://github.com/ttlappalainen/NMEA2000[NMEA2000] library.

To use this library, you will also need NMEA2000 library.

The library defines as default Tx pin to GPIO 32 and Rx pint to GPIO 34. You can
change these with defines:

  #define ESP32_CAN_TX_PIN GPIO_NUM_32
  #define ESP32_CAN_RX_PIN GPIO_NUM_34

before including NMEA2000_CAN.h or NMEA2000_esp32_twai.h

 */

#include <esp_log.h>
#include "NMEA2000_esp32_twai.h"

static const char *TAG = "NMEA2000_esp32_twai";
static const int ERROR_ALERTS_TO_WATCH = TWAI_ALERT_ABOVE_ERR_WARN | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF;
static const int DATA_EVENTS_TO_WATCH = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_RX_DATA;
static const int ALERTS_TO_WATCH = ERROR_ALERTS_TO_WATCH | DATA_EVENTS_TO_WATCH;

#define CTRL_TASK_PRIO                  10
static void ctrl_task(void *arg){
    ((NMEA2000_esp32_twai *)arg)->CtrlTask();
}

NMEA2000_esp32_twai::NMEA2000_esp32_twai(gpio_num_t txPin, gpio_num_t rxPin, twai_mode_t twaiMode,
                                         uint32_t txQueueLen, uint32_t rxQueueLen)
    :m_TxPin(txPin)
    ,m_RxPin(rxPin)
    ,m_twaiMode(twaiMode)
    ,m_txQueueLen(txQueueLen)
    ,m_rxQueueLen(rxQueueLen)
{
    ctrl_task_sem = xSemaphoreCreateBinary();
    sideMessageQueue = xQueueCreate(10, sizeof(twai_message_t));
}

bool NMEA2000_esp32_twai::CANOpen() {

    //Initialize configuration structures using macro initializers
    static twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(m_TxPin, m_RxPin, m_twaiMode);
    g_config.tx_queue_len = m_txQueueLen;
    g_config.rx_queue_len = m_rxQueueLen;
    g_config.alerts_enabled = ALERTS_TO_WATCH;
    static const twai_timing_config_t  t_config = TWAI_TIMING_CONFIG_250KBITS();
    static const twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    //Install TWAI driver
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        ESP_LOGD(TAG,"Driver installed");
    } else {
        ESP_LOGE(TAG,"Failed to install driver");
        return false;
    }

    // Start TWAI driver
    if (twai_start() == ESP_OK) {
        ESP_LOGD(TAG,"Driver started");
    } else {
        ESP_LOGE(TAG,"Failed to start driver\n");
        return false;
    }

    // Create control task
    xTaskCreatePinnedToCore(ctrl_task, "TWAI_ctrl", 4096, this, CTRL_TASK_PRIO, nullptr, tskNO_AFFINITY);
    // Let it run
    xSemaphoreGive(ctrl_task_sem);

    return true;
}

bool NMEA2000_esp32_twai::CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf, bool wait_sent) {

    // Check if the driver is in the running state before trying to transmit
    twai_status_info_t status_info;
    twai_get_status_info(&status_info);
    if ( status_info.state != TWAI_STATE_RUNNING){
        ESP_LOGD(TAG, "Failed to send CAN Frame: Driver is not in running state (%d)", status_info.state);
        return false;
    }

    twai_message_t message;
    memset(&message, 0, sizeof(message));
    message.extd = 1;
    message.identifier = id;
    message.data_length_code = len;
    memcpy(message.data, buf, len);

    // Send frame to side interface listeners
    if ( ! sideInterfaceSuspended ){
        for(int i = 0; i < m_listenerCount ; i++){
            m_listeners[i]->onTwaiFrameTransmit(id, len, buf);
        }
    }

    //Queue message for transmission
    if (twai_transmit(&message, 0) == ESP_OK) {  // Use the driver queue to hold messages
        ESP_LOGD(TAG,"Message queued for transmission\n");
        return true;
    } else {
        ESP_LOGI(TAG,"Failed to queue message for transmission\n");
        return false;
    }

}

bool NMEA2000_esp32_twai::CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) {
    twai_message_t message;

    // Check for message to be received from the side interface
    if (xQueueReceive(sideMessageQueue, &message, pdMS_TO_TICKS(0))){
        id = message.identifier;
        len = message.data_length_code;
        memcpy(buf, message.data,message.data_length_code);
        ESP_LOGD(TAG,"Got side message id=%08lX, len=%d, data=%08X", id, len, (uint32_t)*buf);
        return true;
    }

    // Wait for message to be received from the physical TWAI bus
    if (twai_receive(&message, pdMS_TO_TICKS(0)) == ESP_OK) {
        id = message.identifier;
        len = message.data_length_code;
        memcpy(buf, message.data,message.data_length_code);
        ESP_LOGD(TAG,"Message received id=%08lX, len=%d, data=%08X", id, len, (uint32_t)*buf);

        // Send frame to listeners
        for(int i = 0; i < m_listenerCount; i++){
            m_listeners[i]->onTwaiFrameReceived(id, len, buf);
        }

        return true;
    } else {
        return false;
    }

}

[[noreturn]] void NMEA2000_esp32_twai::CtrlTask() {
    xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);

    while (true){
        uint32_t alerts;
        twai_read_alerts(&alerts, portMAX_DELAY);
        if ( m_alertsListener != nullptr){
            m_alertsListener->onAlert(alerts, alerts & ERROR_ALERTS_TO_WATCH);
        }
        if (alerts & TWAI_ALERT_ABOVE_ERR_WARN) {
            ESP_LOGW(TAG, "Surpassed Error Warning Limit");
        }
        if (alerts & TWAI_ALERT_ERR_PASS) {
            ESP_LOGW(TAG, "Entered Error Passive state");
        }
        if (alerts & TWAI_ALERT_BUS_OFF) {
            ESP_LOGW(TAG, "Bus Off state");
            //Prepare to initiate bus recovery, reconfigure alerts to detect bus recovery completion
            twai_reconfigure_alerts(TWAI_ALERT_BUS_RECOVERED, nullptr);
            ESP_LOGI(TAG, "Initiate bus recovery");
            twai_initiate_recovery();    // Needs 128 occurrences of bus free signal
        }
        if (alerts & TWAI_ALERT_BUS_RECOVERED) {
            // Bus recovery was successful
            ESP_LOGI(TAG, "Bus Recovered");
            // Start TWAI driver
            if (twai_start() == ESP_OK) {
                ESP_LOGI(TAG,"Driver started");
            } else {
                ESP_LOGE(TAG,"Failed to start driver\n");
            }
            // Start monitoring alerts again
            twai_reconfigure_alerts(ALERTS_TO_WATCH, nullptr);
        }
    }
}

void NMEA2000_esp32_twai::InjectSideTwaiFrame(unsigned long id, unsigned char len, const unsigned char *buf) {
    twai_message_t message;
    message.identifier = id;
    message.data_length_code = len;
    memcpy(message.data, buf, len);
    // Offer message to the side message queue
    xQueueSend(sideMessageQueue, &message, pdMS_TO_TICKS(0));
}

bool NMEA2000_esp32_twai::addBusListener(TwaiBusListener *listener) {
    // Add listener to the list
    if (m_listenerCount < MAX_TWAI_LISTENERS) {
        m_listeners[m_listenerCount++] = listener;
        return true;
    }
    return false;
}

void NMEA2000_esp32_twai::SuspendSideInterface(bool suspended) {
    this->sideInterfaceSuspended = suspended;
}

