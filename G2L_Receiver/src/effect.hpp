#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "common.h"

typedef struct {
    uint32_t lastTrigger;   // ms
    uint8_t alpha;
} effect_t;

class Effect {
    public:
    Effect(uint32_t attack = 0, uint32_t sustain = 1000, uint32_t release = 0, bool hasHold = false) 
        : _attack(attack), _sustain(sustain), _release(release), _hasHold(hasHold) { }

    void start() { _started = _held = millis(); };
    void hold() { _held = millis(); }
    void stop() { _started = 0; };
    bool running() { return !!_started; }

    virtual CRGB render(int idx) {
        if (idx == 0) {
            calcAlpha(_hasHold ? _held : _started);
        }
        return CRGB::Black;
    }

    void calcAlpha(uint32_t timingBase) {
        if (!_started) {
            _alpha = 0;
            return;
        }
        uint32_t runtime = millis() - timingBase;
        if (runtime < _attack) {
            _alpha = runtime * 255 / _attack;
        }
        else if (runtime < _attack + _sustain) {
            _alpha = UINT8_MAX;
        }
        else if (runtime < _attack + _sustain + _release) {
            _alpha = 255 - ((runtime - _attack - _sustain) * 255 / _release);
        }
        else {
            _alpha = 0;
            // automatically disable effect when any ASR value is set
            if (_attack + _sustain + _release) {    
                _started = 0;
            }
        }
    }

    protected:
    uint32_t _attack, _sustain, _release;   // in ms
    bool _hasHold;
    uint32_t _started = 0, _held = 0;
    uint8_t _alpha = 0;
};

class FXStrobe : public Effect {
    public:
    FXStrobe() : Effect(0, 100, 0, true) { }
    CRGB render(int idx) override {
        Effect::render(idx);
        if (_alpha != 0) {
            bool on = ((millis() / _strobeCycle) % 2) ^ _startInverted;
            return on ? CRGB::White : CRGB::Black;
        }
        return CRGB::Black;
    }

    void start() {
        Effect::start();
        _startInverted = (millis() / _strobeCycle) % 2;
    }

    uint32_t _strobeCycle = 25;  // ms, length of off/on interval
    bool _startInverted = false;
};

// button / effect association is done via order of this array
Effect *effects[] = {
    new FXStrobe(),
};
constexpr int effectsNum = sizeof(effects) / sizeof(effects[0]);


class EffectEngine {
    public:
    void loop() {
        uint32_t now = millis();

        for (int i = 0; i < _numPixels; i++) {
            uint32_t pixelOffset = i * (rainbowPeriod / _numPixels);
            bgColorHSV.hue = ((now + animOffset + pixelOffset) % rainbowPeriod) * 255 / rainbowPeriod;
            hsv2rgb_rainbow(bgColorHSV, bgColorRGB);

            for (int e = 0; e < effectsNum; e++) {
                if (effects[e]->running()) {
                    bgColorRGB = effects[e]->render(i);
                    break;  // for now, don't do blending. First come, first serve
                }
            }

            setPixel(i, bgColorRGB);
        }
    }

    void trigger(int triggerId, int eventType = BTN_PRESSED) {
        if (triggerId < effectsNum) {
            switch (eventType) {
                case BTN_PRESSED:   effects[triggerId]->start();    break;
                case BTN_HOLD:      effects[triggerId]->hold();     break;
                case BTN_RELEASED:  effects[triggerId]->stop();     break;
            }
        } 
    }

    void init(uint8_t **rgbDsts, uint8_t numPixels) {
        _rgbDestination = rgbDsts;
        _numPixels = numPixels;
    }

    void setPixel(int idx, CRGB color) {
        if (_rgbDestination && _rgbDestination[idx]) {
            memcpy(_rgbDestination[idx], bgColorRGB.raw, 3);
        }
    }

    protected:
    // background color fade
    const int rainbowPeriod = 5000;         // ms, how long a full rainbow revolution should last
    const int baseBrighness = 64;
    CHSV bgColorHSV = CHSV(0, 255, baseBrighness);
    CRGB bgColorRGB;
    int animOffset = 0; // in ms

    uint8_t **_rgbDestination = nullptr;    // memory location where to write new colors
    uint8_t _numPixels = 0;                 // number of pixels to consider in animations (max: 4)
};

static inline EffectEngine fx;