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

void app_main(void)
{
    audio_pipeline_handle_t pipeline1;
    audio_pipeline_handle_t pipeline2;
    audio_pipeline_handle_t pipeline3;
    audio_pipeline_handle_t pipeline_mix;
    audio_element_handle_t spiffs_stream_reader, i2s_stream_writer, mp3_decoder;
    audio_element_handle_t spiffs_stream_reader2, mp3_decoder2;
    audio_element_handle_t spiffs_stream_reader3, mp3_decoder3;
    audio_element_handle_t raw_write_el_1, raw_write_el_2, raw_write_el_3;
    audio_element_handle_t mixer;

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
    pipeline1 = audio_pipeline_init(&pipeline_cfg);
    pipeline2 = audio_pipeline_init(&pipeline_cfg);
    pipeline3 = audio_pipeline_init(&pipeline_cfg);
    pipeline_mix = audio_pipeline_init(&pipeline_cfg);
    AUDIO_NULL_CHECK(TAG, pipeline1, return);
    AUDIO_NULL_CHECK(TAG, pipeline2, return);
    AUDIO_NULL_CHECK(TAG, pipeline3, return);
    AUDIO_NULL_CHECK(TAG, pipeline_mix, return);

    ESP_LOGI(TAG, "[3.1] Create spiffs stream to read data from sdcard");
    spiffs_stream_cfg_t flash_cfg = SPIFFS_STREAM_CFG_DEFAULT();
    flash_cfg.type = AUDIO_STREAM_READER;
    spiffs_stream_reader = spiffs_stream_init(&flash_cfg);
    spiffs_stream_cfg_t flash_cfg2 = SPIFFS_STREAM_CFG_DEFAULT();
    flash_cfg2.type = AUDIO_STREAM_READER;
    spiffs_stream_reader2 = spiffs_stream_init(&flash_cfg2);
    spiffs_stream_cfg_t flash_cfg3 = SPIFFS_STREAM_CFG_DEFAULT();
    flash_cfg3.type = AUDIO_STREAM_READER;
    spiffs_stream_reader3 = spiffs_stream_init(&flash_cfg3);

    ESP_LOGI(TAG, "[3.2] Create i2s stream to write data to codec chip");
#if defined CONFIG_ESP32_C3_LYRA_V2_BOARD
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_PDM_TX_CFG_DEFAULT();
#else
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
#endif
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[3.3] Create mp3 decoder to decode mp3 file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
    mp3_decoder_cfg_t mp3_cfg2 = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder2 = mp3_decoder_init(&mp3_cfg2);
    mp3_decoder_cfg_t mp3_cfg3 = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder3 = mp3_decoder_init(&mp3_cfg3);

    ESP_LOGI(TAG, "[3.4] Create downmix");
    downmix_cfg_t mixer_cfg = DEFAULT_DOWNMIX_CONFIG();
    mixer_cfg.downmix_info.source_num = 3;
    mixer_cfg.downmix_info.out_ctx = ESP_DOWNMIX_OUT_CTX_NORMAL;
    mixer_cfg.stack_in_ext = false;
    mixer = downmix_init(&mixer_cfg);
    downmix_set_input_rb_timeout(mixer, 0, 0);
    downmix_set_input_rb_timeout(mixer, 0, 1);
    downmix_set_input_rb_timeout(mixer, 0, 2);
    downmix_set_output_type(mixer, ESP_DOWNMIX_OUTPUT_TYPE_ONE_CHANNEL);
    downmix_set_out_ctx_info(mixer, ESP_DOWNMIX_OUT_CTX_NORMAL);

#define SAMPLERATE 48000
#define NUM_INPUT_CHANNEL 1
#define TRANSITTIME 0

    esp_downmix_input_info_t source_info[3] = {0};
    esp_downmix_input_info_t source_info_0= {
        .samplerate = SAMPLERATE,
        .channel = NUM_INPUT_CHANNEL,
        .bits_num = 16,
        /* base music depress form 0dB to -10dB */
        .gain = {0, 0},
        .transit_time = TRANSITTIME,
    };
    source_info[0] = source_info_0;
    esp_downmix_input_info_t source_info_1= {
        .samplerate = SAMPLERATE,
        .channel = NUM_INPUT_CHANNEL,
        .bits_num = 16,
        .gain = {0, -10}, 
        .transit_time = TRANSITTIME,
    };
    source_info[1] = source_info_1;
    esp_downmix_input_info_t source_info_2= {
        .samplerate = SAMPLERATE,
        .channel = NUM_INPUT_CHANNEL,
        .bits_num = 16,
        .gain = {0, -10},
        .transit_time = TRANSITTIME,
    };
    source_info[2] = source_info_2;
    source_info_init(mixer, source_info);

    ESP_LOGI(TAG, "[3.5] Create raw stream of base mp3 to write data");
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    raw_write_el_1 = raw_stream_init(&raw_cfg);
    raw_write_el_2 = raw_stream_init(&raw_cfg);
    raw_write_el_3 = raw_stream_init(&raw_cfg);

    ESP_LOGI(TAG, "[3.5] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline1, spiffs_stream_reader, "spiffs1");
    audio_pipeline_register(pipeline1, mp3_decoder, "mp3_1");
    audio_pipeline_register(pipeline1, raw_write_el_1, "raw1");
    audio_pipeline_register(pipeline2, spiffs_stream_reader2, "spiffs2");
    audio_pipeline_register(pipeline2, mp3_decoder2, "mp3_2");
    audio_pipeline_register(pipeline2, raw_write_el_2, "raw2");
    audio_pipeline_register(pipeline3, spiffs_stream_reader3, "spiffs3");
    audio_pipeline_register(pipeline3, mp3_decoder3, "mp3_3");
    audio_pipeline_register(pipeline3, raw_write_el_3, "raw3");
    audio_pipeline_register(pipeline_mix, mixer, "mixer");
    audio_pipeline_register(pipeline_mix, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[3.6] Link it together [flash]-->spiffs-->mp3_decoder-->raw");
    const char *link_tag[3] = {"spiffs1", "mp3_1", "raw1"};
    audio_pipeline_link(pipeline1, &link_tag[0], 3);
    const char *link_tag2[3] = {"spiffs2", "mp3_2", "raw2"};
    audio_pipeline_link(pipeline2, &link_tag2[0], 3);
    const char *link_tag3[3] = {"spiffs3", "mp3_3", "raw3"};
    audio_pipeline_link(pipeline3, &link_tag3[0], 3);

    ESP_LOGI(TAG, "[3.7] Link elements together downmixer-->i2s_writer");
    const char *link_tag_mix[2] = {"mixer", "i2s"};
    audio_pipeline_link(pipeline_mix, &link_tag_mix[0], 2);

    ESP_LOGI(TAG, "[3.7] Set up uri");
    audio_element_set_uri(spiffs_stream_reader, "/spiffs/10.mp3");
    audio_element_set_uri(spiffs_stream_reader2, "/spiffs/09.mp3");
    audio_element_set_uri(spiffs_stream_reader3, "/spiffs/08.mp3");

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    ringbuf_handle_t rb_1 = audio_element_get_input_ringbuf(raw_write_el_1);
    downmix_set_input_rb(mixer, rb_1, 0);
    audio_pipeline_set_listener(pipeline1, evt);
    
    ringbuf_handle_t rb_2 = audio_element_get_input_ringbuf(raw_write_el_2);
    downmix_set_input_rb(mixer, rb_2, 1);
    audio_pipeline_set_listener(pipeline2, evt);

    ringbuf_handle_t rb_3 = audio_element_get_input_ringbuf(raw_write_el_3);
    downmix_set_input_rb(mixer, rb_3, 2);
    audio_pipeline_set_listener(pipeline3, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_pipeline_set_listener(pipeline_mix, evt);
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline1);
    audio_pipeline_run(pipeline2);
    audio_pipeline_run(pipeline3);
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

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
            && (msg.source == (void *)mp3_decoder || msg.source == (void *)mp3_decoder2 || msg.source == (void *)mp3_decoder3)
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(msg.source, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder %x, sample_rates=%d, bits=%d, ch=%d",
                     msg.source, music_info.sample_rates, music_info.bits, music_info.channels);

            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);

            downmix_set_work_mode(mixer, ESP_DOWNMIX_WORK_MODE_SWITCH_ON);
            downmix_set_input_rb_timeout(mixer, 50, 0);
            downmix_set_input_rb_timeout(mixer, 50, 1);
            downmix_set_input_rb_timeout(mixer, 50, 2);
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
    audio_pipeline_stop(pipeline1);
    audio_pipeline_stop(pipeline2);
    audio_pipeline_stop(pipeline3);
    audio_pipeline_stop(pipeline_mix);
    audio_pipeline_wait_for_stop(pipeline1);
    audio_pipeline_wait_for_stop(pipeline2);
    audio_pipeline_wait_for_stop(pipeline3);
    audio_pipeline_wait_for_stop(pipeline_mix);
    audio_pipeline_terminate(pipeline1);
    audio_pipeline_terminate(pipeline2);
    audio_pipeline_terminate(pipeline3);
    audio_pipeline_terminate(pipeline_mix);

    audio_pipeline_unregister(pipeline1, spiffs_stream_reader);
    audio_pipeline_unregister(pipeline1, mp3_decoder);
    audio_pipeline_unregister(pipeline1, raw_write_el_1);
    audio_pipeline_unregister(pipeline2, spiffs_stream_reader2);
    audio_pipeline_unregister(pipeline2, mp3_decoder2);
    audio_pipeline_unregister(pipeline2, raw_write_el_2);
    audio_pipeline_unregister(pipeline3, spiffs_stream_reader3);
    audio_pipeline_unregister(pipeline3, mp3_decoder3);
    audio_pipeline_unregister(pipeline3, raw_write_el_3);
    audio_pipeline_unregister(pipeline_mix, i2s_stream_writer);
    audio_pipeline_unregister(pipeline_mix, mixer);

    /* Terminal the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline1);
    audio_pipeline_remove_listener(pipeline2);
    audio_pipeline_remove_listener(pipeline3);
    audio_pipeline_remove_listener(pipeline_mix);

    /* Stop all periph before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline1);
    audio_pipeline_deinit(pipeline2);
    audio_pipeline_deinit(pipeline3);
    audio_pipeline_deinit(pipeline_mix);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(spiffs_stream_reader);
    audio_element_deinit(mp3_decoder);
    audio_element_deinit(raw_write_el_1);
    audio_element_deinit(spiffs_stream_reader2);
    audio_element_deinit(mp3_decoder2);
    audio_element_deinit(raw_write_el_2);
    audio_element_deinit(spiffs_stream_reader3);
    audio_element_deinit(mp3_decoder3);
    audio_element_deinit(raw_write_el_3);
    audio_element_deinit(mixer);
    esp_periph_set_destroy(set);
}
