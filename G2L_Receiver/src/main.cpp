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
// const int DATA_PIN = 4;
CRGB leds[NUM_LEDS];

const int PIN_DMX_TX = 21;
const int PIN_DMX_RX = 5;
const int PIN_DMX_EN = 23; 
// const int PIN_DMX_TX = 22;
// const int PIN_DMX_RX = 21;
// const int PIN_DMX_EN = 17; // 19 for general enable

const int RX_TIMEOUT = 200;     // ms, timeout after which button is assumed released
const int RX_DEDUP_TIME = 15;   // ms, until next packet of same type is accepted again

dmx_port_t dmxPort = 1;
byte dmxData[DMX_PACKET_SIZE];

// uint8_t *buttonMacAddr[] = {
//     STR2MAC("FF:FF:FF:FF:FF:FF"),
//     STR2MAC("12:34:56:78:9A:BC"),
// };

constexpr auto buttonMacAddr = std::to_array({
    str2mac("3C:84:27:AD:E3:68"), // Spare (now strobe)
    str2mac("3C:84:27:AD:F1:0C"), // OddEven
    str2mac("E8:06:90:66:85:1C"), // Blink
    str2mac("3C:84:27:AD:7D:08"), // Strobe
});
constexpr int buttonNum = buttonMacAddr.size();

uint32_t buttonLastReceived[buttonNum] = {0};
int buttonLastState[buttonNum] = {0};

typedef struct {
    uint8_t master;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;
    uint8_t amber;
    uint8_t uv;
    uint8_t strobe;
    uint8_t macro;
    uint8_t macroSpeed;
} fixture_vega_arc_ii_t;

const int fixturesNum = 2;
#pragma pack(push, 1)
typedef struct {
    uint8_t startByte;
    fixture_vega_arc_ii_t fixtures[fixturesNum];
    // uint8_t padding[492];
} dmxData_t;
dmxData_t dmxPayload = {0};
#pragma pack(pop)


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

    Serial.printf("(%8d) %s %4ddBm", 
        millis(), 
        mac2str(src_mac), 
        esp_now_info->rx_ctrl->rssi);

    if (data_len == sizeof(payload_t) && payload->preamble == G2L_PREAMBLE) {
        // TODO: trigger effect based on sender MAC
        int buttonId = findMacIndex(src_mac);
        Serial.printf(" (%d):", buttonId);
        if (buttonId == -1) return;

        Serial.printf(" %d - %dmV", payload->btnState, payload->batVolt * 4);    //*4? shouldn't it be *2?

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
    else {
        for (int i = 0; i < data_len; i++) {
            Serial.printf("%02X ", data[i]);
        }
    }
    Serial.println();
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
    // new CRGB(),
    // new CRGB(),
    // new CRGB(),
    // new CRGB(),

    (CRGB*)&(dmxPayload.fixtures[0].red),  // hackedy-hack
    (CRGB*)&(dmxPayload.fixtures[1].red),
};

void setup() {
    Serial.begin(921600);
    pinMode(2, OUTPUT);
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS).setCorrection(TypicalSMD5050);
    FastLED.setBrightness(255);

    // RS485 Transceiver enable
    pinMode(19, OUTPUT);
    digitalWrite(19, HIGH);
    // 5V DCDC enable
    pinMode(16, OUTPUT);
    digitalWrite(16, HIGH);

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

    for (int i = 0; i < fixturesNum; i++) {
        dmxPayload.fixtures[i].master = 255;
    }
    // dmxPayload.fixtures[0].uv = 42;


    dmx_config_t config = DMX_CONFIG_DEFAULT;
    dmx_personality_t personalities[] = {};
    int personality_count = 0;
    bool ret =  dmx_driver_install(dmxPort, &config, personalities, personality_count);
    if (!ret) Serial.println("ERROR: Installing DMX driver");
    ret = dmx_set_pin(dmxPort, PIN_DMX_TX, PIN_DMX_RX, PIN_DMX_EN);
    if (!ret) Serial.println("ERROR: Setting DMX pins");

    // payload_t payload = {
    //     .preamble = G2L_PREAMBLE,
    // };

    fx.init((uint8_t**)outputColor, 2);
}

uint32_t lastRun = 0;

void loop() {
    checkStuckButton();
    fx.loop();
    // CRGB toFill = applyGamma_video(outputColor, 2.2);
    // leds[0] = toFill;
    for (int i = 2; i < 3; i++) leds[i] = *outputColor[0];
    for (int i = 14; i < 15; i++) leds[i] = *outputColor[1];
    // leds[0] = *outputColor[0];
    FastLED.show();


    
    dmx_write(dmxPort, &dmxPayload, sizeof(dmxPayload));
    dmx_send_num(dmxPort, sizeof(dmxPayload));
    // dmx_wait_sent(dmxPort, DMX_TIMEOUT_TICK);
    
}
