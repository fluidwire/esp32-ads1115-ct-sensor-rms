/*
 * ESP32 ADS1115 AC Current RMS Meter (CT clamp) -> MQTT
 * -----------------------------------------------------
 * Measures AC load current with a split-core CT clamp (SCT-013-000,
 * 100A:50mA) read through an ADS1115 16-bit ADC, computes the RMS
 * current over a window of samples, and publishes it to MQTT as JSON.
 *
 * The CT outputs an AC current, so a burden resistor turns it into a
 * voltage and a 2x 10k divider biases that voltage to mid-supply
 * (1.65V) so the ADS1115 only sees positive voltages.
 *
 * Required libraries (install via Library Manager):
 *   - "Adafruit ADS1X15" by Adafruit  (pulls in "Adafruit BusIO")
 *   - "PubSubClient"     by Nick O'Leary
 *
 * Wiring:
 *   ADS1115 VDD -> ESP32 3V3
 *   ADS1115 GND -> ESP32 GND
 *   ADS1115 SDA -> ESP32 GPIO21
 *   ADS1115 SCL -> ESP32 GPIO22
 *   ADS1115 ADDR -> GND (address 0x48)
 *   ADS1115 A0  -> bias node (see below)
 *
 *   Bias + burden network (3.5mm jack from the CT):
 *     3V3 --10k--+--10k-- GND        (+ 10uF from node to GND)
 *                |
 *                +--------- CT jack tip   -> also to ADS1115 A0
 *     CT jack sleeve -> GND
 *     22 ohm burden resistor across the CT jack (tip to sleeve).
 *
 * SAFETY: clamp the CT around ONE live conductor only - never around
 * the whole cable. Never leave a CT clamped on a live wire with its
 * burden resistor disconnected. Mains can kill; work on a de-energised
 * circuit when wiring.
 *
 * Setup:
 *   1. Copy config.h.example to config.h
 *   2. Fill in WiFi + MQTT broker.
 *   3. Flash. Open Serial Monitor at 115200.
 *
 * Sample MQTT payload:
 *   {"irms_a":1.234,"apparent_va":283.8,"samples":420,"uptime_s":120}
 *
 * (c) 2026 Fluidwire. Released under the MIT License.
 * https://github.com/fluidwire
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <PubSubClient.h>
#include "config.h"

// ---- CT / front-end constants (adjust to match your hardware) ------------
const float CT_RATIO        = 2000.0f;  // SCT-013-000: 100A / 0.05A
const float BURDEN_OHMS     = 22.0f;    // burden resistor across the CT
const float MAINS_VOLTAGE   = 230.0f;   // used only for apparent power (VA)
const float NOISE_FLOOR_A   = 0.05f;    // readings below this are shown as 0

const uint16_t SAMPLES_PER_WINDOW = 400;     // ~0.5s at 860 SPS (25+ cycles @ 50Hz)
const uint32_t PUBLISH_INTERVAL_MS = 10 * 1000UL;

WiFiClient       wifiClient;
PubSubClient     mqtt(wifiClient);
Adafruit_ADS1115 ads;   // default I2C address 0x48

void connectWiFi() {
  Serial.print(F("[wifi] connecting to "));
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.print(F("[wifi] IP: "));
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  while (!mqtt.connected()) {
    Serial.print(F("[mqtt] connecting... "));
    bool ok = strlen(MQTT_USER) > 0
      ? mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)
      : mqtt.connect(MQTT_CLIENT_ID);
    if (ok) {
      Serial.println(F("ok"));
    } else {
      Serial.print(F("failed state="));
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

// Sample the biased AC waveform and return the RMS current in amps.
float measureIrms() {
  static int16_t raw[SAMPLES_PER_WINDOW];

  // Pass 1: collect samples and the DC bias (mean).
  double sum = 0;
  for (uint16_t i = 0; i < SAMPLES_PER_WINDOW; i++) {
    raw[i] = ads.readADC_SingleEnded(0);
    sum += raw[i];
  }
  double mean = sum / SAMPLES_PER_WINDOW;

  // Pass 2: RMS of the AC component (bias removed).
  double sq = 0;
  for (uint16_t i = 0; i < SAMPLES_PER_WINDOW; i++) {
    double v = ads.computeVolts((int16_t)(raw[i] - mean));
    sq += v * v;
  }
  double vrms = sqrt(sq / SAMPLES_PER_WINDOW);

  // Secondary current = V / burden; primary = secondary * CT ratio.
  float irms = (float)(vrms / BURDEN_OHMS) * CT_RATIO;
  return irms < NOISE_FLOOR_A ? 0.0f : irms;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(21, 22);
  if (!ads.begin()) {
    Serial.println(F("[ads1115] not found - check I2C wiring (SDA=21, SCL=22)"));
    while (1) delay(1000);
  }
  ads.setGain(GAIN_ONE);                       // +/-4.096V range (0.125 mV/bit)
  ads.setDataRate(RATE_ADS1115_860SPS);        // fastest rate for AC sampling
  Serial.println(F("[ads1115] ready"));

  connectWiFi();
}

void loop() {
  static uint32_t lastPublish = 0;

  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  uint32_t now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL_MS || lastPublish == 0) {
    float irms = measureIrms();

    char buf[128];
    int n = snprintf(buf, sizeof(buf),
      "{\"irms_a\":%.3f,\"apparent_va\":%.1f,\"samples\":%u,\"uptime_s\":%lu}",
      irms, irms * MAINS_VOLTAGE, (unsigned)SAMPLES_PER_WINDOW,
      (unsigned long)(now / 1000UL));

    Serial.print(F("[mqtt] "));
    Serial.println(buf);
    mqtt.publish(MQTT_TOPIC, buf, n);

    lastPublish = now;
  }

  delay(50);
}
