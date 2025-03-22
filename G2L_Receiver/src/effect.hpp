#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "common.h"

static const int RAINBOW_PERIOD = 5000;         // ms, how long a full rainbow revolution should last
static const int BASE_BRIGHTNESS = 64;
static const int AFTER_EFFECT_PAUSE = 1000;     // ms, time
static const int AFTER_EFFECT_FADE_UP = 3000;

static const CRGB palette[][2] = {
    { CRGB::Cyan,       CRGB::Orange    },
    { CRGB::Magenta,    CRGB::Cyan      },
    { CRGB::Orange,     CRGB::Red       },
    { CRGB::Blue,       CRGB::LightGrey },
    { CRGB::Yellow,     CRGB::Magenta   },
};
static const int paletteNum = sizeof(palette) / sizeof(palette[0]); 


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
            // automatically disable effect when any ASR value is set
            if (_attack + _sustain + _release) {    
                _alpha = 0;
                _started = 0;
            }
            else {
                _alpha = 255;
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
    void stop() override {} // don't stop when button is released
    CRGB render(int idx) override {
        Effect::render(idx);

        // if (idx == _numPixels - 1) {
        //     if (_alpha < BASE_BRIGHTNESS) {
        //         Effect::stop(); // stop effect when background brightness got reached (avoids steppy fade look)
        //     }
        // }

        uint32_t pixelOffset = idx * (RAINBOW_PERIOD / _numPixels);
        CHSV hsv = CHSV(((millis() + pixelOffset) % RAINBOW_PERIOD) * 255 / RAINBOW_PERIOD, 240, 255);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        return rgb;
    }
};

class FXOddEven : public Effect {
    public:
    FXOddEven() : Effect(0, _fadeOutTime, 0, true) { }
    void start() override {
        if (millis() - max(_started, _held) > _fadeOutTime*4 && millis() - _lastPaletteSwap > _paletteSwapTime) {
            _lastPaletteSwap = millis();
            _curPaletteId++;
            if (_curPaletteId >= paletteNum) 
                _curPaletteId = 0;
        }
        Effect::start();
        _startedLight[_oddEven] = _started;
        // _curPaletteId = (millis() / _paletteSwapTime) % paletteNum;
    }
    void hold() override {
        Effect::hold();
        _startedLight[_oddEven] = _held;
    }
    // don't stop when button is released
    void stop() override { 
        _oddEven = !_oddEven;   // switch between odd/even 
    } 
    CRGB render(int idx) override {
        Effect::render(idx);

        int lightId = idx % _numLights;

        uint32_t runtime = millis() - _startedLight[lightId];
        uint8_t bright = 0;
        if (runtime < _fadeOutTime) {
            bright = 255 - (runtime * 255 / _fadeOutTime);
        }

        // if (idx == _numPixels - 1) {
        //     // check both timeouts, and set myself to disabled. Edit: nope, doesn't work as expected
        //     if (millis() - _startedLight[0] > _fadeOutTime && millis() - _startedLight[1] > _fadeOutTime) {
        //         _started = 0;
        //     }
        // }

        return palette[_curPaletteId][lightId].scale8(bright);
    }
    
    static const int _numLights = 2;
    static const int _fadeOutTime = 400;
    static const int _paletteSwapTime = 5000;
    bool _oddEven = true;
    uint32_t _startedLight[_numLights];
    int _curPaletteId = 0;
    uint32_t _lastPaletteSwap = 0;
};

// button / effect association is done via order of this array
Effect *effects[] = {
    new FXStrobe(),
    new FXOddEven(),
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
                    _lastEffectRun = millis();
                    break;  // for now, don't do blending. First come, first serve
                }
                else if (e == effectsNum - 1) {
                    // no effect active
                    uint8_t idleBright = 0;
                    if (millis() - _lastEffectRun > AFTER_EFFECT_PAUSE) {
                        idleBright = 255;
                        if (millis() - _lastEffectRun < AFTER_EFFECT_PAUSE + AFTER_EFFECT_FADE_UP) {
                            uint32_t ms = millis() - (_lastEffectRun + AFTER_EFFECT_PAUSE);
                            idleBright = ms * 255 / AFTER_EFFECT_FADE_UP;
                        }
                    }
                    bgColorRGB = bgColorRGB.scale8(idleBright);
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
    CHSV bgColorHSV = CHSV(0, 240, 255);    // sat=240 taken from FastLED fill_rainbow, idk
    CRGB bgColorRGB;
    int animOffset = 0; // in ms
    uint32_t _lastEffectRun = 0;

    uint8_t **_rgbDestination = nullptr;    // memory location where to write new colors
    uint8_t _numPixels = 0;                 // number of pixels to consider in animations (max: 4)
};

static inline EffectEngine fx;