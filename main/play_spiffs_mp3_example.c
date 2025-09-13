/* Play MP3 file from Flash(spiffs system)

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "i2s_stream.h"
#include "spiffs_stream.h"
#include "mp3_decoder.h"
#include "raw_stream.h"
#include "downmix.h"

#include "esp_peripherals.h"
#include "periph_spiffs.h"
#include "board.h"

static const char *TAG = "SPIFFS_MP3_EXAMPLE";

#define NUM_DECODE_PIPELINES 4

static const char *mp3_files[NUM_DECODE_PIPELINES] = {
    "/spiffs/10.mp3",
    "/spiffs/09.mp3",
    "/spiffs/08.mp3",
    "/spiffs/07.mp3"
};

void app_main(void)
{
    audio_pipeline_handle_t pipelines[NUM_DECODE_PIPELINES];
    audio_element_handle_t spiffs_stream_readers[NUM_DECODE_PIPELINES];
    audio_element_handle_t mp3_decoders[NUM_DECODE_PIPELINES];
    audio_element_handle_t raw_write_els[NUM_DECODE_PIPELINES];
    audio_pipeline_handle_t pipeline_mix;
    audio_element_handle_t i2s_stream_writer, mixer;

    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    // Initialize peripherals management
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[ 1 ] Mount spiffs");
    // Initialize Spiffs peripheral
    periph_spiffs_cfg_t spiffs_cfg = {
        .root = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_periph_handle_t spiffs_handle = periph_spiffs_init(&spiffs_cfg);

    // Start spiffs
    esp_periph_start(set, spiffs_handle);

    // Wait until spiffs is mounted
    while (!periph_spiffs_is_mounted(spiffs_handle)) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "[3.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        pipelines[i] = audio_pipeline_init(&pipeline_cfg);
        AUDIO_NULL_CHECK(TAG, pipelines[i], return);
    }
    pipeline_mix = audio_pipeline_init(&pipeline_cfg);
    AUDIO_NULL_CHECK(TAG, pipeline_mix, return);

    ESP_LOGI(TAG, "[3.1] Create spiffs stream to read data from sdcard");
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        spiffs_stream_cfg_t flash_cfg = SPIFFS_STREAM_CFG_DEFAULT();
        flash_cfg.type = AUDIO_STREAM_READER;
        spiffs_stream_readers[i] = spiffs_stream_init(&flash_cfg);
    }

    ESP_LOGI(TAG, "[3.2] Create i2s stream to write data to codec chip");
#if defined CONFIG_ESP32_C3_LYRA_V2_BOARD
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_PDM_TX_CFG_DEFAULT();
#else
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
#endif
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[3.3] Create mp3 decoder to decode mp3 file");
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
        mp3_decoders[i] = mp3_decoder_init(&mp3_cfg);
    }

    ESP_LOGI(TAG, "[3.4] Create downmix");
    downmix_cfg_t mixer_cfg = DEFAULT_DOWNMIX_CONFIG();
    mixer_cfg.downmix_info.source_num = NUM_DECODE_PIPELINES;
    mixer_cfg.downmix_info.out_ctx = ESP_DOWNMIX_OUT_CTX_NORMAL;
    mixer_cfg.stack_in_ext = false;
    mixer = downmix_init(&mixer_cfg);
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        downmix_set_input_rb_timeout(mixer, 0, i);
    }
    downmix_set_output_type(mixer, ESP_DOWNMIX_OUTPUT_TYPE_ONE_CHANNEL);
    downmix_set_out_ctx_info(mixer, ESP_DOWNMIX_OUT_CTX_NORMAL);

#define SAMPLERATE 48000
#define NUM_INPUT_CHANNEL 1
#define TRANSITTIME 0

    esp_downmix_input_info_t source_info[NUM_DECODE_PIPELINES] = {0};
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        source_info[i].samplerate = SAMPLERATE;
        source_info[i].channel = NUM_INPUT_CHANNEL;
        source_info[i].bits_num = 16;
        source_info[i].gain[0] = 0;
        source_info[i].gain[1] = -10;
        source_info[i].transit_time = TRANSITTIME;
    }
    source_info_init(mixer, source_info);

    ESP_LOGI(TAG, "[3.5] Create raw stream of base mp3 to write data");
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
        raw_cfg.type = AUDIO_STREAM_WRITER;
        raw_write_els[i] = raw_stream_init(&raw_cfg);
    }

    ESP_LOGI(TAG, "[3.5] Register all elements to audio pipeline");
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        char spiffs_name[10], mp3_name[10], raw_name[10];
        sprintf(spiffs_name, "spiffs%d", i + 1);
        sprintf(mp3_name, "mp3_%d", i + 1);
        sprintf(raw_name, "raw%d", i + 1);
        audio_pipeline_register(pipelines[i], spiffs_stream_readers[i], spiffs_name);
        audio_pipeline_register(pipelines[i], mp3_decoders[i], mp3_name);
        audio_pipeline_register(pipelines[i], raw_write_els[i], raw_name);
    }
    audio_pipeline_register(pipeline_mix, mixer, "mixer");
    audio_pipeline_register(pipeline_mix, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[3.6] Link it together [flash]-->spiffs-->mp3_decoder-->raw");
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        char spiffs_name[10], mp3_name[10], raw_name[10];
        sprintf(spiffs_name, "spiffs%d", i + 1);
        sprintf(mp3_name, "mp3_%d", i + 1);
        sprintf(raw_name, "raw%d", i + 1);
        const char *link_tag[3] = {spiffs_name, mp3_name, raw_name};
        audio_pipeline_link(pipelines[i], &link_tag[0], 3);
    }

    ESP_LOGI(TAG, "[3.7] Link elements together downmixer-->i2s_writer");
    const char *link_tag_mix[2] = {"mixer", "i2s"};
    audio_pipeline_link(pipeline_mix, &link_tag_mix[0], 2);

    ESP_LOGI(TAG, "[3.7] Set up uri");
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        audio_element_set_uri(spiffs_stream_readers[i], mp3_files[i]);
    }

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        ringbuf_handle_t rb = audio_element_get_input_ringbuf(raw_write_els[i]);
        downmix_set_input_rb(mixer, rb, i);
        audio_pipeline_set_listener(pipelines[i], evt);
    }

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_pipeline_set_listener(pipeline_mix, evt);
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        audio_pipeline_run(pipelines[i]);
    }
    audio_pipeline_run(pipeline_mix);

    downmix_set_input_rb_timeout(mixer, 50, 0);
    downmix_set_work_mode(mixer, ESP_DOWNMIX_WORK_MODE_BYPASS);

    ESP_LOGI(TAG, "[ 6 ] Listen for all pipeline events");

    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        bool is_decoder_event = false;
        for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
            if (msg.source == (void *)mp3_decoders[i]) {
                is_decoder_event = true;
                break;
            }
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && is_decoder_event && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(msg.source, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder %p, sample_rates=%d, bits=%d, ch=%d",
                     msg.source, music_info.sample_rates, music_info.bits, music_info.channels);

            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);

            downmix_set_work_mode(mixer, ESP_DOWNMIX_WORK_MODE_SWITCH_ON);
            for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
                downmix_set_input_rb_timeout(mixer, 50, i);
            }
            continue;
        }

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            break;
        }
    }

    ESP_LOGI(TAG, "[ 7 ] Stop audio_pipeline");
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        audio_pipeline_stop(pipelines[i]);
        audio_pipeline_wait_for_stop(pipelines[i]);
        audio_pipeline_terminate(pipelines[i]);
        audio_pipeline_unregister(pipelines[i], spiffs_stream_readers[i]);
        audio_pipeline_unregister(pipelines[i], mp3_decoders[i]);
        audio_pipeline_unregister(pipelines[i], raw_write_els[i]);
        audio_pipeline_remove_listener(pipelines[i]);
    }
    audio_pipeline_stop(pipeline_mix);
    audio_pipeline_wait_for_stop(pipeline_mix);
    audio_pipeline_terminate(pipeline_mix);
    audio_pipeline_unregister(pipeline_mix, i2s_stream_writer);
    audio_pipeline_unregister(pipeline_mix, mixer);
    audio_pipeline_remove_listener(pipeline_mix);

    /* Stop all periph before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    for (int i = 0; i < NUM_DECODE_PIPELINES; i++) {
        audio_pipeline_deinit(pipelines[i]);
        audio_element_deinit(spiffs_stream_readers[i]);
        audio_element_deinit(mp3_decoders[i]);
        audio_element_deinit(raw_write_els[i]);
    }
    audio_pipeline_deinit(pipeline_mix);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(mixer);
    esp_periph_set_destroy(set);
}