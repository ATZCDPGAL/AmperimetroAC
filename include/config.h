#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

/* =========================================================================
 * 1. PINES DE CONEXIÓN DISPLAY TFT 128*RGB*160 VER 1 (ST7735 / ST7735S)
 * ========================================================================= */
#define PIN_TFT_SCL         GPIO_NUM_18   // SPI Clock (SCL / SCK / CLK)
#define PIN_TFT_SDA         GPIO_NUM_23   // SPI MOSI / Data (SDA / MOSI / SDI)
#define PIN_TFT_CS          GPIO_NUM_5    // Chip Select (CS / SS)
#define PIN_TFT_DC          GPIO_NUM_21   // Data/Command (DC / A0 / RS)
#define PIN_TFT_RST         GPIO_NUM_22   // Reset (RES / RST)
#define PIN_TFT_BLK         GPIO_NUM_15   // Backlight control (BLK / LED) - Opcional (-1 si se conecta a 3.3V)

/* Dimensiones del Display (128 x 160 píxeles) */
#define TFT_WIDTH           128
#define TFT_HEIGHT          160

/* Offsets para controlador ST7735 128x160 (módulos 1.8" TFT 128*RGB*160 v1) */
/* Los displays 1.8" 128x160 habitualmente no requieren offset (X=0, Y=0).
 * Si tu panel presentase 1 o 2 píxeles desplazados en el borde, ajusta a X=2, Y=1. */
#define ST7735_X_OFFSET     0
#define ST7735_Y_OFFSET     0

/* =========================================================================
 * 2. CONFIGURACIÓN SENSOR DE EFECTO HALL ACS712 (30A) - MODO CORRIENTE ALTERNA (AC)
 * ========================================================================= */
/* Pin analógico de entrada (GPIO 34 = ADC1_CH6, entrada segura de bajo ruido) */
#define ACS712_ADC_UNIT     ADC_UNIT_1
#define ACS712_ADC_CHANNEL  ADC_CHANNEL_6 // GPIO 34

/* Sensibilidad para modelo ACS712-30A: 66 mV por Amperio */
#define ACS712_SENSITIVITY_MV_PER_A   66.0f

/* Voltaje teórico en reposo a 0A (Vcc / 2 = 2500 mV cuando se alimenta con 5.0V) */
#define ACS712_NOMINAL_ZERO_MV        2500.0f

/* Rango de medición para este amperímetro AC */
#define CURRENT_MIN_A                 0.0f
#define CURRENT_MAX_A                 30.0f

/* =========================================================================
 * 3. CONFIGURACIÓN DEL DIVISOR DE VOLTAJE (PROTECCIÓN DEL ESP32)
 * =========================================================================
 * IMPORTANTE:
 * El sensor ACS712 entrega 2.5V a 0A y hasta 4.5V a corrientes máximas positivas.
 * Los pines del ESP32 toleran hasta 3.3V (rango lineal del ADC con 12dB es ~0 a 3.1V).
 * - Si conectas directo: Solo mide con seguridad hasta corrientes moderadas (< ~9-10A RMS).
 * - Si colocas un divisor de voltaje (por ej. R1=10k hacia el sensor, R2=20k hacia GND):
 *   El factor de reducción es 20k / (10k + 20k) = 0.6667 (2/3).
 *   5.0V se convierten en 3.33V, y 2.5V se convierten en 1.66V, permitiendo medir hasta 30A AC.
 *
 * Configuración:
 * 0 = Conexión directa (ACS712 OUT directo a GPIO34)
 * 1 = Con divisor resistivo
 */
#define ACS712_USE_VOLTAGE_DIVIDER    0

#if ACS712_USE_VOLTAGE_DIVIDER
/* Factor de atenuación = V_esp32 / V_sensor (ejemplo con R1=10k, R2=20k -> factor = 0.6667) */
#define ACS712_VOLTAGE_DIVIDER_RATIO  0.6667f
#else
#define ACS712_VOLTAGE_DIVIDER_RATIO  1.0000f
#endif

/* =========================================================================
 * 4. PARÁMETROS DE MUESTREO AC (TRUE RMS & FRECUENCIA)
 * ========================================================================= */
/* Frecuencia de la red eléctrica comercial (60 Hz en México/América, 50 Hz en Europa) */
#define AC_NOMINAL_FREQ_HZ            60

/* Ventana de muestreo en microsegundos:
 * 100 ms (100,000 us) contiene exactamente:
 * - 6 ciclos completos de 60 Hz (6 * 16.666 ms = 100 ms)
 * - 5 ciclos completos de 50 Hz (5 * 20.000 ms = 100 ms)
 * Esto garantiza que el cálculo True RMS no sufra distorsión por ciclo incompleto. */
#define AC_SAMPLE_WINDOW_US           100000

/* Número de muestras tomadas durante la ventana de 100 ms */
/* 500 muestras en 100 ms = 200 us por muestra (tasa de muestreo de 5.0 kHz) */
#define AC_SAMPLE_COUNT               500

/* Muestras para la calibración de cero inicial (tara en reposo sin carga) */
#define ACS712_CALIB_SAMPLES          1000

/* Umbral de zona muerta (Deadband) en Amperios RMS:
 * El sensor ACS712 presenta un ruido térmico natural de ~20-30 mV RMS (~0.15 - 0.25A).
 * Toda medición RMS menor a este umbral se considera 0.00A para evitar dígitos parásitos. */
#define AC_DEADBAND_RMS_A             0.20f

#endif /* CONFIG_H_ */
