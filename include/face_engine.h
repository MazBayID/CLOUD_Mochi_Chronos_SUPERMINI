#ifndef FACE_ENGINE_H
#define FACE_ENGINE_H

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

enum FaceExpression
{
    FACE_NORMAL = 0,
    FACE_HAPPY,
    FACE_SLEEPY,
    FACE_SURPRISED,
    FACE_ANGRY,
    FACE_CUTE
};

class FaceEngine
{
public:
    void begin(Adafruit_SH1106G *display);

    void update(bool bleConnected);

    void setExpression(FaceExpression expr);
    FaceExpression getExpression() const { return _expr; }

    void lookAt(float xOffset, float yOffset);

    void setIdleAnimationEnabled(bool enabled) { _idleAnimEnabled = enabled; }

private:
    Adafruit_SH1106G *_display = nullptr;
    FaceExpression _expr = FACE_NORMAL;

    unsigned long _nextBlinkAt = 0;
    unsigned long _blinkStartedAt = 0;
    unsigned long _blinkDurationMs = 180;
    bool _blinking = false;
    bool _doubleBlinkPending = false;

    float _lookX = 0.0f;
    float _lookY = 0.0f;
    float _targetLookX = 0.0f;
    float _targetLookY = 0.0f;
    bool _idleAnimEnabled = true;
    unsigned long _nextIdleLookAt = 0;

    unsigned long _lastDrawMs = 0;
    bool _seeded = false;

    void draw();
    void drawEye(int cx, int cy, int r, float openness, FaceExpression expr, bool leftEye);
    void drawMouth(FaceExpression expr);
    void drawConnIcon(bool connected);
    void scheduleNextBlink();
    void updateIdleLook(unsigned long now);
    static float easeInOut(float t);

    static void fillEllipse(Adafruit_SH1106G *d, int cx, int cy, int rx, int ry, uint16_t color);
    static void drawArc(Adafruit_SH1106G *d, int cx, int cy, int r, float startDeg, float endDeg, uint16_t color, int thickness = 2);
};

#endif // FACE_ENGINE_H