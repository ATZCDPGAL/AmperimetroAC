#include "ui.h"
#include "st7735.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define CARD_X       4
#define CARD_Y       24
#define CARD_W       120
#define CARD_H       48

#define BAR_X        12
#define BAR_Y        124
#define BAR_MAX_W    104
#define BAR_H        12

static float s_last_current = -999.0f;
static float s_last_peak = -999.0f;
static float s_last_vrms = -999.0f;
static float s_last_freq = -999.0f;
static int   s_last_bar_w = 0;
static bool  s_last_overload = false;

/* Pantalla de bienvenida mientras se calibra la tara en reposo (128x160) */
void ui_show_calibrating(void) {
    st7735_fill_screen(ST7735_BLACK);

    // 1. Encabezado con estilo de instrumento
    st7735_fill_rect(0, 0, TFT_WIDTH, 20, ST7735_HEADER_BG);
    st7735_draw_string(8, 6, "AC AMMETER", ST7735_CYAN, ST7735_HEADER_BG, 1);
    st7735_draw_string(86, 6, "30A", ST7735_YELLOW, ST7735_HEADER_BG, 1);
    st7735_draw_fast_h_line(0, 20, TFT_WIDTH, ST7735_CYAN);

    // 2. Mensaje central de calibración
    st7735_draw_string(14, 45, "CALIBRANDO TARA", ST7735_YELLOW, ST7735_BLACK, 1);
    st7735_draw_string(14, 65, "Sin carga conectada", ST7735_LIGHTGREY, ST7735_BLACK, 1);
    st7735_draw_string(18, 80, "(0.00 A - Reposo)", ST7735_LIGHTGREY, ST7735_BLACK, 1);
    st7735_draw_string(14, 102, "Por favor espere...", ST7735_DARKGREY, ST7735_BLACK, 1);

    // 3. Barra animada de progreso
    st7735_draw_rect(12, 126, 104, 12, ST7735_DARKCYAN);
    st7735_fill_rect(14, 128, 100, 8, ST7735_NAVY);
}

/* Dibujar la interfaz estática base para resolución 128x160 */
void ui_init(void) {
    st7735_fill_screen(ST7735_BLACK);

    // 1. Barra superior de título
    st7735_fill_rect(0, 0, TFT_WIDTH, 20, ST7735_HEADER_BG);
    st7735_draw_string(8, 6, "AC AMMETER", ST7735_CYAN, ST7735_HEADER_BG, 1);
    st7735_draw_string(86, 6, "0-30A", ST7735_YELLOW, ST7735_HEADER_BG, 1);
    st7735_draw_fast_h_line(0, 20, TFT_WIDTH, ST7735_DARKCYAN);

    // 2. Tarjeta principal del valor RMS (48 px de alto)
    st7735_fill_rect(CARD_X, CARD_Y, CARD_W, CARD_H, ST7735_CARD_BG);
    st7735_draw_rect(CARD_X, CARD_Y, CARD_W, CARD_H, ST7735_DARKGREY);

    // Unidad 'A' fija en tamaño 2 y sub-etiqueta 'RMS' en cyan
    st7735_draw_string(100, 30, "A", ST7735_YELLOW, ST7735_CARD_BG, 2);
    st7735_draw_string(98, 52, "RMS", ST7735_CYAN, ST7735_CARD_BG, 1);

    // 3. Marco de la barra analógica (0 a 30A)
    st7735_draw_rect(BAR_X - 2, BAR_Y - 2, BAR_MAX_W + 4, BAR_H + 4, ST7735_LIGHTGREY);

    // Marcas de escala (Ticks a 0A, 10A, 20A y 30A)
    st7735_draw_fast_v_line(BAR_X, BAR_Y + BAR_H + 3, 3, ST7735_LIGHTGREY);
    st7735_draw_fast_v_line(BAR_X + 35, BAR_Y + BAR_H + 3, 3, ST7735_LIGHTGREY);
    st7735_draw_fast_v_line(BAR_X + 69, BAR_Y + BAR_H + 3, 3, ST7735_LIGHTGREY);
    st7735_draw_fast_v_line(BAR_X + BAR_MAX_W - 1, BAR_Y + BAR_H + 3, 3, ST7735_LIGHTGREY);

    // Leyendas de escala
    st7735_draw_string(10, 144, "0", ST7735_DARKGREY, ST7735_BLACK, 1);
    st7735_draw_string(42, 144, "10", ST7735_DARKGREY, ST7735_BLACK, 1);
    st7735_draw_string(76, 144, "20", ST7735_DARKGREY, ST7735_BLACK, 1);
    st7735_draw_string(102, 144, "30A", ST7735_YELLOW, ST7735_BLACK, 1);

    // Resetear valores de tracking para forzar refresco inicial
    s_last_current = -999.0f;
    s_last_peak = -999.0f;
    s_last_vrms = -999.0f;
    s_last_freq = -999.0f;
    s_last_bar_w = 0;
    s_last_overload = false;
}

/* Actualización dinámica y libre de parpadeo (Flicker-Free) */
void ui_update(const acs712_data_t *data) {
    if (!data) return;

    // 1. Actualizar el valor numérico grande de Corriente True RMS si varía
    if (fabsf(data->current_rms_a - s_last_current) >= 0.01f || data->overload != s_last_overload) {
        s_last_current = data->current_rms_a;
        s_last_overload = data->overload;

        char buf_cur[16];
        snprintf(buf_cur, sizeof(buf_cur), "%5.2f", data->current_rms_a);

        // Color dinámico según la magnitud de corriente
        uint16_t num_color = ST7735_GREEN;
        if (data->current_rms_a >= 25.0f || data->overload) {
            num_color = ST7735_RED;
        } else if (data->current_rms_a >= 18.0f) {
            num_color = ST7735_AMBER;
        }

        // Dígitos en tamaño 3 (18x24 px por carácter) con fondo sólido sin parpadeo
        st7735_draw_string(8, 36, buf_cur, num_color, ST7735_CARD_BG, 3);
    }

    // 2. Información de telemetría secundaria (Pico, Frecuencia, Voltajes del sensor)
    bool need_telemetry_refresh = (fabsf(data->current_peak_a - s_last_peak) >= 0.1f) ||
                                  (fabsf(data->voltage_rms_mv - s_last_vrms) >= 3.0f) ||
                                  (fabsf(data->frequency_hz - s_last_freq) >= 1.0f);

    if (need_telemetry_refresh) {
        s_last_peak = data->current_peak_a;
        s_last_vrms = data->voltage_rms_mv;
        s_last_freq = data->frequency_hz;

        char buf_line1[24];
        if (data->overload) {
            snprintf(buf_line1, sizeof(buf_line1), "! SOBRECARGA >30A !");
            st7735_draw_string(6, 78, buf_line1, ST7735_RED, ST7735_BLACK, 1);
        } else {
            if (data->frequency_hz > 10.0f) {
                snprintf(buf_line1, sizeof(buf_line1), "Ipk:%4.1fA  F:%2.0fHz  ", data->current_peak_a, data->frequency_hz);
            } else {
                snprintf(buf_line1, sizeof(buf_line1), "Ipk:%4.1fA  F:--Hz  ", data->current_peak_a);
            }
            st7735_draw_string(6, 78, buf_line1, ST7735_LIGHTGREY, ST7735_BLACK, 1);
        }

        char buf_vrms[24];
        snprintf(buf_vrms, sizeof(buf_vrms), "Vrms: %4.0f mV     ", data->voltage_rms_mv);
        st7735_draw_string(6, 91, buf_vrms, ST7735_DARKCYAN, ST7735_BLACK, 1);

        char buf_vzero[24];
        snprintf(buf_vzero, sizeof(buf_vzero), "Vzero:%4.0f mV     ", data->voltage_zero_mv);
        st7735_draw_string(6, 104, buf_vzero, ST7735_DARKGREY, ST7735_BLACK, 1);
    }

    // 3. Barra analógica graduada (0.0 a 30.0 A)
    int target_w = (int)((data->current_rms_a / CURRENT_MAX_A) * (float)BAR_MAX_W);
    if (target_w > BAR_MAX_W) target_w = BAR_MAX_W;
    if (target_w < 0) target_w = 0;

    if (target_w != s_last_bar_w) {
        // Color dinámico de la barra
        uint16_t bar_color = ST7735_GREEN;
        if (data->current_rms_a >= 25.0f || data->overload) {
            bar_color = ST7735_RED;
        } else if (data->current_rms_a >= 18.0f) {
            bar_color = ST7735_AMBER;
        }

        if (target_w > s_last_bar_w) {
            // Rellenar nuevo segmento
            st7735_fill_rect(BAR_X + s_last_bar_w, BAR_Y, target_w - s_last_bar_w, BAR_H, bar_color);
            // Si el color cambió, repintar toda la barra
            if (bar_color != ST7735_GREEN) {
                st7735_fill_rect(BAR_X, BAR_Y, s_last_bar_w, BAR_H, bar_color);
            }
        } else {
            // Borrar segmento excedente
            st7735_fill_rect(BAR_X + target_w, BAR_Y, s_last_bar_w - target_w, BAR_H, ST7735_BLACK);
            // Repintar segmento restante con el color correspondiente
            st7735_fill_rect(BAR_X, BAR_Y, target_w, BAR_H, bar_color);
        }
        s_last_bar_w = target_w;
    }
}
