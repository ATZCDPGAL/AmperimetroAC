#include "acs712.h"
#include "config.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ACS712_AC";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
static bool s_has_cali = false;
static float s_zero_offset_mv = ACS712_NOMINAL_ZERO_MV;
static float s_filtered_rms = 0.0f;
static bool s_is_calibrated = false;

/* Buffer estático de muestras para evitar asignación dinámica en cada ciclo */
static float s_sample_buffer[AC_SAMPLE_COUNT];

/* Inicializar ADC1 y esquema de calibración de fábrica (eFuse) */
esp_err_t acs712_init(void) {
    ESP_LOGI(TAG, "Configurando ADC1 para sensor ACS712-30A (Pin GPIO %d)...", ACS712_ADC_CHANNEL);

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ACS712_ADC_UNIT,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo inicializando unidad ADC1: %d", ret);
        return ret;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // Rango máximo (~0 a 3100 mV)
    };
    ret = adc_oneshot_config_channel(adc_handle, ACS712_ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo configurando canal ADC: %d", ret);
        return ret;
    }

    /* Calibración eFuse */
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ACS712_ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle);
    if (ret == ESP_OK) {
        s_has_cali = true;
        ESP_LOGI(TAG, "Calibración eFuse (Line Fitting) habilitada.");
    } else {
        ESP_LOGW(TAG, "Calibración eFuse no disponible, usando modelo analítico.");
    }
#elif ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ACS712_ADC_UNIT,
        .chan = ACS712_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
    if (ret == ESP_OK) {
        s_has_cali = true;
        ESP_LOGI(TAG, "Calibración eFuse (Curve Fitting) habilitada.");
    }
#endif

    return ESP_OK;
}

/* Leer un único valor de voltaje en milivoltios compensando el divisor si aplica */
static inline float read_single_voltage_mv(void) {
    int raw_val = 0;
    int voltage_val = 0;
    adc_oneshot_read(adc_handle, ACS712_ADC_CHANNEL, &raw_val);
    if (s_has_cali) {
        adc_cali_raw_to_voltage(cali_handle, raw_val, &voltage_val);
    } else {
        voltage_val = (raw_val * 3100) / 4095;
    }

    // Convertir de lectura en pin ESP32 a la salida real del sensor ACS712
    return ((float)voltage_val) / ACS712_VOLTAGE_DIVIDER_RATIO;
}

/* Calibrar el cero (Tara) cuando no hay corriente circulando */
esp_err_t acs712_calibrate_zero(uint16_t samples, float *out_zero_mv) {
    if (samples == 0) samples = 1;
    ESP_LOGI(TAG, "Iniciando calibración de tara (0A) con %d muestras...", samples);

    double sum = 0.0;
    for (uint16_t i = 0; i < samples; i++) {
        sum += (double)read_single_voltage_mv();
        vTaskDelay(pdMS_TO_TICKS(1)); // 1 ms entre muestras para abarcar varios ciclos de 50/60Hz
    }

    s_zero_offset_mv = (float)(sum / (double)samples);
    s_is_calibrated = true;
    s_filtered_rms = 0.0f;

    if (out_zero_mv) {
        *out_zero_mv = s_zero_offset_mv;
    }

    ESP_LOGI(TAG, "Calibración de tara finalizada. Punto cero Vout = %.2f mV", s_zero_offset_mv);
    return ESP_OK;
}

/* Muestreo sincrónico de onda completa y cálculo de True RMS, Pico y Frecuencia */
esp_err_t acs712_read_ac(acs712_data_t *data) {
    if (!data) return ESP_ERR_INVALID_ARG;

    const int64_t interval_us = AC_SAMPLE_WINDOW_US / AC_SAMPLE_COUNT; // ~200 us por muestra
    int64_t target_time = esp_timer_get_time();

    double sum_voltage = 0.0;

    /* 1. Fase de Adquisición determinística en el dominio del tiempo */
    for (int i = 0; i < AC_SAMPLE_COUNT; i++) {
        // Sincronización temporal de alta precisión
        while (esp_timer_get_time() < target_time) {
            // Espera activa precisa
        }
        target_time += interval_us;

        float v_mv = read_single_voltage_mv();
        s_sample_buffer[i] = v_mv;
        sum_voltage += (double)v_mv;
    }

    /* 2. Cálculo del nivel DC medio de la ventana
     * El ACS712 genera la señal AC montada sobre un nivel DC (Vcc / 2).
     * Calcular la media exacta de la ventana elimina el componente DC y cualquier deriva térmica. */
    float window_dc_mean = (float)(sum_voltage / (double)AC_SAMPLE_COUNT);

    /* 3. Cálculo de True RMS, detección de picos y estimación de frecuencia */
    double sum_squared_diff = 0.0;
    float max_peak_diff = 0.0f;
    int zero_crossings = 0;
    bool prev_state_above = (s_sample_buffer[0] >= window_dc_mean);

    // Histéresis para detección de cruces por cero inmune a ruido (15 mV)
    const float hysteresis_mv = 15.0f;

    for (int i = 0; i < AC_SAMPLE_COUNT; i++) {
        float ac_component = s_sample_buffer[i] - window_dc_mean;
        sum_squared_diff += (double)(ac_component * ac_component);

        float abs_ac = fabsf(ac_component);
        if (abs_ac > max_peak_diff) {
            max_peak_diff = abs_ac;
        }

        // Detector de cruces por cero con histéresis
        if (!prev_state_above && (ac_component > hysteresis_mv)) {
            zero_crossings++;
            prev_state_above = true;
        } else if (prev_state_above && (ac_component < -hysteresis_mv)) {
            prev_state_above = false;
        }
    }

    // Voltaje AC eficaz (True RMS) en milivoltios
    float v_rms_mv = (float)sqrt(sum_squared_diff / (double)AC_SAMPLE_COUNT);

    // Corriente True RMS = V_rms / Sensibilidad (66 mV/A)
    float raw_rms_a = v_rms_mv / ACS712_SENSITIVITY_MV_PER_A;

    // Corriente de pico instantánea = V_peak / Sensibilidad
    float peak_a = max_peak_diff / ACS712_SENSITIVITY_MV_PER_A;

    // Estimación de frecuencia en Hz
    // Número de ciclos = cruces por cero positivos en 100 ms (0.1 segundos)
    float freq_hz = 0.0f;
    if (raw_rms_a >= AC_DEADBAND_RMS_A) {
        freq_hz = ((float)zero_crossings) / 0.100f; // Multiplicado por 10 (f = ciclos / 0.1s)
        // Redondear a frecuencias comerciales estándar si están en rango ±4Hz
        if (freq_hz >= 56.0f && freq_hz <= 64.0f) {
            freq_hz = 60.0f;
        } else if (freq_hz >= 46.0f && freq_hz <= 54.0f) {
            freq_hz = 50.0f;
        }
    }

    /* 4. Filtro de zona muerta (Deadband) contra ruido térmico residual del Hall */
    if (raw_rms_a < AC_DEADBAND_RMS_A) {
        raw_rms_a = 0.0f;
        peak_a = 0.0f;
        v_rms_mv = 0.0f;
        freq_hz = 0.0f;
    }

    /* 5. Filtro suavizador IIR exponencial para estabilidad en visualización */
    if (raw_rms_a == 0.0f) {
        // Decaimiento rápido a cero cuando se desconecta la carga
        s_filtered_rms = (s_filtered_rms * 0.40f);
        if (s_filtered_rms < 0.04f) s_filtered_rms = 0.0f;
    } else {
        // Suavizado estándar (70% anterior, 30% nuevo)
        s_filtered_rms = (s_filtered_rms * 0.70f) + (raw_rms_a * 0.30f);
    }

    /* 6. Asignar resultados a la estructura de telemetría */
    data->voltage_zero_mv = s_zero_offset_mv;
    data->voltage_rms_mv = v_rms_mv;
    data->frequency_hz = freq_hz;
    data->current_peak_a = peak_a;
    data->is_calibrated = s_is_calibrated;

    // Verificar sobrecarga (> 30 Amperios)
    data->overload = (s_filtered_rms > CURRENT_MAX_A);
    if (s_filtered_rms > CURRENT_MAX_A) {
        data->current_rms_a = CURRENT_MAX_A;
    } else {
        data->current_rms_a = s_filtered_rms;
    }

    return ESP_OK;
}

void acs712_set_zero_offset(float zero_mv) {
    s_zero_offset_mv = zero_mv;
    s_is_calibrated = true;
}

float acs712_get_zero_offset(void) {
    return s_zero_offset_mv;
}
