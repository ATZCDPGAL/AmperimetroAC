#include "st7735.h"
#include "config.h"
#include "fonts.h"
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "ST7735";

static spi_device_handle_t spi;
static uint8_t colstart = ST7735_X_OFFSET;
static uint8_t rowstart = ST7735_Y_OFFSET;
static uint16_t _width = TFT_WIDTH;
static uint16_t _height = TFT_HEIGHT;
static uint8_t _rotation = 0;

/* Buffer en DMA para transmisiones eficientes de líneas por SPI */
#define LINE_BUF_PIXELS 128
static uint16_t line_buf[LINE_BUF_PIXELS];

/* Enviar comando de 1 byte por SPI */
static void st7735_cmd(uint8_t cmd) {
    gpio_set_level(PIN_TFT_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(spi, &t);
}

/* Enviar datos por SPI */
static void st7735_data(const uint8_t *data, int len) {
    if (len <= 0) return;
    gpio_set_level(PIN_TFT_DC, 1);
    spi_transaction_t t = {
        .length = (size_t)len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(spi, &t);
}

/* Enviar 1 solo byte de dato */
static void st7735_data_byte(uint8_t b) {
    st7735_data(&b, 1);
}

/* Configurar ventana de memoria en el display */
static void st7735_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t data[4];
    uint16_t xa = x0 + colstart;
    uint16_t xb = x1 + colstart;
    uint16_t ya = y0 + rowstart;
    uint16_t yb = y1 + rowstart;

    st7735_cmd(0x2A); // CASET (Column Address Set)
    data[0] = (xa >> 8) & 0xFF;
    data[1] = xa & 0xFF;
    data[2] = (xb >> 8) & 0xFF;
    data[3] = xb & 0xFF;
    st7735_data(data, 4);

    st7735_cmd(0x2B); // RASET (Row Address Set)
    data[0] = (ya >> 8) & 0xFF;
    data[1] = ya & 0xFF;
    data[2] = (yb >> 8) & 0xFF;
    data[3] = yb & 0xFF;
    st7735_data(data, 4);

    st7735_cmd(0x2C); // RAMWR (RAM Write)
}

/* Control del pin de Backlight si está definido */
void st7735_set_backlight(bool enable) {
    if (PIN_TFT_BLK >= 0) {
        gpio_set_level(PIN_TFT_BLK, enable ? 1 : 0);
    }
}

/* Configuración de orientación del display (0: Vertical Estándar 128x160) */
void st7735_set_rotation(uint8_t m) {
    _rotation = m % 4;
    st7735_cmd(0x36); // MADCTL
    switch (_rotation) {
        case 0: // Vertical estándar (128 x 160)
            st7735_data_byte(0xC8); // MX=1, MY=1, BGR=1
            _width = TFT_WIDTH;
            _height = TFT_HEIGHT;
            colstart = ST7735_X_OFFSET;
            rowstart = ST7735_Y_OFFSET;
            break;
        case 1: // Apaisado 90° (160 x 128)
            st7735_data_byte(0xA8); // MY=1, MV=1, BGR=1
            _width = TFT_HEIGHT;
            _height = TFT_WIDTH;
            colstart = ST7735_Y_OFFSET;
            rowstart = ST7735_X_OFFSET;
            break;
        case 2: // Invertido 180°
            st7735_data_byte(0x08); // BGR=1
            _width = TFT_WIDTH;
            _height = TFT_HEIGHT;
            colstart = ST7735_X_OFFSET;
            rowstart = ST7735_Y_OFFSET;
            break;
        case 3: // Apaisado 270°
            st7735_data_byte(0x68); // MX=1, MV=1, BGR=1
            _width = TFT_HEIGHT;
            _height = TFT_WIDTH;
            colstart = ST7735_Y_OFFSET;
            rowstart = ST7735_X_OFFSET;
            break;
    }
}

/* Inicialización del periférico SPI y del controlador ST7735 */
esp_err_t st7735_init(void) {
    ESP_LOGI(TAG, "Configurando pines GPIO para display ST7735 (128x160)...");

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_TFT_DC) | (1ULL << PIN_TFT_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (PIN_TFT_BLK >= 0) {
        io_conf.pin_bit_mask |= (1ULL << PIN_TFT_BLK);
    }
    gpio_config(&io_conf);

    if (PIN_TFT_BLK >= 0) {
        st7735_set_backlight(true);
    }

    ESP_LOGI(TAG, "Inicializando bus SPI (Host: SPI2_HOST, Clock: 26MHz)...");
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_TFT_SDA,
        .miso_io_num = -1,
        .sclk_io_num = PIN_TFT_SCL,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LINE_BUF_PIXELS * 2,
    };
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando bus SPI: %d", ret);
        return ret;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 26 * 1000 * 1000, // 26 MHz
        .mode = 0,                          // SPI Mode 0
        .spics_io_num = PIN_TFT_CS,
        .queue_size = 7,
    };
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registrando dispositivo en bus SPI: %d", ret);
        return ret;
    }

    /* Secuencia de Reset por hardware */
    ESP_LOGI(TAG, "Ejecutando secuencia de hardware reset...");
    gpio_set_level(PIN_TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    /* Secuencia de comandos ST7735 / ST7735S */
    st7735_cmd(0x01); // Software Reset
    vTaskDelay(pdMS_TO_TICKS(150));

    st7735_cmd(0x11); // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(150));

    // FRMCTR1 (Frame Rate en modo normal)
    st7735_cmd(0xB1);
    uint8_t frm1[] = {0x01, 0x2C, 0x2D};
    st7735_data(frm1, sizeof(frm1));

    // FRMCTR2 (Frame Rate en modo Idle)
    st7735_cmd(0xB2);
    uint8_t frm2[] = {0x01, 0x2C, 0x2D};
    st7735_data(frm2, sizeof(frm2));

    // FRMCTR3 (Frame Rate en modo Partial)
    st7735_cmd(0xB3);
    uint8_t frm3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    st7735_data(frm3, sizeof(frm3));

    // INVCTR (Display Inversion Control)
    st7735_cmd(0xB4);
    st7735_data_byte(0x07);

    // PWCTR1 (Power Control 1)
    st7735_cmd(0xC0);
    uint8_t pw1[] = {0xA2, 0x02, 0x84};
    st7735_data(pw1, sizeof(pw1));

    // PWCTR2
    st7735_cmd(0xC1);
    st7735_data_byte(0xC5);

    // PWCTR3
    st7735_cmd(0xC2);
    uint8_t pw3[] = {0x0A, 0x00};
    st7735_data(pw3, sizeof(pw3));

    // PWCTR4
    st7735_cmd(0xC3);
    uint8_t pw4[] = {0x8A, 0x2A};
    st7735_data(pw4, sizeof(pw4));

    // PWCTR5
    st7735_cmd(0xC4);
    uint8_t pw5[] = {0x8A, 0xEE};
    st7735_data(pw5, sizeof(pw5));

    // VMCTR1 (VCOM)
    st7735_cmd(0xC5);
    st7735_data_byte(0x0E);

    // Inversión apagada
    st7735_cmd(0x20);

    // Formato de pixel: 16 bits (RGB565)
    st7735_cmd(0x3A);
    st7735_data_byte(0x05);

    // Orientación estándar (0)
    st7735_set_rotation(0);

    // Curva Gamma positiva
    st7735_cmd(0xE0);
    uint8_t gm_p[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                      0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
    st7735_data(gm_p, sizeof(gm_p));

    // Curva Gamma negativa
    st7735_cmd(0xE1);
    uint8_t gm_n[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                      0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};
    st7735_data(gm_n, sizeof(gm_n));

    // Modo normal activado
    st7735_cmd(0x13);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Encender pantalla
    st7735_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Display ST7735 128x160 inicializado correctamente.");
    return ESP_OK;
}

/* Dibujar un solo pixel */
void st7735_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= _width || y < 0 || y >= _height) return;
    st7735_set_addr_window(x, y, x, y);
    uint8_t data[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
    st7735_data(data, 2);
}

/* Rellenar un rectángulo con color */
void st7735_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (x >= _width || y >= _height || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;
    if (w <= 0 || h <= 0) return;

    st7735_set_addr_window(x, y, x + w - 1, y + h - 1);

    uint16_t color_swapped = ((color & 0xFF) << 8) | (color >> 8);
    int total_pixels = w * h;

    int chunk = (w < LINE_BUF_PIXELS) ? w : LINE_BUF_PIXELS;
    for (int i = 0; i < chunk; i++) {
        line_buf[i] = color_swapped;
    }

    gpio_set_level(PIN_TFT_DC, 1);
    while (total_pixels > 0) {
        int to_send = (total_pixels < chunk) ? total_pixels : chunk;
        spi_transaction_t t = {
            .length = (size_t)to_send * 16,
            .tx_buffer = line_buf,
        };
        spi_device_polling_transmit(spi, &t);
        total_pixels -= to_send;
    }
}

/* Limpiar pantalla completa */
void st7735_fill_screen(uint16_t color) {
    st7735_fill_rect(0, 0, _width, _height, color);
}

/* Línea horizontal rápida */
void st7735_draw_fast_h_line(int16_t x, int16_t y, int16_t w, uint16_t color) {
    st7735_fill_rect(x, y, w, 1, color);
}

/* Línea vertical rápida */
void st7735_draw_fast_v_line(int16_t x, int16_t y, int16_t h, uint16_t color) {
    st7735_fill_rect(x, y, 1, h, color);
}

/* Dibujar contorno de rectángulo */
void st7735_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    st7735_draw_fast_h_line(x, y, w, color);
    st7735_draw_fast_h_line(x, y + h - 1, w, color);
    st7735_draw_fast_v_line(x, y, h, color);
    st7735_draw_fast_v_line(x + w - 1, y, h, color);
}

/* Dibujar carácter con fondo transparente o sólido (sin parpadeo) */
void st7735_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t *char_glyph = font5x7[c - 32];

    for (int8_t i = 0; i < 6; i++) { // 5 columnas + 1 espacio inter-carácter
        uint8_t line = (i < 5) ? char_glyph[i] : 0x00;
        for (int8_t j = 0; j < 8; j++) {
            bool pixel_on = (line & 0x01);
            uint16_t pixel_color = pixel_on ? color : bg;

            if (pixel_on || bg != color) {
                if (size == 1) {
                    st7735_draw_pixel(x + i, y + j, pixel_color);
                } else {
                    st7735_fill_rect(x + i * size, y + j * size, size, size, pixel_color);
                }
            }
            line >>= 1;
        }
    }
}

/* Dibujar cadena de texto */
void st7735_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size) {
    int16_t cur_x = x;
    while (*str) {
        if (*str == '\n') {
            y += 8 * size;
            cur_x = x;
        } else {
            st7735_draw_char(cur_x, y, *str, color, bg, size);
            cur_x += 6 * size;
        }
        str++;
    }
}
