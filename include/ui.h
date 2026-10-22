#ifndef UI_H_
#define UI_H_

#include "acs712.h"
#include <stdint.h>
#include <stdbool.h>

/* Inicializar pantalla con la interfaz gráfica estática del Amperímetro AC */
void ui_init(void);

/* Actualizar valores en pantalla (repinta solo los elementos dinámicos sin parpadeo) */
void ui_update(const acs712_data_t *data);

/* Pantalla de espera durante la calibración de tara (0A) */
void ui_show_calibrating(void);

#endif /* UI_H_ */
