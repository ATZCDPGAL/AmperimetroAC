#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "config.h"
#include "st7735.h"
#include "acs712.h"
#include "ui.h"

static const char *TAG = "AMPERIMETRO_AC";

void app_main(void) {
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "  AMPERIMETRO AC 0-30A (ESP32 + ACS712-30A + TFT) ");
    ESP_LOGI(TAG, "  Display: TFT 128*RGB*160 (ST7735/ST7735S)       ");
    ESP_LOGI(TAG, "  Sensor:  ACS712-30A (Efecto Hall, True RMS)     ");
    ESP_LOGI(TAG, "=================================================");

    /* 1. Inicializar periférico SPI y controlador ST7735 */
    esp_err_t ret = st7735_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error crítico al inicializar el display TFT ST7735.");
        return;
    }

    /* 2. Inicializar ADC y sistema de calibración para el sensor ACS712 */
    ret = acs712_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error crítico al inicializar el convertidor ADC.");
        return;
    }

    /* 3. Mostrar pantalla de calibración de tara (0A) */
    ui_show_calibrating();

    /* 4. Medir punto de reposo DC sin carga conectada */
    float zero_mv = 0.0f;
    acs712_calibrate_zero(ACS712_CALIB_SAMPLES, &zero_mv);
    ESP_LOGI(TAG, "Tara calibrada: Nivel DC de reposo = %.2f mV", zero_mv);

    // Pausa breve para visualizar la calibración
    vTaskDelay(pdMS_TO_TICKS(800));

    /* 5. Dibujar la interfaz estática base */
    ui_init();

    /* 6. Bucle de muestreo sincrónico AC y visualización en tiempo real */
    acs712_data_t data;
    uint32_t serial_log_counter = 0;

    while (1) {
        // Muestrear ventana de 100 ms (captura ciclos completos de 50Hz/60Hz)
        ret = acs712_read_ac(&data);
        if (ret == ESP_OK) {
            // Refrescar display TFT sin flicker
            ui_update(&data);

            // Emitir telemetría por puerto serie cada ~600 ms (4 ciclos de ~150 ms)
            if (++serial_log_counter >= 4) {
                serial_log_counter = 0;
                ESP_LOGI(TAG, "[AC RMS] %5.2f A | Pico: %4.1f A | Freq: %2.0f Hz | Vrms: %4.0f mV | Vzero: %4.0f mV%s",
                         data.current_rms_a,
                         data.current_peak_a,
                         data.frequency_hz,
                         data.voltage_rms_mv,
                         data.voltage_zero_mv,
                         data.overload ? " [ALERTA: SOBRECARGA >30A]" : "");
            }
        }

        // Breve respiro para tareas del sistema FreeRTOS
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}