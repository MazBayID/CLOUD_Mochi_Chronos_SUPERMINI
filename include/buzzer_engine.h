#ifndef BUZZER_ENGINE_H
#define BUZZER_ENGINE_H

#include <Arduino.h>
#include "audio_engine.h"

// Non-blocking sequencer for short beep patterns (notification/nav/alarm/
// menu-click). Renders each pattern to whichever sound output is actually
// wired on the board:
//   - ENABLE_SPEAKER_BEEP (default ON): the existing MAX98357A speaker,
//     via AudioEngine's non-blocking I2S tone (no separate hardware
//     needed - this is the path most boards built from the reference
//     pinout actually have).
//   - ENABLE_BUZZER (default OFF): an optional separate passive piezo on
//     BUZZER_PIN, for boards that have one wired in addition.
// Both can be enabled at once; play*() is a no-op wherever the
// corresponding flag is off, so this never assumes hardware that isn't
// there. Nothing here ever calls delay().
class BuzzerEngine
{
public:
    void begin(AudioEngine *audio = nullptr);
    void loop(); // call every loop(); advances any in-progress pattern

    void setEnabled(bool enabled); // master on/off (from settings menu)
    bool isEnabled() const { return _enabled; }

    // 0=low, 1=med, 2=high - forwarded to AudioEngine (the speaker path);
    // the optional GPIO3 piezo has no meaningful volume control, so this
    // only affects ENABLE_SPEAKER_BEEP output.
    void setVolume(uint8_t level);

    // short chirp for an incoming notification
    void playNotification();
    // single short beep, e.g. a new turn/navigation update
    void playNavigation();
    // two quick low-high beeps, e.g. entering/selecting a menu item
    void playClick();
    // repeating urgent pattern, e.g. an active alarm - call stop() to silence
    void playAlarm();
    void stop();

private:
    struct Step
    {
        uint16_t freqHz; // 0 = silence
        uint16_t durationMs;
    };

    static const int MAX_STEPS = 8;
    Step _steps[MAX_STEPS];
    int _stepCount = 0;
    int _stepIndex = 0;
    unsigned long _stepStartedAt = 0;
    bool _playing = false;
    bool _looping = false; // true only for playAlarm()

    bool _enabled = true;
    bool _hwReady = false;
    AudioEngine *_audio = nullptr;

    void loadPattern(const Step *steps, int count, bool loop);
    void applyStep(const Step &s);
};

#endif // BUZZER_ENGINE_H
