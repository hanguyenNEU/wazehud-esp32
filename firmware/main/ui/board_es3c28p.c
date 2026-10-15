/* LCDwiki ES3C28P / ES3N28P — 2.8" 240×320, ILI9341V qua SPI 4 dây.
 *
 * Sơ đồ chân lấy nguyên từ tài liệu chính hãng ("ESP32-S3 Demo Instructions",
 * bảng 2.1), đã đối chiếu chéo với sơ đồ mạch và với config.h của board anh em
 * ES3C35P trong dự án xiaozhi-esp32.
 */

#include "display.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display";

/* --- Bảng 2.1, tài liệu LCDwiki ------------------------------------------- */
#define PIN_TFT_CS   10
#define PIN_TFT_DC   46 /* tài liệu gọi là TFT_RS */
#define PIN_TFT_SCK  12
#define PIN_TFT_MOSI 11
#define PIN_TFT_BL   45

/* TFT_RST nối thẳng vào CHIP_PU — màn reset chung với chip, KHÔNG có GPIO
   riêng. Khai một chân bất kỳ ở đây là sai và sẽ kéo nhầm chân khác. */
#define PIN_TFT_RST  (-1)

#define LCD_HOST     SPI2_HOST
#define LCD_PCLK_HZ  (40 * 1000 * 1000)
#define CHUNK_ROWS   16

#define BL_TIMER    LEDC_TIMER_0
#define BL_CHANNEL  LEDC_CHANNEL_0
#define BL_DUTY_RES LEDC_TIMER_10_BIT

static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t panel_io;
static uint16_t *chunk;

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BL_TIMER,
        .duty_resolution = BL_DUTY_RES,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = PIN_TFT_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

void display_set_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t max_duty = (1u << BL_DUTY_RES) - 1;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL,
                                  max_duty * percent / 100));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL));
}

void display_init(void)
{
    backlight_init();

    const spi_bus_config_t bus = {
        .sclk_io_num = PIN_TFT_SCK,
        .mosi_io_num = PIN_TFT_MOSI,
        .miso_io_num = -1, /* chỉ ghi ra màn, không đọc lại */
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_W * CHUNK_ROWS * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_TFT_CS,
        .dc_gpio_num = PIN_TFT_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                             &io_config, &panel_io));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_TFT_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    /* Native theo BSP: 240×320, inverted, mirror_x=true. Landscape 90° theo
       esp_lvgl_port từ base đó là swap_xy=true + mirror cả hai trục. */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    chunk = heap_caps_malloc(DISPLAY_W * CHUNK_ROWS * sizeof(uint16_t),
                             MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_ERROR_CHECK(chunk ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "ILI9341V %dx%d sẵn sàng (SPI %d MHz)", DISPLAY_W, DISPLAY_H,
             LCD_PCLK_HZ / 1000000);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISPLAY_W) w = DISPLAY_W - x;
    if (y + h > DISPLAY_H) h = DISPLAY_H - y;
    if (w <= 0 || h <= 0) return;

    /* ILI9341 nhận RGB565 byte cao trước. LVGL tự swap qua esp_lvgl_port;
       đường self-test đi thẳng qua esp_lcd nên phải swap ở đây. */
    uint16_t bus_color = __builtin_bswap16(color);
    for (int i = 0; i < w * CHUNK_ROWS; ++i) chunk[i] = bus_color;

    for (int row = 0; row < h; row += CHUNK_ROWS) {
        int rows = (h - row < CHUNK_ROWS) ? h - row : CHUNK_ROWS;
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
            panel, x, y + row, x + w, y + row + rows, chunk));
    }
}

void display_fill(uint16_t color)
{
    display_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, color);
}

void display_selftest(void)
{
    static const uint16_t bars[] = {
        COLOR_RED, COLOR_GREEN, COLOR_BLUE,
        COLOR_YELLOW, COLOR_WHITE, COLOR_GREY,
    };
    const int count = sizeof(bars) / sizeof(bars[0]);
    const int bar_w = DISPLAY_W / count;

    for (int i = 0; i < count; ++i) {
        int w = (i == count - 1) ? DISPLAY_W - i * bar_w : bar_w;
        display_fill_rect(i * bar_w, 0, w, DISPLAY_H, bars[i]);
    }

    display_fill_rect(0, 0, DISPLAY_W, 2, COLOR_BLACK);
    display_fill_rect(0, DISPLAY_H - 2, DISPLAY_W, 2, COLOR_BLACK);
    display_fill_rect(0, 0, 2, DISPLAY_H, COLOR_BLACK);
    display_fill_rect(DISPLAY_W - 2, 0, 2, DISPLAY_H, COLOR_BLACK);

    const int m = 12;
    display_fill_rect(2, 2, m, m, COLOR_WHITE);
    display_fill_rect(DISPLAY_W - m - 2, 2, m, m, COLOR_WHITE);
    display_fill_rect(2, DISPLAY_H - m - 2, m, m, COLOR_WHITE);
    display_fill_rect(DISPLAY_W - m - 2, DISPLAY_H - m - 2, m, m, COLOR_WHITE);

    ESP_LOGI(TAG, "selftest: 6 dải màu, viền 2px, 4 dấu góc trắng");
}

esp_lcd_panel_io_handle_t display_io(void) { return panel_io; }
esp_lcd_panel_handle_t display_panel(void) { return panel; }
