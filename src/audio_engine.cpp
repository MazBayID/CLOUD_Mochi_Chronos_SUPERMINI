#include "audio_engine.h"
#include "dirgamochi_config.h"
#include <math.h>
#include <driver/i2s.h>

// The ESP32-C3 has a single I2S controller. The mic (INMP441) and the amp
// (MAX98357A) share BCLK/WS on this board.
static const i2s_port_t I2S_PORT = I2S_NUM_0;

static const int TONE_SAMPLE_RATE = 16000; // matches VOICE_SAMPLE_RATE - same I2S clock serves both
static const int TONE_CHUNK_SAMPLES = 128; // pushed per loop() tick, non-blocking

// ---- IMA ADPCM (WAV format tag 0x11) decode tables -------------------
// Standard Interactive Multimedia Association tables; verified byte-exact
// against a reference decoder (Python's audioop.adpcm2lin) while building
// the voice asset pipeline. Nibble order within each byte is high nibble
// first, then low nibble (also verified against the reference decoder -
// this is NOT the more commonly assumed "low nibble first").
static const int8_t ADPCM_INDEX_TABLE[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const int16_t ADPCM_STEP_TABLE[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

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
    if (!_speakerReady)
        return;

    if (_toneActive)
    {
        // Push one small chunk of a continuous sine wave, non-blocking (0
        // tick timeout): if the DMA buffer is already full this call
        // returns immediately instead of stalling loop()/BLE, and we just
        // try again next tick. A few dropped samples at most is inaudible;
        // a stalled main loop while an alarm rings would not be.
        static int16_t buf[TONE_CHUNK_SAMPLES * 2]; // interleaved L/R
        const float twoPiOverRate = 2.0f * (float)PI / (float)TONE_SAMPLE_RATE;
        for (int i = 0; i < TONE_CHUNK_SAMPLES; i++)
        {
            int16_t s = (int16_t)((float)(_toneAmplitude * _toneGain / 100) * sinf(_tonePhase));
            buf[i * 2] = s;
            buf[i * 2 + 1] = s;
            _tonePhase += twoPiOverRate * _toneFreqHz;
            if (_tonePhase > 2.0f * (float)PI)
                _tonePhase -= 2.0f * (float)PI;
        }

        size_t written = 0;
        i2s_write(I2S_PORT, buf, sizeof(buf), &written, 0); // 0 = don't block
        return;
    }

    if (_adpcmActive)
    {
        // Same non-blocking chunking approach as the tone path above, just
        // fed from the ADPCM decoder instead of a sine generator.
        static int16_t buf[TONE_CHUNK_SAMPLES * 2];
        int produced = 0;
        while (produced < TONE_CHUNK_SAMPLES && _adpcmPos < _adpcmLen)
        {
            int16_t raw = adpcmDecodeNextSample();
            // Scale by the same amplitude the tone generator uses, so
            // voice clips track the Volume menu setting consistently
            // instead of always playing at full scale.
            int16_t s = (int16_t)(((int32_t)raw * _toneAmplitude) / 32767);
            buf[produced * 2] = s;
            buf[produced * 2 + 1] = s;
            produced++;
        }

        if (produced > 0)
        {
            size_t written = 0;
            i2s_write(I2S_PORT, buf, produced * 2 * sizeof(int16_t), &written, 0);
        }

        if (_adpcmPos >= _adpcmLen)
            _adpcmActive = false; // clip finished
        return;
    }
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

void AudioEngine::startTone(uint16_t freqHz, uint8_t gainPercent)
{
    if (!_speakerReady)
        return;
    _adpcmActive = false; // tone and voice never play at once
    _toneGain = gainPercent > 100 ? 100 : gainPercent;
    _toneFreqHz = (float)freqHz;
    _toneActive = true;
}

void AudioEngine::stopTone()
{
    _toneActive = false;
    if (_speakerReady)
        i2s_zero_dma_buffer(I2S_PORT); // clear any samples still queued, so it stops promptly
}

// ---- IMA ADPCM streaming playback --------------------------------------

void AudioEngine::playADPCM(const unsigned char *data, uint32_t len, uint16_t blockAlign)
{
    if (!_speakerReady || !data || len < 4)
        return;
    _toneActive = false; // tone and voice never play at once
    _adpcmData = data;
    _adpcmLen = len;
    _adpcmBlockAlign = blockAlign;
    _adpcmPos = 0;
    _adpcmHighNibbleDone = false;
    _adpcmActive = true;
}

void AudioEngine::stopADPCM()
{
    _adpcmActive = false;
    if (_speakerReady)
        i2s_zero_dma_buffer(I2S_PORT);
}

// Decodes exactly one PCM sample from the current stream position and
// advances it. Every WAV IMA ADPCM block starts with a 4-byte header (an
// int16 initial predictor, a step-index byte, a reserved byte) which IS
// itself the block's first sample, followed by nibble-packed samples (high
// nibble of each byte first, then low - see the note by ADPCM_INDEX_TABLE).
// PROGMEM data is directly readable on ESP32 (unlike AVR), so this is a
// plain pointer dereference - no pgm_read_byte() needed.
int16_t AudioEngine::adpcmDecodeNextSample()
{
    uint32_t blockStart = (_adpcmPos / _adpcmBlockAlign) * _adpcmBlockAlign;
    uint32_t offsetInBlock = _adpcmPos - blockStart;

    if (offsetInBlock == 0)
    {
        uint8_t lo = _adpcmData[_adpcmPos];
        uint8_t hi = _adpcmData[_adpcmPos + 1];
        _adpcmPredictor = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
        _adpcmStepIndex = (int8_t)_adpcmData[_adpcmPos + 2];
        if (_adpcmStepIndex < 0)
            _adpcmStepIndex = 0;
        if (_adpcmStepIndex > 88)
            _adpcmStepIndex = 88;
        _adpcmPos += 4; // header is 4 bytes: predictor lo/hi, step index, reserved
        _adpcmHighNibbleDone = false;
        return _adpcmPredictor;
    }

    uint8_t byteVal = _adpcmData[_adpcmPos];
    uint8_t nibble;
    if (!_adpcmHighNibbleDone)
    {
        nibble = (byteVal >> 4) & 0x0F;
        _adpcmHighNibbleDone = true;
    }
    else
    {
        nibble = byteVal & 0x0F;
        _adpcmHighNibbleDone = false;
        _adpcmPos++;
    }

    int step = ADPCM_STEP_TABLE[_adpcmStepIndex];
    int diff = step >> 3;
    if (nibble & 4)
        diff += step;
    if (nibble & 2)
        diff += step >> 1;
    if (nibble & 1)
        diff += step >> 2;

    int32_t predictor = _adpcmPredictor;
    if (nibble & 8)
        predictor -= diff;
    else
        predictor += diff;
    if (predictor > 32767)
        predictor = 32767;
    else if (predictor < -32768)
        predictor = -32768;
    _adpcmPredictor = (int16_t)predictor;

    int stepIndex = _adpcmStepIndex + ADPCM_INDEX_TABLE[nibble];
    if (stepIndex < 0)
        stepIndex = 0;
    else if (stepIndex > 88)
        stepIndex = 88;
    _adpcmStepIndex = (int8_t)stepIndex;

    return _adpcmPredictor;
}
