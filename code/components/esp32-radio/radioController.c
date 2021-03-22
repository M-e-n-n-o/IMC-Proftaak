/* Play an MP3 file from HTTP

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"

#if __has_include("esp_idf_version.h")
#include "esp_idf_version.h"
#else
#define ESP_IDF_VERSION_VAL(major, minor, patch) 1
#endif

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

#include "radioController.h"

static void init();
static void update();
static void stop();
static void reset();
static int _http_stream_event_handle(http_stream_event_msg_t*);

static const char *TAG = "HTTP_MP3_EXAMPLE";

static int running = 1;
static int isInit = 0;
static int isPlaying = 0;

SemaphoreHandle_t radioMutex;

audio_pipeline_handle_t pipeline;
audio_element_handle_t http_stream_reader, i2s_stream_writer, mp3_decoder;
audio_event_iface_handle_t evt;
esp_periph_set_handle_t set;

void radio_switch(char channel[])
{    
    if (!isInit)
        return;

    char* ip = " ";

    xSemaphoreTake(radioMutex, portMAX_DELAY);

    if (strcmp(channel, "538") == 0)
        ip = "https://21253.live.streamtheworld.com/RADIO538.mp3";
    else if(strcmp(channel, "Qmusic") == 0)
        ip = "https://icecast-qmusicnl-cdp.triple-it.nl/Qmusic_nl_live_96.mp3";
    else if (strcmp(channel, "SKY") == 0)
        ip = "https://19993.live.streamtheworld.com/SKYRADIO.mp3";

    if (strcmp(ip, " ") != 0)
    {
        if (isPlaying)
            reset();
        audio_element_set_uri(http_stream_reader, ip);
        audio_pipeline_run(pipeline);
        isPlaying = 1;
    }

    xSemaphoreGive(radioMutex);
}

void radio_task(void *p)
{
    radioMutex = xSemaphoreCreateMutex();
    init();

    running = 1;
    while (running)
    {
        if (isPlaying)
        {
            xSemaphoreTake(radioMutex, portMAX_DELAY);
            update();
            xSemaphoreGive(radioMutex);
        }
        
        vTaskDelay(50 / portTICK_RATE_MS);
    }
    
    stop();
}

void radio_quit()
{
    xSemaphoreTake(radioMutex, portMAX_DELAY);
    running = 0;
    xSemaphoreGive(radioMutex);
}

static void update()
{
    audio_event_iface_msg_t msg;
    esp_err_t ret = audio_event_iface_listen(evt, &msg, 200);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
        return;
    }

    if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
        && msg.source == (void *) mp3_decoder
        && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
        audio_element_info_t music_info = {0};
        audio_element_getinfo(mp3_decoder, &music_info);

        ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                    music_info.sample_rates, music_info.bits, music_info.channels);

        audio_element_setinfo(i2s_stream_writer, &music_info);
        i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
        return;
    }

    /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
    if ((int)msg.data == AEL_STATUS_STATE_FINISHED) 
    {
        ESP_LOGW(TAG, "[ * ] Stop event received");
        reset();
    }
}

void init()
{
    xSemaphoreTake(radioMutex, portMAX_DELAY);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) 
    {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
	tcpip_adapter_init();
#else
    tcpip_adapter_init();
#endif

    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "[2.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "[2.1] Create http stream to read data");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.event_handle = _http_stream_event_handle;
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = true;
    http_stream_reader = http_stream_init(&http_cfg);

    ESP_LOGI(TAG, "[2.2] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[2.3] Create mp3 decoder to decode mp3 file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    ESP_LOGI(TAG, "[2.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_register(pipeline, mp3_decoder,        "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer,  "i2s");

    ESP_LOGI(TAG, "[2.5] Link it together http_stream-->mp3_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"http", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "[2.6] Set up  uri (http as http_stream, mp3 as mp3 decoder, and default output is i2s)");
    audio_element_set_uri(http_stream_reader, "https://icecast-qmusicnl-cdp.triple-it.nl/Qmusic_nl_live_96.mp3");
    
    static int isWifiInit = 0;
    ESP_LOGI(TAG, "[ 3 ] Start and wait for Wi-Fi network");
    static esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    set = esp_periph_set_init(&periph_cfg);
    periph_wifi_cfg_t wifi_cfg = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
    };

    ESP_LOGI(TAG, "[ 3.2 ] Start and wait for Wi-Fi network");
    static esp_periph_handle_t wifi_handle;
    wifi_handle = periph_wifi_init(&wifi_cfg);
    ESP_LOGI(TAG, "[ 3.3 ] Start and wait for Wi-Fi network");
    if (!isWifiInit)
    {
        esp_periph_start(set, wifi_handle);
        ESP_LOGI(TAG, "[ 3.4 ] Start and wait for Wi-Fi network");
        periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);
        isWifiInit = 1;
    }

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    isInit = 1;
    xSemaphoreGive(radioMutex);
}

void radio_reset()
{
    xSemaphoreTake(radioMutex, portMAX_DELAY);
    reset();
    xSemaphoreGive(radioMutex);
}

static void reset()
{
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_element_reset_state(mp3_decoder);
    audio_element_reset_state(i2s_stream_writer);
    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_items_state(pipeline);
    isPlaying = 0;
}

static void stop()
{
    ESP_LOGI(TAG, "[ 6 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_unregister(pipeline, http_stream_reader);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
    audio_pipeline_unregister(pipeline, mp3_decoder);

    audio_pipeline_remove_listener(pipeline);

    /* Stop all peripherals before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(http_stream_reader);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(mp3_decoder);
    // esp_periph_set_destroy(set);

    isPlaying = 0;
    isInit = 0;
    ESP_LOGI(TAG, "[ 7 ] Finished");
    vTaskDelete(NULL);
}

static int _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    if (msg->event_id == HTTP_STREAM_RESOLVE_ALL_TRACKS) 
        return ESP_OK;

    if (msg->event_id == HTTP_STREAM_FINISH_TRACK) 
        return http_stream_next_track(msg->el);
    if (msg->event_id == HTTP_STREAM_FINISH_PLAYLIST) 
        return http_stream_fetch_again(msg->el);
    return ESP_OK;
}