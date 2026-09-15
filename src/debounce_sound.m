#import "debounce_sound.h"

#import <AVFoundation/AVFoundation.h>
#import <math.h>
#import <pthread.h>
#import <stdbool.h>
#import <string.h>

enum {
    kMaxClickLevel = 17,
    kMaxQueuedSounds = 20,
    kToneFrames = 240, /* 5 ms at 48 kHz. */
    kGapFrames = 2400, /* 50 ms at 48 kHz. */
    kDraggedToneFrames = 9600 /* 200 ms at 48 kHz. */
};

static AVAudioEngine *engine;
static AVAudioPlayerNode *player;
static AVAudioPCMBuffer *tick;
static AVAudioPCMBuffer *dragged_tone;
static AVAudioPCMBuffer *sound_gap;
static AVAudioPCMBuffer *click_tones[kMaxClickLevel + 1];
static pthread_mutex_t sound_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static int queued_sound_count;

static const double kSampleRate = 48000.0;
static const double kTwoPi = 6.283185307179586;

static AVAudioPCMBuffer *make_buffer(AVAudioFormat *format, AVAudioFrameCount frames) {
    AVAudioPCMBuffer *buffer = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:format frameCapacity:frames];
    if (buffer == nil) return nil;
    buffer.frameLength = frames;
    return buffer;
}

static AVAudioPCMBuffer *make_tone(AVAudioFormat *format, double frequency) {
    AVAudioPCMBuffer *buffer = make_buffer(format, kToneFrames);
    if (buffer == nil) return nil;
    float *samples = buffer.floatChannelData[0];
    const double step = frequency / kSampleRate;
    double phase = 0.0;
    for (AVAudioFrameCount i = 0; i < kToneFrames; ++i) {
        samples[i] = phase < 0.5 ? 1.0f : -1.0f;
        phase += step;
        if (phase >= 1.0) phase -= 1.0;
    }
    return buffer;
}

static AVAudioPCMBuffer *make_silence(AVAudioFormat *format) {
    AVAudioPCMBuffer *buffer = make_buffer(format, kGapFrames);
    if (buffer == nil) return nil;
    memset(buffer.floatChannelData[0], 0, kGapFrames * sizeof(float));
    return buffer;
}

static AVAudioPCMBuffer *make_dragged_tone(AVAudioFormat *format) {
    AVAudioPCMBuffer *buffer = make_buffer(format, kDraggedToneFrames);
    if (buffer == nil) return nil;
    float *samples = buffer.floatChannelData[0];
    const float nonlinearity = 4.0f; /* 0 is linear; 5 is nearly square. */
    const float normalization = 0.69 * 1.0f / tanh(nonlinearity);
    double phase = 0.0;
    for (AVAudioFrameCount i = 0; i < kDraggedToneFrames; ++i) {
        double progress = (double)i / (double)(kDraggedToneFrames - 1);
        double frequency = 300.0 + 100.0 * progress;
        float y = (float)sin(kTwoPi * phase) * sin(.05 * kTwoPi * phase);
        samples[i] = tanh(nonlinearity * y) * normalization;
        phase += frequency / kSampleRate;
        if (phase >= 10.0) phase -= 20.0;
    }
    return buffer;
}

static double click_frequency(int64_t click_level) {
    return 345.0 * pow(1.2, (double)(click_level - 1));
}

static void sound_finished(void) {
    pthread_mutex_lock(&sound_queue_mutex);
    if (queued_sound_count > 0) --queued_sound_count;
    pthread_mutex_unlock(&sound_queue_mutex);
}

static void play_buffer(AVAudioPCMBuffer *buffer) {
    if (player == nil || buffer == nil) return;

    pthread_mutex_lock(&sound_queue_mutex);
    if (queued_sound_count >= kMaxQueuedSounds) {
        pthread_mutex_unlock(&sound_queue_mutex);
        return;
    }
    bool needs_gap = queued_sound_count > 0;
    ++queued_sound_count;
    pthread_mutex_unlock(&sound_queue_mutex);

    /* nil schedules after all previously accepted buffers, preserving FIFO order. */
    if (needs_gap && sound_gap != nil) {
        [player scheduleBuffer:sound_gap completionHandler:nil];
    }
    [player scheduleBuffer:buffer completionHandler:^{
        sound_finished();
    }];
    [player play];
}

static void debounce_sound_init(void) {
    if (engine != nil) return;

    AVAudioFormat *format = [[[AVAudioFormat alloc]
        initStandardFormatWithSampleRate:kSampleRate channels:1] autorelease];

    tick = make_tone(format, 1000.0);
    sound_gap = make_silence(format);
    dragged_tone = make_dragged_tone(format);
    for (int64_t click_level = 1; click_level <= kMaxClickLevel; ++click_level) {
        click_tones[click_level] = make_tone(format, click_frequency(click_level));
    }

    engine = [[AVAudioEngine alloc] init];
    player = [[AVAudioPlayerNode alloc] init];
    [engine attachNode:player];
    [engine connect:player to:engine.mainMixerNode format:format];

    NSError *error = nil;
    [engine startAndReturnError:&error]; // Start once so event-time playback avoids engine startup latency.
}

void debounce_sound_set_volume(double volume) {
    debounce_sound_init();
    player.volume = (float)volume;
}

void debounce_sound_play(void) {
    debounce_sound_init();
    play_buffer(tick);
}

void debounce_sound_play_click(int64_t click_state) {
    if (click_state <= 1) return;
    debounce_sound_init();
    if (click_state > kMaxClickLevel) click_state = kMaxClickLevel;
    play_buffer(click_tones[click_state]);
}

void debounce_sound_play_wheel(int64_t vertical) {
    int i = vertical + kMaxClickLevel / 2;
    debounce_sound_init();
    if (i < 1) i = 1;
    if (i > kMaxClickLevel) i = kMaxClickLevel;
    play_buffer(click_tones[i]);
}

void debounce_sound_play_dragged(void) {
    debounce_sound_init();
    play_buffer(dragged_tone);
}
