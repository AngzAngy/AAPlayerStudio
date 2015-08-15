#ifndef AA_SLESAUDIO_TRACK_H
#define AA_SLESAUDIO_TRACK_H
// for native audio
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "AudioTrack.h"

class SLESAudioTrack : public AudioTrack{
public:
    static  SLuint32 convertSLESSamplerate(int samplerate);

    SLESAudioTrack(int samplerate, int bitsPerSample, int channel);
    virtual void start();
    virtual void pause();
    virtual void stop();
    virtual int32_t write(void *buf, int32_t offset, int32_t length);

protected:
    static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

    virtual void release();
    SLresult createEngine();
    void releaseEngine();
    SLresult createBufferQueueAudioPlayer(int samplerate, int bitsPerSample, int channel);
    void releaseBufferQueueAudioPlayer();

private:
    // engine interfaces
    SLObjectItf engineObject;
    SLEngineItf engineEngine;

    // output mix interfaces
    SLObjectItf outputMixObject;

    // buffer queue player interfaces
    SLObjectItf bqPlayerObject;
    SLPlayItf bqPlayerPlay;
    SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
    SLEffectSendItf bqPlayerEffectSend;
    SLMuteSoloItf bqPlayerMuteSolo;
    SLVolumeItf bqPlayerVolume;
};

#endif //AA_SLESAUDIO_TRACK_H
