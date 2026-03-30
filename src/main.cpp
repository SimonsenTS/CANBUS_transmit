#include <Arduino.h>
#include <FastLED.h>
#include "driver/twai.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// ------------------------------------------------------------
// Hardware configuration
// ------------------------------------------------------------
#define LED_PIN      48
#define NUM_LEDS     1
#define CAN_TXD_PIN  GPIO_NUM_4
#define CAN_RXD_PIN  GPIO_NUM_5
#define CAN_BAUDRATE TWAI_TIMING_CONFIG_500KBITS()
#define RELAY_PIN    13

// WiFi Access Point credentials (override via build flags in platformio.ini)
#ifndef WIFI_SSID
  #define WIFI_SSID     "CAN_AP"
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD "12345678"
#endif

// ------------------------------------------------------------
// WS2812B status LED
//   Green flash  = TX OK
//   Red flash    = TX error
//   Blue flash   = RX frame received
//   Black        = idle
// ------------------------------------------------------------
class StatusLed {
public:
    void begin() {
        FastLED.addLeds<WS2812B, LED_PIN, GRB>(_leds, NUM_LEDS);
        FastLED.setBrightness(50);
        set(CRGB::Black);
    }
    void flash(CRGB colour, uint32_t ms = 100) {
        set(colour);
        delay(ms);
        set(CRGB::Black);
    }
    void set(CRGB colour) {
        _leds[0] = colour;
        FastLED.show();
    }
private:
    CRGB _leds[NUM_LEDS];
};

// ------------------------------------------------------------
// CAN controller — wraps ESP-IDF TWAI driver
// ------------------------------------------------------------
class CanController {
public:
    CanController(gpio_num_t txPin, gpio_num_t rxPin) {
        _gConfig = TWAI_GENERAL_CONFIG_DEFAULT(txPin, rxPin, TWAI_MODE_NORMAL);
        _tConfig = CAN_BAUDRATE;
        _fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    }

    bool begin() {
        if (twai_driver_install(&_gConfig, &_tConfig, &_fConfig) != ESP_OK) {
            Serial.println("[CAN] Driver install failed!");
            return false;
        }
        if (twai_start() != ESP_OK) {
            Serial.println("[CAN] Start failed!");
            return false;
        }
        Serial.printf("[CAN] Started — TX=GPIO%d  RX=GPIO%d\n",
                      (int)_gConfig.tx_io, (int)_gConfig.rx_io);
        return true;
    }

    // Send a CAN frame. Set extd=true for 29-bit extended IDs.
    bool sendFrame(uint32_t id, const uint8_t* data, uint8_t len, bool extd = false) {
        twai_message_t msg = {};
        msg.identifier       = id;
        msg.data_length_code = len;
        msg.extd             = extd ? 1 : 0;
        memcpy(msg.data, data, len);

        esp_err_t result = twai_transmit(&msg, pdMS_TO_TICKS(100));
        if (result != ESP_OK) {
            Serial.printf("[CAN] TX error 0x%X (id=0x%08X)\n", result, id);
            return false;
        }
        Serial.printf("[CAN] TX id=0x%08X extd=%d data=", id, extd);
        for (uint8_t i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
        Serial.println();
        return true;
    }

    // Poll for a received frame — returns true if one was available
    bool receive() {
        twai_message_t msg = {};
        if (twai_receive(&msg, 0) != ESP_OK) return false;
        Serial.printf("[CAN] RX id=0x%08X extd=%d len=%d data=",
                      msg.identifier, msg.extd, msg.data_length_code);
        for (uint8_t i = 0; i < msg.data_length_code; i++)
            Serial.printf("%02X ", msg.data[i]);
        Serial.println();
        return true;
    }

private:
    twai_general_config_t _gConfig;
    twai_timing_config_t  _tConfig;
    twai_filter_config_t  _fConfig;
};

// ------------------------------------------------------------
// Globals
// ------------------------------------------------------------
StatusLed       statusLed;
CanController   canBus(CAN_TXD_PIN, CAN_RXD_PIN);
AsyncWebServer  server(80);

// Last TX result for the web UI feedback
volatile bool   lastTxOk   = false;
volatile int    lastBrand  = 0;

// ------------------------------------------------------------
// CAN TX helpers — one per brand
// ------------------------------------------------------------

// Brand 1 — Deutz / Valtra / MF
// ID: 0x18FF5806 (extended 29-bit)  data[4] == 0x01
bool sendBrand1() {
    uint8_t d[8] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    return canBus.sendFrame(0x18FF5806, d, 8, true);
}

// Brand 2 — CaseIH / New Holland
// ID: 0x14FF7706 (extended 29-bit)  engage: data[0]=130, data[1]=1
bool sendBrand2() {
    uint8_t d[8] = {130, 1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return canBus.sendFrame(0x14FF7706, d, 8, true);
}

// Brand 3 — Fendt
// ID: 0x613 (standard 11-bit)  pattern: {0x15,0x22,0x06,0xCA,0x80,0x01,0x00,0x00}
bool sendBrand3() {
    uint8_t d[8] = {0x15, 0x22, 0x06, 0xCA, 0x80, 0x01, 0x00, 0x00};
    return canBus.sendFrame(0x613, d, 8, false);
}

// Brand 4 — FendtOne
// ID: 0x0CFFD899 (extended 29-bit)  data[3] == 0xF6
bool sendBrand4() {
    uint8_t d[8] = {0x00, 0x00, 0x00, 0xF6, 0x00, 0x00, 0x00, 0x00};
    return canBus.sendFrame(0x0CFFD899, d, 8, true);
}

// ------------------------------------------------------------
// Web page HTML
// ------------------------------------------------------------
static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CAN TX Control</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Segoe UI', sans-serif;
      background: #1a1a2e;
      color: #e0e0e0;
      display: flex;
      flex-direction: column;
      align-items: center;
      min-height: 100vh;
      padding: 40px 16px;
    }
    h1 { font-size: 1.6rem; margin-bottom: 8px; letter-spacing: 1px; color: #a8d8ea; }
    p.sub { font-size: 0.85rem; color: #888; margin-bottom: 32px; }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 20px;
      width: 100%;
      max-width: 800px;
    }
    .card {
      background: #16213e;
      border: 1px solid #0f3460;
      border-radius: 12px;
      padding: 24px 20px;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 14px;
    }
    .card h2 { font-size: 1rem; color: #e94560; text-align: center; }
    .card .meta { font-size: 0.72rem; color: #667; text-align: center; line-height: 1.5; font-family: monospace; }
    button {
      background: #e94560;
      color: #fff;
      border: none;
      border-radius: 8px;
      padding: 12px 28px;
      font-size: 0.95rem;
      cursor: pointer;
      transition: background 0.15s, transform 0.1s;
      width: 100%;
    }
    button:hover  { background: #c73652; }
    button:active { transform: scale(0.97); }
    #status {
      margin-top: 32px;
      padding: 12px 24px;
      border-radius: 8px;
      font-size: 0.9rem;
      background: #0f3460;
      min-width: 260px;
      text-align: center;
      transition: background 0.3s;
    }
    #status.ok  { background: #1a5c3a; }
    #status.err { background: #5c1a1a; }
  </style>
</head>
<body>
  <h1>&#128268; CAN TX Control</h1>
  <p class="sub">ESP32-S3 &mdash; 500 kbit/s</p>

  <div class="grid">

    <div class="card">
      <h2>Deutz / Valtra / MF</h2>
      <div class="meta">ID: 0x18FF5806 (ext)<br>data[4] = 0x01</div>
      <button onclick="send(1)">Send Brand 1</button>
    </div>

    <div class="card">
      <h2>CaseIH / New Holland</h2>
      <div class="meta">ID: 0x14FF7706 (ext)<br>data[0]=130, data[1]=1</div>
      <button onclick="send(2)">Send Brand 2</button>
    </div>

    <div class="card">
      <h2>Fendt</h2>
      <div class="meta">ID: 0x613 (std)<br>{15 22 06 CA 80 01 00 00}</div>
      <button onclick="send(3)">Send Brand 3</button>
    </div>

    <div class="card">
      <h2>FendtOne</h2>
      <div class="meta">ID: 0x0CFFD899 (ext)<br>data[3] = 0xF6</div>
      <button onclick="send(4)">Send Brand 4</button>
    </div>

  </div>

  <div id="status">Ready</div>

  <script>
    async function send(brand) {
      const el = document.getElementById('status');
      el.className = '';
      el.textContent = 'Sending...';
      try {
        const r = await fetch('/send?brand=' + brand);
        const t = await r.text();
        el.textContent = t;
        el.className = r.ok ? 'ok' : 'err';
      } catch(e) {
        el.textContent = 'Network error';
        el.className = 'err';
      }
    }
  </script>
</body>
</html>
)rawliteral";

// ------------------------------------------------------------
// Arduino entry points
// ------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("ESP32-S3 CAN TX board starting...");

    statusLed.begin();

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    // Start CAN bus
    if (canBus.begin()) {
        statusLed.flash(CRGB::Green, 300);
    } else {
        statusLed.set(CRGB::Red);
    }

    // Start WiFi Access Point
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[WiFi] AP started — SSID: %s  IP: %s\n",
                  WIFI_SSID, WiFi.softAPIP().toString().c_str());

    // Serve web UI
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", INDEX_HTML);
    });

    // Handle TX requests: /send?brand=1..4
    server.on("/send", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("brand")) {
            req->send(400, "text/plain", "Missing brand parameter");
            return;
        }
        int brand = req->getParam("brand")->value().toInt();
        bool ok = false;
        switch (brand) {
            case 1: ok = sendBrand1(); break;
            case 2: ok = sendBrand2(); break;
            case 3: ok = sendBrand3(); break;
            case 4: ok = sendBrand4(); break;
            default:
                req->send(400, "text/plain", "Unknown brand");
                return;
        }
        lastTxOk  = ok;
        lastBrand = brand;
        if (ok) {
            statusLed.flash(CRGB::Green, 80);
            req->send(200, "text/plain",
                      String("OK — Brand ") + brand + " frame sent");
        } else {
            statusLed.flash(CRGB::Red, 80);
            req->send(500, "text/plain",
                      String("ERROR — Brand ") + brand + " TX failed");
        }
    });

    server.begin();
    Serial.println("[HTTP] Server started on port 80");
}

void loop() {
    // Forward any received CAN frames to serial
    if (canBus.receive()) {
        statusLed.flash(CRGB::Blue, 50);
    }
}