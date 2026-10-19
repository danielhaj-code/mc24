/*
 * MockStm32 — înlocuiește temporar placa STM32F429I-DISC1.
 *
 * Generează exact mesajele pe care le-ar trimite STM32 pe UART (vezi PROTOCOL.md):
 *   L,<raw>,<pct>,<stable>   o dată pe secundă
 *   A,C / E,C                răspuns la calibrare
 * și răspunde la comenzile aplicației (C,<gol>,<plin> și R).
 *
 * Folosit doar cu MOCK_STM32 = 1. Citirea reală a senzorului rămâne treaba STM32-ului;
 * când placa e legată, pui MOCK_STM32 = 0 și ESP32 redevine punte transparentă.
 *
 * Comenzi pe Serial Monitor (USB, 115200):
 *   d = înghițitură   r = reumplere   l = ridică / pune sticla la loc
 *   a = pornește / oprește înghițiturile automate   ? = ajutor
 * Butonul BOOT (GPIO0) = înghițitură.
 */
#pragma once
#include <Arduino.h>

class MockStm32 {
public:
  // Aceleași valori ca în Demo-ul din aplicația web (senzor analogic, ADC 12 biți).
  static constexpr int RAW_EMPTY = 800;
  static constexpr int RAW_FULL  = 3000;
  static constexpr int STABLE_N  = 5;      // ca STABLE_COUNT din app_config.h
  static constexpr int STABLE_TOL = 15;    // ca STABLE_TOLERANCE

  typedef void (*LineSink)(const char *line, size_t len);

  explicit MockStm32(LineSink sink) : sink_(sink) {}

  void begin() {
    randomSeed(esp_random());
    calEmpty_ = RAW_EMPTY;
    calFull_  = RAW_FULL;
    level_ = 0.80f;
    nextAutoSip_ = millis() + 8000;
  }

  // Apelat din loop(). Emite o citire pe secundă.
  void tick() {
    uint32_t now = millis();
    if (autoSips_ && sipLeft_ == 0 && (int32_t)(now - nextAutoSip_) >= 0) {
      startSip();
      nextAutoSip_ = now + 20000 + random(0, 20000);
    }
    if ((int32_t)(now - nextReading_) >= 0) {
      nextReading_ = now + 1000;
      emitReading();
    }
  }

  // O linie de la aplicație (fără '\n'). Înlocuiește ce ar fi primit STM32 pe UART.
  void handleCommand(const char *line) {
    if (line[0] == 'C' && line[1] == ',') {
      long e = 0, f = 0;
      if (sscanf(line + 2, "%ld,%ld", &e, &f) == 2 && e != f) {
        calEmpty_ = e; calFull_ = f;
        send("A,C");
      } else {
        send("E,C");
      }
    } else if (line[0] == 'R' && line[1] == '\0') {
      emitReading();
    } else if (line[0] == 'S' && line[1] == ',') {
      // stare conexiune: STM32 aprinde LD3; aici doar o afișăm
      Serial.printf("[mock] telefon %s\n", line[2] == '1' ? "conectat" : "deconectat");
    }
  }

  // Comenzi de la tastatură (Serial Monitor) și butonul BOOT.
  void startSip() {
    if (sipLeft_ == 0 && present_) { sipLeft_ = 3; Serial.println("[mock] înghițitură..."); }
  }
  void refill() { level_ = 0.95f; Serial.println("[mock] reumplere"); }
  void toggleLift() { present_ = !present_; Serial.printf("[mock] sticla %s\n", present_ ? "pusă la loc" : "ridicată"); }
  void toggleAuto() { autoSips_ = !autoSips_; Serial.printf("[mock] înghițituri automate: %s\n", autoSips_ ? "DA" : "NU"); }

private:
  void emitReading() {
    float noise = (random(-300, 301)) / 100.0f;            // ±3 unități: zgomot de repaus
    if (sipLeft_ > 0) {
      sipLeft_--;
      noise = random(-200, 201);                             // sticla e înclinată: citiri haotice
      if (sipLeft_ == 0) {
        level_ -= 0.06f + random(0, 80) / 1000.0f;           // 6–14% din capacitate
        if (level_ < 0.12f) refill();
      }
    }
    // Sticla ridicată de pe senzor: valoare sub „gol”, aplicația o raportează ca lipsă.
    float base = present_ ? RAW_EMPTY + (RAW_FULL - RAW_EMPTY) * level_ : RAW_EMPTY - 400;
    long raw = lroundf(base + noise);
    if (raw < 0) raw = 0;
    if (raw > 4095) raw = 4095;

    hist_[histPos_] = raw;
    histPos_ = (histPos_ + 1) % STABLE_N;
    if (histCount_ < STABLE_N) histCount_++;
    long mn = hist_[0], mx = hist_[0];
    for (int i = 1; i < histCount_; i++) { mn = min(mn, hist_[i]); mx = max(mx, hist_[i]); }
    bool stable = histCount_ == STABLE_N && (mx - mn) <= STABLE_TOL;

    long span = calFull_ - calEmpty_;
    long pct = span ? (raw - calEmpty_) * 100 / span : 0;
    pct = constrain(pct, 0, 100);

    char buf[32];
    snprintf(buf, sizeof buf, "L,%ld,%ld,%d", raw, pct, stable ? 1 : 0);
    send(buf);
  }

  void send(const char *s) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%s\n", s);
    sink_(buf, (size_t)n);
  }

  LineSink sink_;
  long calEmpty_ = RAW_EMPTY, calFull_ = RAW_FULL;
  float level_ = 0.8f;
  int sipLeft_ = 0;
  bool present_ = true;
  bool autoSips_ = true;
  uint32_t nextReading_ = 0, nextAutoSip_ = 0;
  long hist_[STABLE_N] = {0};
  int histPos_ = 0, histCount_ = 0;
};
