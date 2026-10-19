# Protocol Smart Bottle

Toate mesajele sunt text ASCII, terminate cu `\n`, câmpuri separate prin virgulă.
Același format circulă pe UART (STM32 ↔ ESP32) și pe BLE (ESP32 ↔ telefon) —
ESP32 doar retransmite, nu interpretează (cu excepția mesajului `S`, pe care îl generează el).
În modul mock (`MOCK_STM32 1`), ESP32 produce el mesajele STM32-ului, în exact același format.

## BLE (Nordic UART Service — compatibil nRF Connect pentru teste)

| Rol | UUID | Proprietăți |
|---|---|---|
| Serviciu | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | |
| RX (telefon → dispozitiv) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write, Write w/o response |
| TX (dispozitiv → telefon) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Notify |

Nume advertising: `SmartBottle`. Mesajele încap în MTU-ul implicit (≤ 20 octeți).

## Dispozitiv → telefon

| Mesaj | Semnificație |
|---|---|
| `L,<raw>,<pct>,<stable>` | Citire. `raw` = valoare filtrată (ADC 0–4095 sau HX711 cu semn), `pct` = 0–100 conform calibrării, `stable` = 1 dacă ultimele N citiri sunt stabile |
| `A,C` | Calibrare acceptată |
| `E,C` | Calibrare respinsă (gol == plin) |

Exemplu: `L,2310,64,1`

## Telefon → dispozitiv

| Mesaj | Semnificație |
|---|---|
| `C,<raw_gol>,<raw_plin>` | Setează calibrarea folosită pentru procent și LCD |
| `R` | Cere o citire imediată |

## ESP32 → STM32 (intern)

| Mesaj | Semnificație |
|---|---|
| `S,1` / `S,0` | Telefon conectat / deconectat |

## Reguli pentru aplicație

- Calibrarea se ține **în aplicație** (sursa de adevăr). STM32 o păstrează doar în RAM,
  deci aplicația trimite `C,...` la **fiecare conectare**.
- Conversia în mL și detectarea evenimentelor de consum se fac **în aplicație**,
  folosind doar citirile cu `stable = 1`.
- Creșterea nivelului = eveniment de reumplere, nu consum negativ.
