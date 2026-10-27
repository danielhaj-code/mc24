/*
 * Smart Bottle — punte transparentă UART <-> BLE (Nordic UART Service)
 * Placă: ESP32 DevKit (Arduino core 2.x sau 3.x)
 *
 * Cablaj:  ESP32 GPIO16 (RX2) <- STM32 PC12 (UART5 TX)
 *          ESP32 GPIO17 (TX2) -> STM32 PD2  (UART5 RX)
 *          GND comun. Ambele pe 3.3 V: fără divizor de tensiune.
 *
 * Linii UART complete ("...\n") de la STM32 -> notificare BLE pe TX.
 * Scrieri BLE pe RX -> UART spre STM32, linie cu linie.
 * La conectare/deconectare trimite "S,1\n" / "S,0\n" spre STM32.
 *
 * Heartbeat (vezi PROTOCOL.md): aplicația trimite "P" la câteva secunde. ESP32 răspunde el însuși
 * cu "P,<uptime_s>,<sursa_ok>" (nu ajunge la STM32), unde sursa_ok = 1 dacă STM32 a trimis ceva
 * în ultimele SOURCE_TIMEOUT_MS. Dacă un client care a trimis deja "P" tace APP_TIMEOUT_MS,
 * conexiunea e considerată blocată: ESP32 o închide și reîncepe advertising-ul, ca aplicația să se
 * poată reconecta curat.
 *
 * MOCK_STM32 = 1: fără STM32. ESP32 generează singur citirile (MockStm32.h), ca să poți
 * testa BLE-ul și aplicația înainte de cablare. Citirea reală a senzorului rămâne pe STM32;
 * pune 0 când placa e legată și totul trece din nou prin UART, neschimbat.
 */
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// 1 = simulează STM32 (fără placă legată)   0 = punte reală UART <-> BLE
#ifndef MOCK_STM32
#define MOCK_STM32    1
#endif

#define DEVICE_NAME   "SmartBottle"
#define SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // telefon -> dispozitiv
#define TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // dispozitiv -> telefon

#define UART_RX_PIN   16
#define UART_TX_PIN   17
#define UART_BAUD     115200
#define LED_PIN       2        // LED-ul albastru de pe majoritatea DevKit-urilor
#define LINE_MAX      32
#define BOOT_BTN_PIN  0        // butonul BOOT; în modul mock = o înghițitură

#define SOURCE_TIMEOUT_MS  3000   // STM32 trimite o citire pe secundă
#define HEARTBEAT_MS       2000   // H,<uptime_s>,<aplicație> spre STM32, ca să știe că ESP32 trăiește
#define APP_ALIVE_MS       12000  // aplicația scrie cel puțin la 5 s (ping); 12 s fără nimic = nu răspunde
#define APP_TIMEOUT_MS     20000  // aplicația face ping la 5 s; 20 s fără nimic = conexiune blocată

static BLEServer *server = nullptr;
static BLECharacteristic *txChar = nullptr;
static volatile bool connected = false;
static bool wasConnected = false;

// Starea heartbeat-ului (scrisă și din callback-ul BLE, de aceea volatile)
static volatile uint32_t lastAppRx = 0;     // ultima scriere de la aplicație
static volatile bool appPings = false;      // clientul curent folosește heartbeat-ul
static volatile bool appBye = false;        // aplicația a trimis „B” (la revedere)
static uint32_t lastSourceRx = 0;           // ultima linie de la STM32 (sau mock)

static void notifyLine(const char *s, size_t n);

// Scrierile BLE sosesc pe task-ul stivei BLE; le procesăm în loop() prin coadă, linie cu linie.
static QueueHandle_t cmdQueue;
struct Cmd { char text[LINE_MAX]; };

#if MOCK_STM32
#include "MockStm32.h"
static void mockSink(const char *s, size_t n) {
  lastSourceRx = millis();
  notifyLine(s, n);
  Serial.write((const uint8_t *)s, n);      // ce ar fi trimis STM32, vizibil și pe USB
}
static MockStm32 mock(mockSink);
#else
static char lineBuf[LINE_MAX];
static size_t lineLen = 0;
static bool lineOverflow = false;
#endif

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { connected = true; appPings = false; appBye = false; lastAppRx = millis(); }
  void onDisconnect(BLEServer *) override { connected = false; }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    auto v = c->getValue();                 // std::string (core 2.x) sau String (core 3.x)
    size_t n = v.length();
    if (n == 0) return;
    lastAppRx = millis();
    // poate conține mai multe linii ("C,800,3000\nR\n"): le punem pe rând în coadă
    Cmd cmd; size_t len = 0;
    for (size_t i = 0; i <= n; i++) {
      char ch = i < n ? v.c_str()[i] : '\n';
      if (ch == '\r') continue;
      if (ch == '\n') {
        if (len) { cmd.text[len] = '\0'; xQueueSend(cmdQueue, &cmd, 0); }
        len = 0;
      } else if (len < LINE_MAX - 1) {
        cmd.text[len++] = ch;
      }
    }
  }
};

static void notifyLine(const char *s, size_t n) {
  if (!connected) return;                   // nimeni nu ascultă: aruncăm
  txChar->setValue((uint8_t *)s, n);
  txChar->notify();
}

// Răspunsul la ping, generat de ESP32 (nu trece pe la STM32).
static void sendPong() {
  bool sourceOk = lastSourceRx != 0 && millis() - lastSourceRx < SOURCE_TIMEOUT_MS;
  char buf[LINE_MAX];
  int n = snprintf(buf, sizeof buf, "P,%lu,%d\n", (unsigned long)(millis() / 1000), sourceOk ? 1 : 0);
  notifyLine(buf, (size_t)n);
}

// O linie de la aplicație: ping-ul îl tratăm aici, restul merge la STM32 (sau la mock).
static void handleAppLine(const char *line) {
  if (line[0] == 'B' && line[1] == '\0') {  // aplicația pleacă: închidem noi legătura,
    appBye = true;                          // altfel Windows o poate ține deschisă
    appPings = false;
    Serial.println("[app] la revedere, închid conexiunea");
    if (connected) server->disconnect(server->getConnId());
    return;
  }
  appBye = false;
  if (line[0] == 'P' && line[1] == '\0') {
    appPings = true;
    sendPong();
    return;
  }
#if MOCK_STM32
  mock.handleCommand(line);
#else
  Serial2.print(line);
  Serial2.write('\n');
  Serial.print("[app] ");                   // jurnal pe USB: ce trimite aplicația spre STM32
  Serial.println(line);
#endif
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.begin(115200);                     // debug pe USB
  cmdQueue = xQueueCreate(8, sizeof(Cmd));
#if MOCK_STM32
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  mock.begin();
#else
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
#endif

  BLEDevice::init(DEVICE_NAME);
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *svc = server->createService(SERVICE_UUID);
  txChar = svc->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());     // CCCD: permite clientului să activeze notificările

  BLECharacteristic *rxChar = svc->createCharacteristic(
      RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());
  svc->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);        // necesar pentru filtrul din Web Bluetooth / ble-plx
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

#if MOCK_STM32
  Serial.println("SmartBottle MOCK pornit (fără STM32). Comenzi: d r l a ?");
#else
  Serial2.print("S,0\n");
  Serial.println("SmartBottle bridge pornit");
#endif
}

#if MOCK_STM32
static void mockLoop() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'd': mock.startSip(); break;
      case 'r': mock.refill(); break;
      case 'l': mock.toggleLift(); break;
      case 'a': mock.toggleAuto(); break;
      case 'f': mock.toggleFreeze(); break;
      case '?': Serial.println("d = înghițitură, r = reumplere, l = ridică/pune sticla, a = auto on/off, f = îngheață/dezgheață sursa"); break;
      default: break;
    }
  }

  // BOOT: activ pe 0, cu debounce simplu
  static bool lastBtn = true;
  static uint32_t btnTs = 0;
  bool btn = digitalRead(BOOT_BTN_PIN);
  if (btn != lastBtn && millis() - btnTs > 50) {
    btnTs = millis();
    lastBtn = btn;
    if (!btn) mock.startSip();
  }
  mock.tick();
}
#else
// 0 = niciun client, 1 = client BLE care nu răspunde (nu mai scrie), 2 = aplicație activă
static int appState() {
  if (!connected || appBye) return 0;
  return millis() - lastAppRx < APP_ALIVE_MS ? 2 : 1;
}

// UART -> BLE, linie cu linie
static void uartLoop() {
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\n') {
      if (!lineOverflow && lineLen > 0) {
        lastSourceRx = millis();
        lineBuf[lineLen++] = '\n';
        notifyLine(lineBuf, lineLen);
        Serial.write((const uint8_t *)lineBuf, lineLen);
      }
      lineLen = 0;
      lineOverflow = false;
    } else if (!lineOverflow) {
      if (lineLen < LINE_MAX - 1) lineBuf[lineLen++] = c;
      else lineOverflow = true;             // linie prea lungă: aruncată
    }
  }
}

// Test fără telefon: ce scrii în monitorul serial USB (ex. "R", "C,100,3000") ajunge la STM32.
static void usbToUartLoop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    Serial2.write(c);
  }
}
#endif

void loop() {
  Cmd cmd;
  while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) handleAppLine(cmd.text);

#if MOCK_STM32
  mockLoop();
#else
  uartLoop();
  usbToUartLoop();

  static uint32_t lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = millis();
    char hb[24];
    int n = snprintf(hb, sizeof hb, "H,%lu,%d\n", (unsigned long)(millis() / 1000), appState());
    Serial2.write((const uint8_t *)hb, n);
  }
#endif

  // Conexiune blocată: clientul a folosit heartbeat-ul, apoi a tăcut. O închidem ca să
  // reînceapă advertising-ul și aplicația să se poată reconecta.
  if (connected && appPings && millis() - lastAppRx > APP_TIMEOUT_MS) {
    Serial.println("[heartbeat] aplicația nu mai răspunde, închid conexiunea");
    appPings = false;
    server->disconnect(server->getConnId());
  }

  // tranziții de conexiune (făcute aici, nu în callback, ca să nu blocăm stiva BLE)
  // onDisconnect nu vine mereu (ex. Windows ține legătura); numărul real de clienți decide
  if (connected && server->getConnectedCount() == 0) connected = false;

  if (connected != wasConnected) {
    wasConnected = connected;
    digitalWrite(LED_PIN, connected ? HIGH : LOW);
    handleAppLine(connected ? "S,1" : "S,0");
    if (!connected) {
      delay(300);                           // lasă stiva să elibereze conexiunea
      BLEDevice::startAdvertising();        // redevine vizibil pentru o nouă conectare
    }
  }
  delay(2);
}
