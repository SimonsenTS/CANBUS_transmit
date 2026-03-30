#include <Arduino.h>
#include <FastLED.h>
#include "driver/twai.h"

// ------------------------------------------------------------
// Hardware configuration
// ------------------------------------------------------------
#define LED_PIN      48
#define NUM_LEDS     1
#define CAN_TXD_PIN  GPIO_NUM_4
#define CAN_RXD_PIN  GPIO_NUM_5
#define CAN_BAUDRATE TWAI_TIMING_CONFIG_500KBITS()  // change to e.g. _250KBITS() if needed
#define RELAY_PIN    13

// ------------------------------------------------------------
// WS2812B status LED
//   Green flash = TX OK
//   Red         = error
//   Blue flash  = RX frame received
//   Black       = idle
// ------------------------------------------------------------
class StatusLed {
public:
    StatusLed() {}  // no hardware calls in constructor

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
        _gConfig  = TWAI_GENERAL_CONFIG_DEFAULT(txPin, rxPin, TWAI_MODE_NORMAL);
        _tConfig  = CAN_BAUDRATE;
        _fConfig  = TWAI_FILTER_CONFIG_ACCEPT_ALL();
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

    // Send a standard CAN frame
    bool sendFrame(uint32_t id, const uint8_t* data, uint8_t len) {
        twai_message_t msg = {};
        msg.identifier     = id;
        msg.data_length_code = len;
        msg.extd           = 0;   // standard 11-bit ID
        memcpy(msg.data, data, len);

        esp_err_t result = twai_transmit(&msg, pdMS_TO_TICKS(100));
        if (result != ESP_OK) {
            Serial.printf("[CAN] TX error 0x%X (id=0x%03X)\n", result, id);
            return false;
        }
        Serial.printf("[CAN] TX id=0x%03X data=", id);
        for (uint8_t i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
        Serial.println();
        return true;
    }

    // Poll for a received frame — returns true if one was available
    bool receive() {
        twai_message_t msg = {};
        if (twai_receive(&msg, 0) != ESP_OK) return false;

        Serial.printf("[CAN] RX id=0x%03X len=%d data=",
                      msg.identifier, msg.data_length_code);
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

// --------------- Arduino entry points ----------------------

StatusLed     statusLed;
CanController canBus(CAN_TXD_PIN, CAN_RXD_PIN);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("ESP32-S3 CAN transceiver (MAX3051EKA) starting...");

    statusLed.begin();   // init LED hardware after Serial is ready

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);  // relay off at startup

    if (canBus.begin()) {
        statusLed.flash(CRGB::Green, 500);
    } else {
        statusLed.set(CRGB::Red);
    }
}

void loop() {
    // Receive any incoming frames
    if (canBus.receive()) {
        statusLed.flash(CRGB::Blue);
    }

    // Send a test frame every 2 seconds
    static uint32_t lastTx    = 0;
    static uint8_t  txCount   = 0;   // counts successful TX frames
    if (millis() - lastTx >= 2000) {
        lastTx = millis();
        uint8_t payload[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03 };
        bool ok = canBus.sendFrame(0x123, payload, sizeof(payload));
        if (ok) {
            txCount++;
            // Every 2nd TX: toggle the relay (ON on even counts, OFF on odd counts)
            bool relayOn = (txCount % 2 == 0);
            digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);
            Serial.printf("[RELAY] GPIO%d -> %s (txCount=%d)\n",
                          RELAY_PIN, relayOn ? "ON" : "OFF", txCount);
        }
        statusLed.flash(ok ? CRGB::Green : CRGB::Red);
    }
}