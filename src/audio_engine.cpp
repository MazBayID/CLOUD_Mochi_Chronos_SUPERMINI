#include "audio_engine.h"
#include "dirgamochi_config.h"
#include <math.h>
#include <driver/i2s.h>

// The ESP32-C3 has a single I2S controller. The mic (INMP441) and the amp
// (MAX98357A) share BCLK/WS on this board.
static const i2s_port_t I2S_PORT = I2S_NUM_0;

static const int TONE_SAMPLE_RATE = 16000;
static const int TONE_CHUNK_SAMPLES = 128; // pushed per loop() tick, non-blocking

#if ENABLE_AUDIO
// Full duplex: mic (RX) + speaker (TX). Only used once the voice pipeline
// itself is enabled and wired/tested.
static void configureI2SFullDuplex()
{
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX);
    cfg.sample_rate = TONE_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = MIC_SCK_PIN;   // == SPK_BCLK_PIN
    pins.ws_io_num = MIC_WS_PIN;     // == SPK_LRC_PIN
    pins.data_out_num = SPK_DIN_PIN; // to MAX98357A
    pins.data_in_num = MIC_SD_PIN;   // from INMP441

    i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
    i2s_set_pin(I2S_PORT, &pins);
}
#else
// Speaker-only (TX): the default. Deliberately does NOT touch the mic pin
// at all (data_in_num = I2S_PIN_NO_CHANGE), so this is safe to enable even
// on a board where the INMP441 isn't wired/tested yet - it only drives the
// existing MAX98357A amp so notification/nav/alarm beeps have somewhere
// to come out of.
static void configureI2STxOnly()
{
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = TONE_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = SPK_BCLK_PIN;
    pins.ws_io_num = SPK_LRC_PIN;
    pins.data_out_num = SPK_DIN_PIN; // to MAX98357A
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
    i2s_set_pin(I2S_PORT, &pins);
}
#endif

void AudioEngine::begin()
{
#if ENABLE_AUDIO
    configureI2SFullDuplex();
    _inited = true;
#else
    configureI2STxOnly();
    _inited = false; // full mic+speaker pipeline intentionally left off
#endif
    _speakerReady = true; // either path above gives us a working speaker TX
}

void AudioEngine::loop()
{
    if (!_speakerReady || !_toneActive)
        return;

    // Push one small chunk of a continuous sine wave, non-blocking (0 tick
    // timeout): if the DMA buffer is already full this call returns
    // immediately instead of stalling loop()/BLE, and we just try again
    // next tick. A few dropped samples at most is inaudible; a stalled
    // main loop while an alarm rings would not be.
    static int16_t buf[TONE_CHUNK_SAMPLES * 2]; // interleaved L/R
    const float twoPiOverRate = 2.0f * (float)PI / (float)TONE_SAMPLE_RATE;
    for (int i = 0; i < TONE_CHUNK_SAMPLES; i++)
    {
        int16_t s = (int16_t)((float)_toneAmplitude * sinf(_tonePhase));
        buf[i * 2] = s;
        buf[i * 2 + 1] = s;
        _tonePhase += twoPiOverRate * _toneFreqHz;
        if (_tonePhase > 2.0f * (float)PI)
            _tonePhase -= 2.0f * (float)PI;
    }

    size_t written = 0;
    i2s_write(I2S_PORT, buf, sizeof(buf), &written, 0); // 0 = don't block
}

bool AudioEngine::isEnabled() const
{
    return _inited;
}

void AudioEngine::startListening()
{
#if ENABLE_AUDIO
    // TODO: begin reading i2s_read() into a ring buffer for push-to-talk
#endif
}

void AudioEngine::stopListening()
{
#if ENABLE_AUDIO
    // TODO: stop reading, hand buffer off to whatever consumes it
#endif
}

void AudioEngine::playTone(uint16_t freqHz, uint16_t durationMs)
{
    // Kept as a blocking helper for the future voice/TTS milestone, where a
    // one-shot precise-duration tone is more useful than the continuous
    // startTone()/stopTone() pair BuzzerEngine drives. Not used for the
    // notification/nav/alarm beeps (see startTone()) so it never blocks
    // the main loop during normal operation.
    if (!_speakerReady)
        return;
    const int totalSamples = (TONE_SAMPLE_RATE * durationMs) / 1000;
    int16_t sample;
    size_t written;
    for (int i = 0; i < totalSamples; i++)
    {
        float t = (float)i / (float)TONE_SAMPLE_RATE;
        sample = (int16_t)((float)_toneAmplitude * sinf(2.0f * PI * freqHz * t));
        int16_t stereo[2] = {sample, sample};
        i2s_write(I2S_PORT, stereo, sizeof(stereo), &written, portMAX_DELAY);
    }
}

void AudioEngine::setVolume(uint8_t level)
{
    switch (level)
    {
    case 0:
        _toneAmplitude = BEEP_VOLUME_LOW;
        break;
    case 2:
        _toneAmplitude = BEEP_VOLUME_HIGH;
        break;
    default:
        _toneAmplitude = BEEP_VOLUME_MED;
        break;
    }
}

void AudioEngine::startTone(uint16_t freqHz)
{
    if (!_speakerReady)
        return;
    _toneFreqHz = (float)freqHz;
    _toneActive = true;
}

void AudioEngine::stopTone()
{
    _toneActive = false;
    if (_speakerReady)
        i2s_zero_dma_buffer(I2S_PORT); // clear any samples still queued, so it stops promptly
}
