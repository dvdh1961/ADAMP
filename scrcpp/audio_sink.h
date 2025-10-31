#ifndef AUDIO_SINK_H
#define AUDIO_SINK_H

#include <cstdint>

class IAudioSink {
public:
    virtual ~IAudioSink() {}

    // framesStereo = aantal stereo frames in buffer
    // buffer = interleaved L,R met 16-bit signed samples
    virtual void pushAudioFromEmu(const int16_t* buffer, int framesStereo) = 0;
};

#endif // AUDIO_SINK_H
