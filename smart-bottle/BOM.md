# Listă de componente — Smart Bottle

Proiectul are două variante de senzor. **A** e cea din specificația inițială, **B** e varianta recomandată
(măsurare prin cântărire, nimic nu atinge apa). Cumperi doar setul variantei alese. Restul e comun.

## 1. Plăci principale (comune)

| # | Componentă | Cant. | Observații |
|---|---|---|---|
| 1 | STM32F429I-DISC1 (32F429IDISCOVERY) | 1 | Cortex-M4 cu LCD TFT 2.4" integrat |
| 2 | ESP32 DevKit V1 (ESP32-WROOM-32) | 1 | Orice DevKit cu GPIO16/17 accesibili și LED pe GPIO2 |

## 2. Senzor — varianta A (senzor analogic rezistiv)

| # | Componentă | Cant. | Observații |
|---|---|---|---|
| 3A | Water Level Sensor (PCB analogic cu 3 pini: S, +, −) | 1 | Zonă activă ~4 cm; domeniu limitat pentru o sticlă întreagă |
| 4A | Silicon / adeziv termic | – | Izolează conexiunile senzorului de stropi |

## 2. Senzor — varianta B (celulă de sarcină + HX711, recomandată)

| # | Componentă | Cant. | Observații |
|---|---|---|---|
| 3B | Celulă de sarcină tip bară, 2 kg sau 3 kg | 1 | Sticlă de 750 mL plină cântărește ~1 kg; o celulă de 1 kg ar fi la limită |
| 4B | Modul amplificator HX711 | 1 | Pinul RATE la VCC dă 80 SPS (citiri mai rapide). Pinii vin de obicei nelipiți |
| 5B | Placă suport + placă platformă (MDF, plexiglas sau print 3D, ~12×12 cm) | 2 | Formează „coaster-ul” pe care stă sticla |
| 6B | Șuruburi M4 sau M5 + distanțiere/șaibe | 4 + 4 | Pentru fixarea celulei între cele două plăci; verifică filetul celulei |

## 3. Conectică și montaj (comune)

| # | Componentă | Cant. | Observații |
|---|---|---|---|
| 7 | Breadboard 830 puncte | 1 | Opțional dacă legi direct cu fire F-F |
| 8 | Fire Dupont F-F | 10 | Pinii ambelor plăci sunt tată, deci F-F merg direct placă la placă |
| 9 | Fire Dupont M-F și M-M | 10 + 10 | Pentru breadboard |
| 10 | Barete de pini tată 2,54 mm | 1 | Pentru HX711 (varianta B) |
| 11 | Rezistențe 1 kΩ | 2 | Opțional: în serie pe liniile UART, protejează la cablare greșită. Nu e nevoie de divizor: ambele plăci lucrează pe 3,3 V |

## 4. Alimentare și programare (comune)

| # | Componentă | Cant. | Observații |
|---|---|---|---|
| 12 | Cablu USB **Mini-B** | 1 | Pentru ST-LINK (programare + alimentare STM32). Atenție: nu e Micro-USB |
| 13 | Cablu USB pentru ESP32 (Micro-USB sau USB-C, după modelul plăcii) | 1 | Trebuie să fie cablu de date, nu doar de încărcare |
| 14 | Încărcător USB 5 V / 2 A cu 2 porturi sau powerbank | 1 | Opțional, pentru funcționare fără PC. GND-ul plăcilor rămâne comun prin firul de masă |

## 5. Recipient și accesorii

| # | Componentă | Cant. | Observații |
|---|---|---|---|
| 15 | Sticlă/recipient de test (plastic transparent) | 1 | Varianta A: senzorul se fixează pe peretele interior |
| 16 | Pahar gradat sau cântar de bucătărie | 1 | Verifici calibrarea (mL măsurați față de mL afișați) |

## 6. Unelte (dacă nu le ai deja)

| # | Unealtă | Observații |
|---|---|---|
| 17 | Letcon + fludor | Barete HX711, fire celulă de sarcină |
| 18 | Multimetru | Verificare continuitate și tensiuni 3,3 V / 5 V |
| 19 | Tub termocontractabil | Izolarea lipiturilor |

## 7. Dispozitive pentru aplicație

| Dispozitiv | Cerință |
|---|---|
| PC Windows 10/11 | Bluetooth 4.0+ (BLE) integrat sau adaptor USB BLE; Chrome sau Edge |
| Telefon Android | Android 6+ cu Bluetooth LE; Chrome; aplicația servită prin HTTPS |
| (opțional) Telefon pentru teste | Aplicația nRF Connect, pentru testarea punții BLE fără aplicația noastră |

## 8. Software (gratuit)

| Software | Folosit pentru |
|---|---|
| STM32CubeIDE | Firmware STM32 (include CubeMX) |
| Pachetul STM32CubeF4 | Drivere BSP pentru LCD/SDRAM ale plăcii DISC1 |
| Arduino IDE + core „esp32” (Espressif) | Firmware ESP32 |
| Chrome / Edge | Aplicația web (Web Bluetooth) |
| Python 3 | Server local pentru aplicația web (`python -m http.server`) |
| Git | Repo-ul proiectului |
