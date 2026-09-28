#include "buzzer_engine.h"
#include "dirgamochi_config.h"

void BuzzerEngine::begin(AudioEngine *audio)
{
    _audio = audio;

#if ENABLE_BUZZER
    ledcSetup(BUZZER_LEDC_CHANNEL, 2000, BUZZER_LEDC_RES_BITS);
    ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    _hwReady = true;
#else
    _hwReady = false;
#endif
}

void BuzzerEngine::setEnabled(bool enabled)
{
    _enabled = enabled;
    if (!enabled)
        stop();
}

void BuzzerEngine::setVolume(uint8_t level)
{
    if (_audio)
        _audio->setVolume(level);
}

void BuzzerEngine::applyStep(const Step &s)
{
#if ENABLE_BUZZER
    if (_hwReady)
    {
        if (s.freqHz == 0)
            ledcWrite(BUZZER_LEDC_CHANNEL, 0);
        else
            ledcWriteTone(BUZZER_LEDC_CHANNEL, s.freqHz);
    }
#endif

#if ENABLE_SPEAKER_BEEP
    if (_audio)
    {
        if (s.freqHz == 0)
            _audio->stopTone();
        else
            _audio->startTone(s.freqHz);
    }
#endif

#if !ENABLE_BUZZER && !ENABLE_SPEAKER_BEEP
    (void)s;
#endif
}

void BuzzerEngine::loadPattern(const Step *steps, int count, bool loop)
{
    if (count > MAX_STEPS)
        count = MAX_STEPS;
    for (int i = 0; i < count; i++)
        _steps[i] = steps[i];
    _stepCount = count;
    _stepIndex = 0;
    _looping = loop;
    _playing = _enabled && count > 0;
    _stepStartedAt = millis();
    if (_playing)
        applyStep(_steps[0]);
}

void BuzzerEngine::playNotification()
{
    // two short rising chirps
    static const Step pattern[] = {
        {2400, 70}, {0, 50}, {3100, 90}};
    loadPattern(pattern, 3, false);
}

void BuzzerEngine::playNavigation()
{
    static const Step pattern[] = {
        {1800, 60}};
    loadPattern(pattern, 1, false);
}

void BuzzerEngine::playClick()
{
    static const Step pattern[] = {
        {2600, 25}, {0, 20}, {3400, 25}};
    loadPattern(pattern, 3, false);
}

void BuzzerEngine::playAlarm()
{
    // urgent repeating two-tone; loops until stop() is called
    static const Step pattern[] = {
        {2000, 200}, {0, 80}, {2600, 200}, {0, 200}};
    loadPattern(pattern, 4, true);
}

void BuzzerEngine::stop()
{
    _playing = false;
    _looping = false;
    _stepCount = 0;
    applyStep({0, 0});
}

void BuzzerEngine::loop()
{
    if (!_playing)
        return;

    unsigned long now = millis();
    if (now - _stepStartedAt < _steps[_stepIndex].durationMs)
        return;

    _stepIndex++;
    if (_stepIndex >= _stepCount)
    {
        if (_looping)
        {
            _stepIndex = 0;
        }
        else
        {
            stop();
            return;
        }
    }

    _stepStartedAt = now;
    applyStep(_steps[_stepIndex]);
}
