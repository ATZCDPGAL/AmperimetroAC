#ifndef ST7735_H_
#define ST7735_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* =========================================================================
 * DEFINICIÓN DE COLORES RGB565 (16 BITS)
 * ========================================================================= */
#define ST7735_BLACK       0x0000
#define ST7735_WHITE       0xFFFF
#define ST7735_RED         0xF800
#define ST7735_GREEN       0x07E0
#define ST7735_BLUE        0x001F
#define ST7735_CYAN        0x07FF
#define ST7735_MAGENTA     0xF81F
#define ST7735_YELLOW      0xFFE0
#define ST7735_ORANGE      0xFD20
#define ST7735_DARKGREY    0x31A6
#define ST7735_LIGHTGREY   0xC618
#define ST7735_NAVY        0x000F
#define ST7735_DARKGREEN   0x03E0
#define ST7735_DARKCYAN    0x03EF
#define ST7735_MAROON      0x7800
#define ST7735_PURPLE      0x780F
#define ST7735_OLIVE       0x7BE0
#define ST7735_CARD_BG     0x1104
#define ST7735_HEADER_BG   0x08A5
#define ST7735_AMBER       0xFCE0

/* =========================================================================
 * PROTOTIPOS DE FUNCIONES DEL CONTROLADOR ST7735
 * ========================================================================= */
esp_err_t st7735_init(void);
void st7735_set_rotation(uint8_t m);
void st7735_fill_screen(uint16_t color);
void st7735_draw_pixel(int16_t x, int16_t y, uint16_t color);
void st7735_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void st7735_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void st7735_draw_fast_h_line(int16_t x, int16_t y, int16_t w, uint16_t color);
void st7735_draw_fast_v_line(int16_t x, int16_t y, int16_t h, uint16_t color);
void st7735_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
void st7735_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size);
void st7735_set_backlight(bool enable);

#endif /* ST7735_H_ */
