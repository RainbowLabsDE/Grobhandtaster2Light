#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "common.h"

static const int RAINBOW_PERIOD = 5000;         // ms, how long a full rainbow revolution should last
static const int BASE_BRIGHTNESS = 32;


class Effect {
    public:
    Effect(uint32_t attack = 0, uint32_t sustain = 1000, uint32_t release = 0, bool hasHold = false) 
        : _attack(attack), _sustain(sustain), _release(release), _hasHold(hasHold) { }

    void init(uint8_t numPixels = 2) { _numPixels = numPixels; }
    virtual void start() { _started = _held = millis(); };
    virtual void hold() { _held = millis(); }
    virtual void stop() { _started = 0; };
    bool running() { return !!_started; }
    uint8_t alpha() { return _alpha; }

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
    uint8_t _numPixels;
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

    void start() override {
        Effect::start();
        _startInverted = (millis() / _strobeCycle) % 2;
    }

    uint32_t _strobeCycle = 25;  // ms, length of off/on interval
    bool _startInverted = false;
};

class FXRainbowFlash : public Effect {
    public:
    FXRainbowFlash() : Effect(0, 0, 350, true) { }
    CRGB render(int idx) override {
        Effect::render(idx);

        if (idx == _numPixels - 1) {
            if (_alpha < BASE_BRIGHTNESS) {
                Effect::stop(); // stop effect when background brightness got reached (avoids steppy fade look)
            }
        }

        uint32_t pixelOffset = idx * (RAINBOW_PERIOD / _numPixels);
        CHSV hsv = CHSV(((millis() + pixelOffset) % RAINBOW_PERIOD) * 255 / RAINBOW_PERIOD, 255, 255);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        return rgb;
    }
    void stop() override {}
};

// TODO: oddEven pallette jump

// button / effect association is done via order of this array
Effect *effects[] = {
    new FXStrobe(),
    new FXRainbowFlash(),
};
constexpr int effectsNum = sizeof(effects) / sizeof(effects[0]);


class EffectEngine {
    public:
    void loop() {
        uint32_t now = millis();

        for (int i = 0; i < _numPixels; i++) {
            uint32_t pixelOffset = i * (RAINBOW_PERIOD / _numPixels);
            bgColorHSV.hue = ((now + animOffset + pixelOffset) % RAINBOW_PERIOD) * 255 / RAINBOW_PERIOD;
            hsv2rgb_rainbow(bgColorHSV, bgColorRGB);
            bgColorRGB = bgColorRGB.scale8(BASE_BRIGHTNESS);

            for (int e = 0; e < effectsNum; e++) {
                if (effects[e]->running()) {
                    bgColorRGB = effects[e]->render(i);
                    bgColorRGB = bgColorRGB.scale8(effects[e]->alpha());
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
        for (auto e : effects) {
            e->init(_numPixels);
        }
    }

    void setPixel(int idx, CRGB color) {
        if (_rgbDestination && _rgbDestination[idx]) {
            memcpy(_rgbDestination[idx], bgColorRGB.raw, 3);
        }
    }

    protected:
    // background color fade
    CHSV bgColorHSV = CHSV(0, 255, 255);
    CRGB bgColorRGB;
    int animOffset = 0; // in ms

    uint8_t **_rgbDestination = nullptr;    // memory location where to write new colors
    uint8_t _numPixels = 0;                 // number of pixels to consider in animations (max: 4)
};

static inline EffectEngine fx;