#pragma once

#include <Arduino.h>
#include <FastLED.h>

typedef struct {
    uint32_t started;   // ms

} effect_t;

class EffectEngine {
    public:
    void loop() {
        uint32_t now = millis();

        bgColorHSV.hue = ((now + animOffset) % rainbowPeriod) * 255 / rainbowPeriod;
        hsv2rgb_rainbow(bgColorHSV, bgColorRGB);

        if (_rgbDestination) {
            memcpy(_rgbDestination, bgColorRGB.raw, 3);
        }
    }

    void trigger(int triggerId) {
        
    }

    void init(uint8_t *rgbDst) {
        _rgbDestination = rgbDst;
    }

    protected:
    // background color fade
    const int rainbowPeriod = 2000;         // ms, how long a full rainbow revolution should last
    const int baseBrighness = 30;
    CHSV bgColorHSV = CHSV(0, 255, baseBrighness);
    CRGB bgColorRGB;
    int animOffset = 0; // in ms

    uint8_t *_rgbDestination = nullptr;      // memory location where to write new color
};

static inline EffectEngine fx;