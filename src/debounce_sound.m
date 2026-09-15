#import "debounce_sound.h"

#import "monotonic_clock.h"
#import <AVFoundation/AVFoundation.h>
#import <math.h>

static AVAudioEngine *engine;
static AVAudioPlayerNode *player;
static AVAudioPCMBuffer *tick;
static AVAudioPCMBuffer *dragged_tone;
static NSMutableDictionary *click_tones;
static uint64_t busy_until_ns;

static const double kSampleRate = 48000.0;
static const AVAudioFrameCount kToneFrames = 240; /* 5 ms at 48 kHz. */
static const AVAudioFrameCount kDraggedToneFrames = 9600; /* 200 ms at 48 kHz. */

static AVAudioPCMBuffer *make_tone(AVAudioFormat *format, double frequency) {
    AVAudioPCMBuffer *buffer = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:format frameCapacity:kToneFrames];
    if (buffer == nil) return nil;
    buffer.frameLength = kToneFrames;
    float *samples = buffer.floatChannelData[0];
    const double step = frequency / kSampleRate;
    double phase = 0.0;
    for (AVAudioFrameCount i = 0; i < kToneFrames; ++i) {
        samples[i] = phase < 0.5 ? 1.0f : -1.0f;
        phase += step;
        if (phase >= 1.0) phase -= floor(phase);
    }
    return buffer;
}

static AVAudioPCMBuffer *make_dragged_tone(AVAudioFormat *format) {
    AVAudioPCMBuffer *buffer = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:format frameCapacity:kDraggedToneFrames];
    if (buffer == nil) return nil;
    buffer.frameLength = kDraggedToneFrames;
    float *samples = buffer.floatChannelData[0];
    double phase = 0.0;
    for (AVAudioFrameCount i = 0; i < kDraggedToneFrames; ++i) {
        double progress = (double)i / (double)(kDraggedToneFrames - 1);
        double frequency = 400.0 + 200.0 * progress;
        const float nonlinearity = 5;   // 0 for linear, 5 for almost square wave
        float y = (float)sin(6.283185307179586 * phase);
        samples[i] = tanh(nonlinearity * y) / tanh(nonlinearity);
        phase += frequency / kSampleRate;
        if (phase >= 1.0) phase -= floor(phase);
    }
    return buffer;
}

static void debounce_sound_init(void) {
    if (engine != nil) return;

    AVAudioFormat *format = [[[AVAudioFormat alloc]
        initStandardFormatWithSampleRate:kSampleRate channels:1] autorelease];

    tick = make_tone(format, 1000.0);
    click_tones = [[NSMutableDictionary alloc] init];

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
    if (player == nil) return;

    uint64_t now_ns = monotonic_now_ns();
    if (now_ns < busy_until_ns) return;
    busy_until_ns = now_ns + UINT64_C(5000000);

    [player scheduleBuffer:tick completionHandler:nil];
    if (!player.isPlaying) [player play];
}

void debounce_sound_play_click(int64_t click_state) {
    if (click_state <= 1) return;
    debounce_sound_init();
    if (player == nil || tick == nil) return;

    uint64_t now_ns = monotonic_now_ns();
    if (now_ns < busy_until_ns) return;

    double frequency = 345.0 * pow(1.2, (double)(click_state - 1));
    if (!isfinite(frequency) || frequency <= 0.0) return;
    NSNumber *key = [[NSNumber alloc] initWithLongLong:click_state];
    AVAudioPCMBuffer *tone = [click_tones objectForKey:key];
    if (tone == nil) {
        tone = make_tone(tick.format, frequency);
        if (tone != nil) {
            [click_tones setObject:tone forKey:key];
            [tone release];
            tone = [click_tones objectForKey:key];
        }
    }
    [key release];
    if (tone == nil) return;

    busy_until_ns = now_ns + UINT64_C(5000000);
    [player scheduleBuffer:tone completionHandler:nil];
    if (!player.isPlaying) [player play];
}

void debounce_sound_play_dragged(void) {
    debounce_sound_init();
    if (player == nil || tick == nil) return;

    uint64_t now_ns = monotonic_now_ns();
    if (now_ns < busy_until_ns) return;

    if (dragged_tone == nil) dragged_tone = make_dragged_tone(tick.format);
    AVAudioPCMBuffer *tone = dragged_tone;
    if (tone == nil) return;
    busy_until_ns = now_ns + UINT64_C(200000000);
    [player scheduleBuffer:tone completionHandler:nil];
    if (!player.isPlaying) [player play];
}
