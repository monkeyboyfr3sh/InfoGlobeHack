#include "ir_led_task.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "message_type.h"

/*
 * Olympia Infoglobe IR link.
 *
 * Waveform:
 *   - 38.54 kHz carrier, 50 % duty
 *   - 1 ms per bit, bytes sent MSB first, back to back
 *   - bit 0 = carrier ON, bit 1 = LED off; idle = LED off
 *   - after each frame, a configurable idle gap (no start/stop bits in the
 *     protocol, so the globe uses idle time to find the next frame)
 *
 * The RMT peripheral generates everything - bits, carrier and the gap - from
 * its own memory, timed by the APB clock. Frames are pipelined: while one is
 * on air the next is already queued in the RMT driver, so the CPU adds no
 * dead time and the link can run at 100 % (bit rate is the only limit).
 * Producers block on the display queue when it is full, so a loop that sends
 * frames naturally runs at exactly the link rate with minimal latency.
 *
 * History: the previous design toggled LEDC duty from a 1 kHz GPTimer ISR.
 * Flash-cache stalls delayed that ISR and stretched bits (garbled text), the
 * last bit could be cut short, and calling the non-ISR-safe ledc_set_duty()
 * from the ISR could leave the ESP32 LEDC duty_start bit stuck, hanging
 * ledc_update_duty() until the interrupt watchdog reset the chip.
 */

static const char *TAG = "IR_TASK";

#define IR_RESOLUTION_HZ   1000000                 // 1 tick = 1 us
#define IR_BIT_US          1000                    // 1 ms per bit
#define IR_CARRIER_HZ      38540
#define IR_MAX_RUN_US      32000                   // < 32767-tick symbol limit
#define IR_LEVEL_CARRIER   1                       // RMT high = carrier on
#define IR_LEVEL_OFF       0

#define IR_PIPELINE        2                       // frames queued in the RMT driver
#define IR_STATS_WINDOW_US (2 * 1000 * 1000)
#define IR_LOG_EVERY       5                       // stats windows between log lines

/* Worst case: one half-symbol per bit (alternating bits) plus the gap split
 * into IR_MAX_RUN_US pieces. Each frame must fit in RMT RAM (4 x 64 symbols)
 * so playback never depends on a refill interrupt. */
#define IR_MEM_SYMBOLS     256
#define IR_MAX_HALVES      (MAX_MESSAGE_LEN * 8 + (IR_MAX_GAP_MS * 1000) / IR_MAX_RUN_US + 1)
#define IR_MAX_SYMBOLS     ((IR_MAX_HALVES + 1) / 2 + 1)
_Static_assert(IR_MAX_SYMBOLS < IR_MEM_SYMBOLS, "IR frame must fit in RMT memory");

static rmt_channel_handle_t s_tx_chan;
static rmt_encoder_handle_t s_copy_encoder;
static rmt_symbol_word_t s_symbols[IR_PIPELINE][IR_MAX_SYMBOLS];
static SemaphoreHandle_t s_free_slots;     // counts symbol buffers not on air

static volatile uint32_t s_gap_us = CONFIG_INFOGLOBE_IR_FRAME_GAP_MS * 1000;

static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static ir_stats_t s_stats;

/* ---------------------------------------------------------------- encoding */

typedef struct {
    rmt_symbol_word_t *sym;
    size_t halves;
} sym_writer_t;

static void emit(sym_writer_t *w, int level, uint32_t dur_us)
{
    while (dur_us > 0) {
        uint32_t d = dur_us > IR_MAX_RUN_US ? IR_MAX_RUN_US : dur_us;
        rmt_symbol_word_t *s = &w->sym[w->halves / 2];
        if ((w->halves & 1) == 0) {
            s->level0 = level; s->duration0 = d;
            s->level1 = IR_LEVEL_OFF; s->duration1 = 0;   // end marker if unpaired
        } else {
            s->level1 = level; s->duration1 = d;
        }
        w->halves++;
        dur_us -= d;
    }
}

/* Runs of equal bits become one level/duration pair, then the idle gap.
 * Returns the number of symbols; *air_us gets the frame's time on air. */
static size_t build_frame(rmt_symbol_word_t *sym, const uint8_t *data, size_t len,
                          uint32_t gap_us, uint32_t *air_us)
{
    sym_writer_t w = { .sym = sym, .halves = 0 };
    int run_level = -1;
    uint32_t run_us = 0;

    for (size_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--) {
            int level = (data[i] & (1u << b)) ? IR_LEVEL_OFF : IR_LEVEL_CARRIER;
            if (level != run_level) {
                if (run_level >= 0) {
                    emit(&w, run_level, run_us);
                }
                run_level = level;
                run_us = 0;
            }
            run_us += IR_BIT_US;
        }
    }
    /* The gap is "off", so it simply extends a trailing run of 1-bits. */
    if (run_level == IR_LEVEL_OFF) {
        run_us += gap_us;
    } else if (run_level >= 0) {
        emit(&w, run_level, run_us);
        run_level = IR_LEVEL_OFF;
        run_us = gap_us;
    }
    if (run_level >= 0 && run_us > 0) {
        emit(&w, run_level, run_us);
    }

    *air_us = len * 8 * IR_BIT_US + gap_us;
    return (w.halves + 1) / 2;
}

/* ------------------------------------------------------------------ driver */

static bool IRAM_ATTR on_frame_done(rmt_channel_handle_t chan,
                                    const rmt_tx_done_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_free_slots, &woken);
    return woken == pdTRUE;
}

static void ir_init(void)
{
    s_free_slots = xSemaphoreCreateCounting(IR_PIPELINE, IR_PIPELINE);
    configASSERT(s_free_slots);

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = LED_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = IR_MEM_SYMBOLS,
        .trans_queue_depth = IR_PIPELINE,
        .flags.init_level = IR_LEVEL_OFF,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &s_tx_chan));

    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = IR_CARRIER_HZ,
        .duty_cycle = 0.5f,
        .flags.polarity_active_low = false,   // carrier on the high level
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(s_tx_chan, &carrier_cfg));

    rmt_copy_encoder_config_t enc_cfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_cfg, &s_copy_encoder));

    rmt_tx_event_callbacks_t cbs = { .on_trans_done = on_frame_done };
    ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(s_tx_chan, &cbs, NULL));
    ESP_ERROR_CHECK(rmt_enable(s_tx_chan));
}

/* --------------------------------------------------------------- public API */

void ir_set_frame_gap_ms(uint32_t gap_ms)
{
    if (gap_ms > IR_MAX_GAP_MS) {
        gap_ms = IR_MAX_GAP_MS;
    }
    s_gap_us = gap_ms * 1000;
    ESP_LOGI(TAG, "Frame gap set to %u ms", (unsigned)gap_ms);
}

uint32_t ir_get_frame_gap_ms(void)
{
    return s_gap_us / 1000;
}

void ir_get_stats(ir_stats_t *out)
{
    taskENTER_CRITICAL(&s_stats_lock);
    *out = s_stats;
    taskEXIT_CRITICAL(&s_stats_lock);
    out->gap_ms = ir_get_frame_gap_ms();
}

/* -------------------------------------------------------------------- task */

void ir_tx_task(void *pvParameters)
{
    QueueHandle_t display_queue = (QueueHandle_t)(pvParameters);
    ir_init();

    int slot = 0;
    uint32_t win_frames = 0, total_frames = 0;
    uint64_t win_air_us = 0;
    int64_t win_start = esp_timer_get_time();
    int windows = 0;

    while (1) {
        msg_buffer_t msg;
        /* Wake at least once per stats window even when idle. */
        if (xQueueReceive(display_queue, &msg, pdMS_TO_TICKS(IR_STATS_WINDOW_US / 1000)) == pdPASS) {
            size_t len = msg.size;
            if (len > MAX_MESSAGE_LEN) {
                ESP_LOGW(TAG, "Frame of %u bytes truncated to %d", (unsigned)len, MAX_MESSAGE_LEN);
                len = MAX_MESSAGE_LEN;
            }
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, msg.buffer, len, ESP_LOG_DEBUG);

            /* Wait for a symbol buffer whose frame has finished playing. */
            xSemaphoreTake(s_free_slots, portMAX_DELAY);
            uint32_t air_us;
            size_t n = build_frame(s_symbols[slot], msg.buffer, len, s_gap_us, &air_us);
            free(msg.buffer);

            rmt_transmit_config_t tx = {
                .loop_count = 0,
                .flags.eot_level = IR_LEVEL_OFF,
            };
            esp_err_t err = rmt_transmit(s_tx_chan, s_copy_encoder, s_symbols[slot],
                                         n * sizeof(rmt_symbol_word_t), &tx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "rmt_transmit: %s", esp_err_to_name(err));
                xSemaphoreGive(s_free_slots);
            } else {
                slot = (slot + 1) % IR_PIPELINE;
                win_frames++;
                total_frames++;
                win_air_us += air_us;
            }
        }

        int64_t now = esp_timer_get_time();
        int64_t elapsed = now - win_start;
        if (elapsed >= IR_STATS_WINDOW_US) {
            float fps = win_frames * 1e6f / elapsed;
            float busy = win_air_us * 100.0f / elapsed;
            if (busy > 100.0f) {
                busy = 100.0f;   // queued airtime can run past the window edge
            }
            taskENTER_CRITICAL(&s_stats_lock);
            s_stats.fps = fps;
            s_stats.busy_pct = busy;
            s_stats.frames = total_frames;
            taskEXIT_CRITICAL(&s_stats_lock);
            if (win_frames && ++windows >= IR_LOG_EVERY) {
                windows = 0;
                ESP_LOGI(TAG, "%.1f frames/s, link %.0f%% busy, gap %u ms, %u frames total",
                         fps, busy, (unsigned)ir_get_frame_gap_ms(), (unsigned)total_frames);
            }
            win_frames = 0;
            win_air_us = 0;
            win_start = now;
        }
    }
}
