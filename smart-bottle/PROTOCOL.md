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
| `P,<uptime_s>,<sursa_ok>` | Răspuns la ping, generat de **ESP32** (nu de STM32). `sursa_ok` = 1 dacă STM32 a trimis ceva în ultimele 3 s |

Exemplu: `L,2310,64,1`

## Telefon → dispozitiv

| Mesaj | Semnificație |
|---|---|
| `C,<raw_gol>,<raw_plin>` | Setează calibrarea folosită pentru procent și LCD |
| `R` | Cere o citire imediată |
| `P` | Ping (heartbeat). Răspunde ESP32, nu ajunge la STM32 |

## ESP32 → STM32 (intern)

| Mesaj | Semnificație |
|---|---|
| `S,1` / `S,0` | Telefon conectat / deconectat |

## Heartbeat

- Aplicația trimite `P` la 5 s și imediat după 3 s fără niciun mesaj (citirile vin la 1 s).
- 10 s fără niciun mesaj = legătură blocată: aplicația închide conexiunea GATT și se reconectează
  (5 încercări, ~21 s). Dacă nu reușește, afișează „Reconectează” (gest al utilizatorului, cu
  posibilitatea unei asocieri noi).
- Pong cu `sursa_ok = 0` = Bluetooth-ul merge, dar STM32 tace: aplicația nu se reconectează degeaba,
  ci arată că placa nu trimite date.
- ESP32: un client care a trimis deja `P` și apoi tace 20 s e considerat blocat; ESP32 închide
  conexiunea și reîncepe advertising-ul. Clienții fără ping (ex. nRF Connect) nu sunt deconectați.
- Scăderea lui `uptime_s` între două pong-uri înseamnă că ESP32 a repornit.

## Reguli pentru aplicație

- Calibrarea se ține **în aplicație** (sursa de adevăr). STM32 o păstrează doar în RAM,
  deci aplicația trimite `C,...` la **fiecare conectare**.
- Conversia în mL și detectarea evenimentelor de consum se fac **în aplicație**,
  folosind doar citirile cu `stable = 1`.
- Creșterea nivelului = eveniment de reumplere, nu consum negativ.
