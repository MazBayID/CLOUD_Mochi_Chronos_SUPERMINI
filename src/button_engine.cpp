#include "button_engine.h"
#include "dirgamochi_config.h"

void TouchButton::begin(uint8_t pin)
{
    _pin = pin;
    pinMode(_pin, INPUT_PULLDOWN); // push button: internal pull-down holds pin LOW when released
    _lastRaw = digitalRead(_pin) == HIGH;
    _stableState = _lastRaw;
    _lastChangeMs = millis();
}

ButtonEvent TouchButton::update()
{
    bool raw = digitalRead(_pin) == HIGH;
    unsigned long now = millis();

    if (raw != _lastRaw)
    {
        _lastChangeMs = now;
        _lastRaw = raw;
    }

    ButtonEvent ev = BTN_NONE;

    if ((now - _lastChangeMs) > BTN_DEBOUNCE_MS && raw != _stableState)
    {
        _stableState = raw;
        if (_stableState)
        {
            _pressStartMs = now;
            _longFired = false;
        }
        else
        {
            if (!_longFired)
            {
                ev = BTN_SHORT_PRESS;
            }
        }
    }

    if (_stableState && !_longFired && (now - _pressStartMs) >= BTN_LONGPRESS_MS)
    {
        _longFired = true;
        ev = BTN_LONG_PRESS;
    }

    return ev;
}   