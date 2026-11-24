#!/bin/sh
# Copiază modulele aplicației din stm32/Core în varianta Template_GPIO (care trebuie să rămână
# un folder de sine stătător). main.c, stm32f4xx_it.c, stm32f4xx_hal_msp.c și main.h NU se copiază:
# aici sunt în formatul CubeMX, cu codul nostru în secțiunile USER CODE.
cd "$(dirname "$0")" || exit 1
for n in app display filter fonts_ui link protocol sensor ui_gfx; do cp ../stm32/Core/Src/$n.c Src/; done
for n in app app_config display filter link protocol sensor ui_gfx; do cp ../stm32/Core/Inc/$n.h Inc/; done
echo "modulele din stm32/Core copiate în stm32-template"
