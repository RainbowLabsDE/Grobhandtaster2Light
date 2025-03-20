#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include <esp_dmx.h>

#include "common.h"
#include "util.hpp"
#include "effect.hpp"

#include <FastLED.h>
const int NUM_LEDS = 24;
const int DATA_PIN = 22;
CRGB leds[NUM_LEDS];

const int PIN_DMX_TX = 16;
const int PIN_DMX_RX = 17;
const int PIN_DMX_EN = 21;

const int RX_TIMEOUT = 200;     // ms, timeout after which button is assumed released
const int RX_DEDUP_TIME = 15;   // ms, until next packet of same type is accepted again

dmx_port_t dmxPort = 1;
byte data[DMX_PACKET_SIZE];

// uint8_t *buttonMacAddr[] = {
//     STR2MAC("FF:FF:FF:FF:FF:FF"),
//     STR2MAC("12:34:56:78:9A:BC"),
// };

constexpr auto buttonMacAddr = std::to_array({
    str2mac("FF:FF:FF:FF:FF:00"),
    str2mac("3C:84:27:AD:7D:08"), 
    str2mac("12:34:56:78:9A:BC"), 
});

constexpr int buttonNum = buttonMacAddr.size();

uint32_t buttonLastReceived[buttonNum] = {0};
int buttonLastState[buttonNum] = {0};

// Function to find the index of a MAC address in the array
int findMacIndex(const uint8_t* macAddr) {
    auto it = std::find(buttonMacAddr.begin(), buttonMacAddr.end(), std::array<uint8_t, 6>{macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]});
    
    if (it != buttonMacAddr.end()) {
        return std::distance(buttonMacAddr.begin(), it);
    }
    return -1; // Not found
}


void handleButtonEvent(int buttonId, int buttonState) {
    if (buttonState == BTN_PRESSED || buttonState == BTN_RELEASED) {
        Serial.printf("B %d: %s\n", buttonId, (buttonState == BTN_PRESSED) ? "Pres" : "Rel");
    }

    fx.trigger(buttonId, buttonState);

    
    // Debug
    if (buttonState == BTN_PRESSED) {
        digitalWrite(2, HIGH);
    }
    else if (buttonState == BTN_RELEASED) {
        digitalWrite(2, LOW);
    }
}

void espNowRx(const esp_now_recv_info_t * esp_now_info, const uint8_t *data, int data_len) {
    uint8_t * src_mac = esp_now_info->src_addr;
    payload_t *payload = (payload_t*)data;

    Serial.printf("(%8d) RX: %s %4ddBm. (%2d): ", 
        millis(), 
        mac2str(src_mac), 
        esp_now_info->rx_ctrl->rssi,
        data_len);
    for (int i = 0; i < data_len; i++) {
        Serial.printf("%02X ", data[i]);
    }
    Serial.println();

    if (data_len == sizeof(payload_t) && payload->preamble == G2L_PREAMBLE) {
        // TODO: trigger effect based on sender MAC
        int buttonId = findMacIndex(src_mac);
        if (buttonId == -1) return;

        // Serial.printf("State: %d (%d), Time: %d (%d)\n", payload->btnState, buttonLastState[buttonId], millis(), buttonLastReceived[buttonId]);
        
        // deduplicate multiple sent events (except hold events)
        if (buttonLastState[buttonId] != payload->btnState || millis() - buttonLastReceived[buttonId] > 50 || payload->btnState == BTN_HOLD) {
            // inject "pressed" event when first newly received event is "hold" (missed "pressed" transmission)
            if (buttonLastState[buttonId] == BTN_RELEASED && payload->btnState == BTN_HOLD) {
                handleButtonEvent(buttonId, BTN_PRESSED);
            }
            handleButtonEvent(buttonId, payload->btnState);
        }

        buttonLastReceived[buttonId] = millis();
        buttonLastState[buttonId] = payload->btnState;
    }
}

// if only the pressed/held, but no released events got received, release the button after a timeout
void checkStuckButton() {
    for (int i = 0; i < buttonNum; i++) {
        if ((buttonLastState[i] == BTN_PRESSED || buttonLastState[i] == BTN_HOLD) && millis() - buttonLastReceived[i] > RX_TIMEOUT) {
            buttonLastState[i] = BTN_RELEASED;
            handleButtonEvent(i, BTN_RELEASED);
        }
    }
}

CRGB *outputColor[] = {
    new CRGB(),
    new CRGB(),
    new CRGB(),
    new CRGB(),
};

void setup() {
    Serial.begin(921600);
    pinMode(2, OUTPUT);
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS).setCorrection(TypicalSMD5050);
    FastLED.setBrightness(255);

    // Serial.println(mac2str(buttonMacAddr[0].data()));
    // Serial.println(mac2str(buttonMacAddr[1].data()));
    // uint8_t test[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    // Serial.println(findMacIndex(test));

    for (int i = 0; i < buttonNum; i++) {
        buttonLastState[i] = BTN_RELEASED;
    }

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(espNowRx);


    dmx_config_t config = DMX_CONFIG_DEFAULT;
    dmx_personality_t personalities[] = {};
    int personality_count = 0;
    dmx_driver_install(dmxPort, &config, personalities, personality_count);
    dmx_set_pin(dmxPort, PIN_DMX_TX, PIN_DMX_RX, PIN_DMX_EN);

    // payload_t payload = {
    //     .preamble = G2L_PREAMBLE,
    // };

    fx.init((uint8_t**)outputColor, 2);
}

void loop() {
    checkStuckButton();
    fx.loop();
    // CRGB toFill = applyGamma_video(outputColor, 2.2);
    // leds[0] = toFill;
    for (int i = 2; i < 10; i++) leds[i] = *outputColor[0];
    for (int i = 14; i < 22; i++) leds[i] = *outputColor[1];
    
    FastLED.show();
}
