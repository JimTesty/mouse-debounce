#import "debounce_sound.h"

#import "monotonic_clock.h"
#import <AVFoundation/AVFoundation.h>

static AVAudioEngine *engine;
static AVAudioPlayerNode *player;
static AVAudioPCMBuffer *tick;
static uint64_t busy_until_ns;

static void debounce_sound_init(void) {
    if (engine != nil) return;

    const double sample_rate = 48000.0;
    const AVAudioFrameCount frames = (AVAudioFrameCount)(sample_rate * 0.005);
    AVAudioFormat *format = [[[AVAudioFormat alloc]
        initStandardFormatWithSampleRate:sample_rate channels:1] autorelease];

    tick = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:frames];
    tick.frameLength = frames;
    float *samples = tick.floatChannelData[0];
    for (AVAudioFrameCount i = 0; i < frames; ++i) {
        samples[i] = ((i / 24) & 1) ? -1.0f : 1.0f; // 1 kHz, exactly 5 ms
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
    if (player == nil) return;

    uint64_t now_ns = monotonic_now_ns();
    if (now_ns < busy_until_ns) return;
    busy_until_ns = now_ns + UINT64_C(5000000);

    [player scheduleBuffer:tick completionHandler:nil];
    if (!player.isPlaying) [player play];
}
