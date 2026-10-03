#include "sprite_face_engine.h"
#include "dirgamochi_config.h"
#include "generated/face_frames.h"
#include <esp_system.h>

static const int GLANCE_SCALE_PCT[] = {50, 100, 100, 100, 50};
static const int GLANCE_STEP_COUNT = sizeof(GLANCE_SCALE_PCT) / sizeof(GLANCE_SCALE_PCT[0]);

void SpriteFaceEngine::begin(Adafruit_SH1106G *display)
{
    _display = display;
    if (!_seeded)
    {
        randomSeed(esp_random());
        _seeded = true;
    }
    resetIdleTimers();
}

void SpriteFaceEngine::scheduleNextBlink()
{
    _nextBlinkAt = millis() + (unsigned long)random(SPRITE_BLINK_INTERVAL_MIN_MS,
                                                    SPRITE_BLINK_INTERVAL_MAX_MS);
}

void SpriteFaceEngine::scheduleNextShow()
{
    _nextShowAt = millis() + (unsigned long)random(SPRITE_SHOW_INTERVAL_MIN_MS,
                                                   SPRITE_SHOW_INTERVAL_MAX_MS);
}

void SpriteFaceEngine::scheduleNextGlance()
{
    _nextGlanceAt = millis() + (unsigned long)random(SPRITE_GLANCE_INTERVAL_MIN_MS,
                                                     SPRITE_GLANCE_INTERVAL_MAX_MS);
}

void SpriteFaceEngine::resetIdleTimers()
{
    _state = STATE_RESTING;
    _secondBlinkPending = false;
    _events = 0;
    scheduleNextGlance();
    _everDrawn = false;
    scheduleNextBlink();
    scheduleNextShow();
}

void SpriteFaceEngine::drawFrame(const unsigned char *bitmap, int dx, int dy)
{
    _display->clearDisplay();
    _display->drawBitmap(SPRITE_X_OFFSET + dx, SPRITE_Y_OFFSET + dy, bitmap,
                         FACE_FRAME_W, FACE_FRAME_H, SH110X_WHITE);
}

void SpriteFaceEngine::drawConnIcon(bool connected)
{
    int cx = 122;
    int cy = 4;
    if (connected)
        _display->fillCircle(cx, cy, 3, SH110X_WHITE);
    else
        _display->drawCircle(cx, cy, 3, SH110X_WHITE);
}

void SpriteFaceEngine::update(bool bleConnected)
{
    if (!_display)
        return;

    unsigned long now = millis();
    bool changed = !_everDrawn;

    switch (_state)
    {
    case STATE_RESTING:
        if (now >= _nextShowAt)
        {
            _animSet = random(0, FACE_ANIM_COUNT);
            _animFrame = 0;
            _secondBlinkPending = false;
            _lastFrameAt = now;
            _state = STATE_PLAYING_ANIM;
            _events |= SPRITE_EV_MOOD;
            changed = true;
        }
        else if (now >= _nextBlinkAt)
        {
            _lastFrameAt = now;
            _state = STATE_BLINKING;
            if (!_secondBlinkPending)
                _events |= SPRITE_EV_BLINK;
            changed = true;
        }
        else if (now >= _nextGlanceAt)
        {
            _glanceDirX = 0;
            _glanceDirY = 0;
            switch (random(0, 4))
            {
            case 0: _glanceDirX = -1; break;
            case 1: _glanceDirX = 1; break;
            case 2: _glanceDirY = -1; break;
            default: _glanceDirY = 1; break;
            }
            _glanceStep = 0;
            _lastFrameAt = now;
            _state = STATE_GLANCING;
            _events |= SPRITE_EV_GLANCE;
            changed = true;
        }
        break;

    case STATE_GLANCING:
        if (now - _lastFrameAt >= SPRITE_FRAME_INTERVAL_MS)
        {
            _lastFrameAt = now;
            _glanceStep++;
            if (_glanceStep >= GLANCE_STEP_COUNT)
            {
                _state = STATE_RESTING;
                scheduleNextGlance();
            }
            changed = true;
        }
        break;

    case STATE_BLINKING:
        if (now - _lastFrameAt >= SPRITE_BLINK_HOLD_MS)
        {
            _state = STATE_RESTING;
            if (_secondBlinkPending)
            {
                _secondBlinkPending = false;
                scheduleNextBlink();
            }
            else if (random(0, 100) < SPRITE_DOUBLE_BLINK_PERCENT)
            {
                _secondBlinkPending = true;
                _nextBlinkAt = now + SPRITE_DOUBLE_BLINK_GAP_MS;
            }
            else
            {
                scheduleNextBlink();
            }
            changed = true;
        }
        break;

    case STATE_PLAYING_ANIM:
        if (now - _lastFrameAt >= SPRITE_FRAME_INTERVAL_MS)
        {
            _lastFrameAt = now;
            _animFrame++;
            if (_animFrame >= (int)faceAnimFrameCount[_animSet])
            {
                _state = STATE_RESTING;
                scheduleNextBlink();
                scheduleNextShow();
                scheduleNextGlance();
            }
            changed = true;
        }
        break;
    }

    if (!changed)
        return;

    const unsigned char *bmp;
    int ox = 0, oy = 0;
    switch (_state)
    {
    case STATE_GLANCING:
        ox = _glanceDirX * SPRITE_GLANCE_X * GLANCE_SCALE_PCT[_glanceStep] / 100;
        oy = _glanceDirY * SPRITE_GLANCE_Y * GLANCE_SCALE_PCT[_glanceStep] / 100;
        bmp = faceAnimFrames[0][0];
        break;
    case STATE_BLINKING:
        bmp = faceAnimFrames[FACE_BLINK_SET][FACE_BLINK_FRAME];
        break;
    case STATE_PLAYING_ANIM:
        bmp = faceAnimFrames[_animSet][_animFrame];
        break;
    case STATE_RESTING:
    default:
        bmp = faceAnimFrames[0][0];
        break;
    }

    drawFrame(bmp, ox, oy);
    drawConnIcon(bleConnected);
    _display->display();
    _everDrawn = true;
}
