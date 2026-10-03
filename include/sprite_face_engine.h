#ifndef SPRITE_FACE_ENGINE_H
#define SPRITE_FACE_ENGINE_H

#include <Adafruit_SH110X.h>

#define SPRITE_EV_BLINK 0x01
#define SPRITE_EV_GLANCE 0x02
#define SPRITE_EV_MOOD 0x04

class SpriteFaceEngine
{
public:
    void begin(Adafruit_SH1106G *display);
    void update(bool bleConnected);

    void resetIdleTimers();

    uint8_t takeEvents()
    {
        uint8_t e = _events;
        _events = 0;
        return e;
    }

    int currentAnimSet() const { return _animSet; }

private:
    enum State
    {
        STATE_RESTING,
        STATE_BLINKING,
        STATE_GLANCING,
        STATE_PLAYING_ANIM
    };

    Adafruit_SH1106G *_display = nullptr;
    State _state = STATE_RESTING;
    bool _seeded = false;
    bool _everDrawn = false;

    unsigned long _lastFrameAt = 0;
    unsigned long _nextBlinkAt = 0;
    unsigned long _nextShowAt = 0;
    unsigned long _nextGlanceAt = 0;

    uint8_t _events = 0;
    int _glanceDirX = 0;
    int _glanceDirY = 0;
    int _glanceStep = 0;

    bool _secondBlinkPending = false;
    int _animSet = 0;
    int _animFrame = 0;

    void drawFrame(const unsigned char *bitmap, int dx = 0, int dy = 0);
    void drawConnIcon(bool connected);
    void scheduleNextBlink();
    void scheduleNextShow();
    void scheduleNextGlance();
};

#endif // SPRITE_FACE_ENGINE_H