#include "st7789_display.h"
#include "board_pins.h"

#include <stddef.h>
#include <string.h>

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "st7789";

#define LCD_SPI_HOST       SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (12 * 1000 * 1000)
#define LCD_GAP_X          52
#define LCD_GAP_Y          40

#define BL_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER     LEDC_TIMER_0
#define BL_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BL_LEDC_RES       LEDC_TIMER_13_BIT
#define BL_LEDC_FREQ_HZ   5000
#define BL_LEDC_MAX_DUTY  ((1u << 13) - 1)

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_framebuffer;

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = LCD_PIN_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel = BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void st7789_vendor_init(esp_lcd_panel_io_handle_t io)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xB0, (uint8_t[]){0x00, 0xE8}, 2));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xB2, (uint8_t[]){0x0C, 0x0C, 0x00, 0x33, 0x33}, 5));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xB7, (uint8_t[]){0x75}, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xBB, (uint8_t[]){0x1A}, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xC0, (uint8_t[]){0x80}, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xC2, (uint8_t[]){0x01, 0xFF}, 2));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xC3, (uint8_t[]){0x13}, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xC4, (uint8_t[]){0x20}, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xC6, (uint8_t[]){0x0F}, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xD0, (uint8_t[]){0xA4, 0xA1}, 2));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xE0, (uint8_t[]){
        0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38,
        0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30
    }, 14));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xE1, (uint8_t[]){
        0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37,
        0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x2E
    }, 14));
}

esp_err_t display_init(void)
{
    backlight_init();
    display_set_backlight(0);

    spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init failed");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                                 &io_config, &io),
                        TAG, "panel io init failed");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_config, &s_panel),
                        TAG, "panel create failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init failed");

    st7789_vendor_init(io);

    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "panel mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, LCD_GAP_X, LCD_GAP_Y), TAG, "panel gap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "panel invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "panel display on failed");

    s_framebuffer = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                                     MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (s_framebuffer == NULL) {
        ESP_LOGE(TAG, "framebuffer allocation failed");
        return ESP_ERR_NO_MEM;
    }

    display_fill(0x0000);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "ST7789 ready, %dx%d, SPI %d MHz", LCD_H_RES, LCD_V_RES,
             LCD_PIXEL_CLOCK_HZ / 1000000);
    return ESP_OK;
}

void display_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (BL_LEDC_MAX_DUTY * percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL));
}

void display_fill(uint16_t color)
{
    if (s_panel == NULL || s_framebuffer == NULL) {
        return;
    }
    size_t count = LCD_H_RES * LCD_V_RES;
    for (size_t i = 0; i < count; i++) {
        s_framebuffer[i] = color;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES,
                                              s_framebuffer));
}

void display_color_cycle(void)
{
    static const uint16_t colors[] = {
        0xF800,
        0x07E0,
        0x001F,
        0xFFFF,
        0x0000,
    };
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        display_fill(colors[i]);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

void display_test_pattern(void)
{
    if (s_panel == NULL || s_framebuffer == NULL) {
        return;
    }

    static const uint16_t bars[] = {
        0xFFFF,
        0xFFE0,
        0x07FF,
        0x07E0,
        0xF81F,
        0xF800,
        0x001F,
        0x0000,
    };

    for (int y = 0; y < LCD_V_RES; y++) {
        for (int x = 0; x < LCD_H_RES; x++) {
            size_t bar = (size_t)x * (sizeof(bars) / sizeof(bars[0])) / LCD_H_RES;
            s_framebuffer[y * LCD_H_RES + x] = bars[bar];
        }
    }

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES,
                                              s_framebuffer));
}

extern const uint8_t photo_rgb565_start[] asm("_binary_photo_rgb565_start");
extern const uint8_t photo_rgb565_end[] asm("_binary_photo_rgb565_end");

void display_show_photo(void)
{
    if (s_panel == NULL || s_framebuffer == NULL) {
        return;
    }

    size_t expected = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    size_t len = (size_t)(photo_rgb565_end - photo_rgb565_start);
    if (len != expected) {
        ESP_LOGE(TAG, "photo size %u, expected %u", (unsigned)len, (unsigned)expected);
        return;
    }

    memcpy(s_framebuffer, photo_rgb565_start, expected);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES,
                                              s_framebuffer));
}
