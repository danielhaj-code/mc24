/*
 * display.h — ecranul LCD al plăcii (240 x 320), desenat cu funcțiile BSP_LCD_*.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include "app.h"

void display_init(void);                         /* pornește LCD-ul și desenează partea fixă */
void display_show(const app_state_t *st);        /* redesenează valorile (nivel, mL, stare) */

#endif /* DISPLAY_H */
