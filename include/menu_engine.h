#ifndef MENU_ENGINE_H
#define MENU_ENGINE_H

#include <Adafruit_SH110X.h>
#include <ChronosESP32.h>
#include <Preferences.h>
#include "buzzer_engine.h"

enum MenuItem
{
    MENU_BUZZER = 0,
    MENU_VOLUME,
    MENU_ROTATION,
    MENU_RESET_PAIRING,
    MENU_DEVICE_INFO,
    MENU_COUNT
};

class MenuEngine
{
public:
    void begin(Adafruit_SH1106G *display, ChronosESP32 *watch, BuzzerEngine *buzzer);

    void enter();
    void moveNext();
    void activate();
    void draw();

    bool isShowingDetail() const { return _showingDetail; }
    void closeDetail() { _showingDetail = false; }

    bool buzzerEnabled() const { return _soundMode >= 1; }
    bool ambientEnabled() const { return _soundMode >= 2; }
    uint8_t oledRotation() const { return _oledRotation; }

private:
    Adafruit_SH1106G *_display = nullptr;
    ChronosESP32 *_watch = nullptr;
    BuzzerEngine *_buzzer = nullptr;
    Preferences _prefs;

    int _selected = 0;
    uint8_t _soundMode = 2;
    uint8_t _oledRotation = 2;
    uint8_t _volumeLevel = 1;
    bool _showingDetail = false;

    String _statusMsg;
    unsigned long _statusUntil = 0;

    void loadPrefs();
    void setStatus(const char *msg, unsigned long durationMs = 1500);
    void drawDeviceInfo();
};

#endif // MENU_ENGINE_H