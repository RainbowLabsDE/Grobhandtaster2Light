#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>

#include <../../G2L_Receiver/src/common.h>

const int PIN_BUTTON = 9;
const int PIN_BAT_DIV = 4; // battery resistive divider

const int SEND_INTERVAL_HOLD = 25000;   // us when to send the hold event

payload_t payload = {
    .preamble = G2L_PREAMBLE,
};
esp_now_peer_info_t peer = {
    .peer_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    .channel = 0,
    .encrypt = false,
};


// round about 0.6mA in light sleep
void delaySleep(uint32_t us) {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup(us);
    esp_light_sleep_start();
}

void enterSleep() {
    esp_deep_sleep_enable_gpio_wakeup(digitalPinToBitMask(PIN_BUTTON), ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

// bool espNowTxDone = true;
// void espNowTxCb(const uint8_t *mac_addr, esp_now_send_status_t status) {
//     // Serial.println(micros());
//     espNowTxDone = true;
// }

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:     Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1:     Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER:    Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP:      Serial.println("Wakeup caused by ULP program"); break;
    default:                        Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(8, OUTPUT);
    digitalWrite(8, HIGH);

    // additional logic GNDs
    // pinMode(10, OUTPUT);
    // digitalWrite(10, LOW);
    pinMode(3, OUTPUT);
    digitalWrite(3, LOW);
    // pinMode(1, OUTPUT);
    // digitalWrite(1, LOW);

    print_wakeup_reason();

    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    // Add peer
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    // esp_now_register_send_cb(espNowTxCb);

    // basically disable ESP-NOW RX, only ~20mA left instead of ~80mA
    esp_now_set_wake_window(0);
}

esp_err_t send(payload_t payload, int repeats = 1, bool delayAfter = true) {
    payload.batVolt = analogReadMilliVolts(PIN_BAT_DIV) / 2;

    esp_err_t result = ESP_OK;
    for (int i = 0; i < repeats; i++) {
        // espNowTxDone = false;
        result = esp_now_send(peer.peer_addr, (uint8_t *)&payload, sizeof(payload));
        if (result != ESP_OK) break;
    }

    // Serial.print(micros());

    if (delayAfter) {
        delayMicroseconds(10000);   // needed because of random high power draw (30mA and falling) when entering sleep too early?
    }
    return result;
}

void sleepUntilButtonIO() {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    gpio_wakeup_enable((gpio_num_t)PIN_BUTTON, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();

    // uint64_t gpioMask = esp_sleep_get_gpio_wakeup_status();
    // Serial.printf("Wake from sleep. Mask: %016X\n", gpioMask);
    // digitalWrite(8, LOW);
}

void loop() {
    sleepUntilButtonIO();
    // ToDo: regular wakeup for checkin message?

    payload.btnState = BTN_PRESSED;
    send(payload, 3);

    while (!digitalRead(PIN_BUTTON)) {
        payload.btnState = BTN_HOLD;
        send(payload, 2);
        delaySleep(SEND_INTERVAL_HOLD); // TODO: need to randomize this a bit?
    }

    payload.btnState = BTN_RELEASED;
    send(payload, 3);

}
