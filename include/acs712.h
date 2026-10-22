#ifndef ACS712_H_
#define ACS712_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Estructura con datos de medición del sensor ACS712 en corriente alterna (AC) */
typedef struct {
    float current_rms_a;    // Corriente eficaz True RMS en Amperios (0.00 a 30.00 A)
    float current_peak_a;   // Corriente de pico instantánea en Amperios (0.00 a 42.4 A)
    float voltage_rms_mv;   // Voltaje AC eficaz en la salida del sensor (mV RMS)
    float voltage_zero_mv;  // Voltaje continuo de referencia de cero / tara (mV DC)
    float frequency_hz;     // Frecuencia estimada de la red AC (50 / 60 Hz)
    bool overload;          // Indicador de sobrecarga (> 30.0 Amperios)
    bool is_calibrated;     // Indica si la calibración de tara inicial fue completada
} acs712_data_t;

/* Inicializar el ADC del ESP32 y el sistema de calibración eFuse */
esp_err_t acs712_init(void);

/* Calibrar el cero (Tara) midiendo el voltaje sin carga */
esp_err_t acs712_calibrate_zero(uint16_t samples, float *out_zero_mv);

/* Realizar muestreo de onda AC completa y cálculo True RMS */
esp_err_t acs712_read_ac(acs712_data_t *data);

/* Modificar manualmente el voltaje de referencia de cero */
void acs712_set_zero_offset(float zero_mv);

/* Obtener el offset de cero actual */
float acs712_get_zero_offset(void);

#endif /* ACS712_H_ */
