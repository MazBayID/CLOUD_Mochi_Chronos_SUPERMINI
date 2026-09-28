#ifndef SPRITE_FACE_ENGINE_H
#define SPRITE_FACE_ENGINE_H

#include <Adafruit_SSD1306.h>

// Plays the bitmap "idle mood" animations converted at build time from
// assets/faces/mochi_*.zip by tools/generate_faces.py (see
// include/generated/face_frames.h). This only ever owns the OLED during
// the *plain* idle state - main.cpp keeps every named expression (Happy on
// BLE connect, Surprised on notification/alarm, Sleepy on quiet-hours/idle
// timeout, Cute on nav/music, the remote-touch "petting" reaction, ...) on
// the existing procedural FaceEngine, completely unchanged. The two engines
// take turns owning the display; only one is ever updated on a given tick.
//
// Behaviour, in order of priority while idle:
//   1. Rest on a static neutral frame (cheap - no redraw while nothing
//      changes, so it doesn't compete with BLE/button handling for I2C
//      time or CPU).
//   2. Every few seconds, blink (one real "eyes closed" frame from the mood
//      loops, FACE_BLINK_* in the generated header), sometimes twice in a
//      row like a real double-blink - snappy, not the full ~17s loop.
//   3. Every so often (much rarer), play one full, randomly chosen ~17s
//      mood animation as a little surprise, then return to resting.
// All timing is non-blocking (millis()-driven) - nothing here ever calls
// delay(), matching the rest of the firmware's loop() style.
class SpriteFaceEngine
{
public:
    void begin(Adafruit_SSD1306 *display);
    void update(bool bleConnected); // call every loop() tick while idle

    // Call once when (re-)entering the plain idle state (e.g. after a
    // notification/nav/petting episode ends), so a blink/show that became
    // "due" while this engine wasn't being ticked doesn't fire the instant
    // it regains the screen.
    void resetIdleTimers();

private:
    enum State
    {
        STATE_RESTING,
        STATE_BLINKING,
        STATE_PLAYING_ANIM
    };

    Adafruit_SSD1306 *_display = nullptr;
    State _state = STATE_RESTING;
    bool _seeded = false;
    bool _everDrawn = false;

    unsigned long _lastFrameAt = 0;
    unsigned long _nextBlinkAt = 0;
    unsigned long _nextShowAt = 0;

    bool _secondBlinkPending = false; // a double-blink's follow-up is queued
    int _animSet = 0;
    int _animFrame = 0;

    void drawFrame(const unsigned char *bitmap);
    void drawConnIcon(bool connected);
    void scheduleNextBlink();
    void scheduleNextShow();
};

#endif // SPRITE_FACE_ENGINE_H
