# Smart Bottle pe șablonul de laborator Template_GPIO

Același firmware ca în `../stm32/`, adus în structura șablonului de laborator *Template_MC2025 / Template_GPIO*
(proiect CubeMX + Keil µVision pentru STM32F429I-DISC1). Folderul e de sine stătător: conține driverele
și se poate copia sau arhiva separat de restul repo-ului.

## Deschidere și scriere pe placă

1. Keil MDK cu **Arm Compiler 6.22** și pack-ul `Keil::STM32F4xx_DFP` (proiectul e salvat cu 2.17.1, ca șablonul).
   Licența trebuie să permită imagini peste 32 KB (MDK-Community sau Pro; imaginea are ~66 KB).
2. Deschide `MDK-ARM/Template_GPIO.uvprojx`.
3. **F7** compilează (`MDK-ARM/Template_GPIO/Template_GPIO.axf` și `.hex`), **F8** scrie pe placă prin
   ST-LINK-ul de pe placă și pornește programul.
4. Jurnalul: terminal serial pe portul COM „STMicroelectronics STLink Virtual COM Port”, **115200** 8N1
   (șablonul folosea 9600). La pornire apare `[boot] Smart Bottle dev (Template_GPIO), 180 MHz, ...`.

Variantele se aleg din *Options for Target → C/C++ → Define*, adăugând la listă:

| Definiție | Efect |
|---|---|
| `LINK_VCP=1` | protocolul trece pe portul serial USB (test fără ESP32, cu `stm32/tools/vcp_test.py`) |
| `SENSOR_USE_HX711=1` | celula de sarcină cu HX711 în loc de senzorul analogic |

## Ce s-a schimbat față de șablon

| Zonă | Șablon | Aici |
|---|---|---|
| Compilator | Arm Compiler 5.06 | Arm Compiler 6.22, C99, `-Oz`, MicroLIB |
| Ceas | HSI, 25 MHz | HSE 8 MHz → PLL 180 MHz (Scale 1 + over-drive, 5 stări de așteptare Flash) |
| Periferice (.ioc) | GPIO (LD3, LD4, USER), USART1 9600 | + ADC1 IN5 pe PA5, UART5 115200 pe PC12/PD2 (spre ESP32), USART1 115200, PE2/PE3/PE4 pentru senzor |
| NVIC | toate pe prioritatea 0 | UART5 și USART1 pe 5, SysTick pe 15 |
| Stiva | 0x400 | 0x1000 (în `.ioc` și în `MDK-ARM/startup_stm32f429xx.s`) |
| `main.c` | buclă care clipește LED-urile, `printf` pe USART1 | inițializările de mai sus, apoi `app_init()` / `app_loop()` în secțiunile `USER CODE` |
| `stm32f4xx_it.c` | ecou al caracterelor primite pe USART1 | `HAL_UART_IRQHandler` pentru USART1 și UART5 (recepția o tratează `link.c`); erorile grave aprind LD4 |
| Drivere | HAL minimal (14 module) + CMSIS complet (DSP, NN, RTOS: ~90 MB) | HAL 1.8.5 (aceeași versiune) cu ADC, LTDC, DMA2D, SDRAM/FMC, SPI, I2C, BSP-ul plăcii, fonturile; CMSIS doar `Include` |

Modulele LTDC, DMA2D, SDRAM, SPI și I2C le folosește doar BSP-ul LCD-ului și al touch-ului, care le inițializează
singur. De aceea nu sunt în `.ioc`, ci activate din definițiile proiectului Keil (`HAL_LTDC_MODULE_ENABLED` etc.),
ca să nu le dezactiveze o regenerare a lui `stm32f4xx_hal_conf.h`.

## Structura

| Cale | Conținut |
|---|---|
| `Template_GPIO.ioc` | Configurația CubeMX (pini, ceasuri, periferice) |
| `Src/main.c`, `Src/stm32f4xx_it.c`, `Src/stm32f4xx_hal_msp.c`, `Inc/main.h` | Fișierele în format CubeMX, cu codul aplicației în `USER CODE` |
| `Src/app.c` … `Src/ui_gfx.c`, `Inc/app.h` … `Inc/ui_gfx.h` | Modulele aplicației, **copii** ale celor din `../stm32/Core` |
| `Drivers/` | CMSIS, HAL, BSP STM32F429I-Discovery cu componentele ili9341 și stmpe811 |
| `Utilities/Fonts/` | Fonturile cerute de BSP-ul LCD-ului |
| `MDK-ARM/` | Proiectul Keil, startup-ul în sintaxă armasm, configurația de depanare |
| `sync-core.sh` | Recopiază modulele din `../stm32/Core` |

Modulele aplicației se modifică în `../stm32/Core` și se copiază aici cu `sh sync-core.sh`;
`tests/test_template_sync.mjs` pică dacă cele două copii diferă. Detaliile fiecărui modul sunt în
[`../docs/firmware-stm32.md`](../docs/firmware-stm32.md).

## Regenerarea din CubeMX

`.ioc`-ul descrie aceeași configurație ca `main.c`, iar codul nostru stă doar în `USER CODE`, deci o
regenerare (*GENERATE CODE*) ar trebui să-l păstreze. `.ioc`-ul a fost editat de mână, fără CubeMX:
la prima deschidere verifică pinii (PA5, PC12, PD2, PE2–PE4) și ceasul de 180 MHz, iar după regenerare
compilează și verifică jurnalul de boot. Dacă CubeMX scoate din proiect fișierele HAL pentru LTDC, DMA2D,
SDRAM (`stm32f4xx_ll_fmc.c`), SPI sau I2C, adaugă-le înapoi în grupul `Drivers/STM32F4xx_HAL_Driver`.

## Verificat

Lista de fișiere, definițiile și căile din `Template_GPIO.uvprojx` au fost compilate cu GCC în modul strict C99
(fără avertismente în codul aplicației). Imaginea a rulat pe placă: ceas 180 MHz, jurnal pe USB, citiri trimise
pe UART5 și mesaje primite de la ESP32. Compilarea cu Arm Compiler 6 în Keil și deschiderea `.ioc`-ului în CubeMX
nu au putut fi încercate pe calculatorul pe care s-a făcut portarea.
