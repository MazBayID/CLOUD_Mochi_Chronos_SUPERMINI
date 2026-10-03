#ifndef CHRONOS_UI_H
#define CHRONOS_UI_H

#include <Adafruit_SH110X.h>
#include <ChronosESP32.h>
#include "face_engine.h"
#include "menu_engine.h"

enum UiScreen
{
    SCR_FACE = 0,
    SCR_TIME,
    SCR_WEATHER,
    SCR_NOTIFICATIONS,
    SCR_NAVIGATION,
    SCR_MUSIC,
    SCR_PHONE,
    SCR_QR,
    SCR_MENU,
    SCR_COUNT
};

class ChronosUI
{
public:
    void begin(Adafruit_SH1106G *display, ChronosESP32 *watch, FaceEngine *face, MenuEngine *menu);

    void nextScreen();
    void backToFace();
    void goTo(UiScreen s);
    void openMenu();
    UiScreen current() const { return _screen; }

    void update();

    void setQrCount(int count) { _qrCount = count; }
    void nextQr();

private:
    Adafruit_SH1106G *_display = nullptr;
    ChronosESP32 *_watch = nullptr;
    FaceEngine *_face = nullptr;
    MenuEngine *_menu = nullptr;
    UiScreen _screen = SCR_FACE;
    unsigned long _lastDrawMs = 0;

    int _qrCount = 0;
    int _qrIndex = 0;

    void drawTime();
    void drawWeather();
    void drawNotifications();
    void drawNavigation();
    void drawMusic();
    void drawPhone();
    void drawQr();

    void header(const char *title);
};

#endif // CHRONOS_UI_H