#ifndef AA_AUDIO_TRACK_H
#define AA_AUDIO_TRACK_H

#include <stdint.h>

typedef void (*AudioCB)(void*);

class AudioTrack{
public:
    AudioTrack();
    virtual ~AudioTrack();
    virtual void start();
    virtual void pause();
    virtual void stop();
    virtual int32_t write(void *buf, int32_t sizeInBytes) = 0;
    void setAudioCallback(AudioCB audioCB, void *userData);

protected:
    virtual void release();
    AudioCB mAudioCB;
    void *mUserData;
};

#endif //AA_AUDIO_TRACK_H
