#include "SLESAudioTrack.h"
#include "jnilogger.h"

SLuint32 SLESAudioTrack::convertSLESSamplerate(int samplerate){
    switch(samplerate) {
        case 8000:
            return SL_SAMPLINGRATE_8;
            break;
        case 11025:
            return SL_SAMPLINGRATE_11_025;
            break;
        case 16000:
            return SL_SAMPLINGRATE_16;
            break;
        case 22050:
            return SL_SAMPLINGRATE_22_05;
            break;
        case 24000:
            return SL_SAMPLINGRATE_24;
            break;
        case 32000:
            return SL_SAMPLINGRATE_32;
            break;
        case 44100:
            return SL_SAMPLINGRATE_44_1;
            break;
        case 48000:
            return SL_SAMPLINGRATE_48;
            break;
        case 64000:
            return SL_SAMPLINGRATE_64;
            break;
        case 88200:
            return SL_SAMPLINGRATE_88_2;
            break;
        case 96000:
            return SL_SAMPLINGRATE_96;
            break;
        case 192000:
            return SL_SAMPLINGRATE_192;
            break;
        default:
            return -1;
    }
}

// this callback handler is called every time a buffer finishes playing
void SLESAudioTrack::bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context){
    SLESAudioTrack * audioTrack = (SLESAudioTrack*)context;
    if(audioTrack && audioTrack->mAudioCB){
        audioTrack->mAudioCB(audioTrack->mUserData);
    }
}

SLESAudioTrack::SLESAudioTrack(int samplerate, int bitsPerSample, int channel):engineObject(NULL),engineEngine(NULL),
                                 outputMixObject(NULL),
                                 bqPlayerObject(NULL),bqPlayerPlay(NULL),
                                 bqPlayerBufferQueue(NULL),bqPlayerEffectSend(NULL),
                                 bqPlayerMuteSolo(NULL),bqPlayerVolume(NULL){
    if(SL_RESULT_SUCCESS == createEngine()){
        if(SL_RESULT_SUCCESS != createBufferQueueAudioPlayer(samplerate, bitsPerSample, channel)){
            releaseEngine();
        }
    }
}

void SLESAudioTrack::start(){
    // set the player's state to playing
    if(bqPlayerPlay) {
        (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
    }
}
void SLESAudioTrack::pause(){
    if(bqPlayerPlay) {
        (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PAUSED);
    }
}
void SLESAudioTrack::stop(){
    if(bqPlayerPlay) {
        (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
    }
}
int32_t SLESAudioTrack::write(void *buf, int32_t sizeInBytes){
    int8_t *buffer = ( int8_t *)buf;
    if(bqPlayerBufferQueue && buffer && sizeInBytes > 0){
        (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, buffer, sizeInBytes);
        return sizeInBytes;
    }else{
        return -1;
    }
}
void SLESAudioTrack::release(){
    releaseBufferQueueAudioPlayer();
    releaseEngine();
}

SLresult SLESAudioTrack::createEngine(){
    SLresult result;

    // create engine
    result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    if(SL_RESULT_SUCCESS != result){
        return result;
    }

    // realize the engine
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    if(SL_RESULT_SUCCESS != result){
        releaseEngine();
        return result;
    }

    // get the engine interface, which is needed in order to create other objects
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    if(SL_RESULT_SUCCESS != result){
        releaseEngine();
        return result;
    }

    // create output mix, with environmental reverb specified as a non-required interface
    const SLInterfaceID ids[1] = {SL_IID_ENVIRONMENTALREVERB};
    const SLboolean req[1] = {SL_BOOLEAN_FALSE};
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 1, ids, req);
    if(SL_RESULT_SUCCESS != result){
        releaseEngine();
        return result;
    }

    // realize the output mix
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    if(SL_RESULT_SUCCESS != result){
        releaseEngine();
        return result;
    }
    return result;
}

void SLESAudioTrack::releaseEngine(){
    // destroy output mix object, and invalidate all associated interfaces
    if (outputMixObject != NULL) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = NULL;
    }
    if (engineObject != NULL) {
        (*engineObject)->Destroy(engineObject);
        engineObject = NULL;
        engineEngine = NULL;
    }
}

SLresult SLESAudioTrack::createBufferQueueAudioPlayer(int samplerate, int bitsPerSample, int channel){
    SLresult result;

    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};

    SLDataFormat_PCM format_pcm;
    format_pcm.formatType = SL_DATAFORMAT_PCM;
    format_pcm.numChannels = channel;
    format_pcm.samplesPerSec = convertSLESSamplerate(samplerate);
    format_pcm.bitsPerSample = bitsPerSample;
    format_pcm.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
    if(channel==2){
        format_pcm.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    }else{
        format_pcm.channelMask = SL_SPEAKER_FRONT_CENTER;
    }
    format_pcm.endianness = SL_BYTEORDER_LITTLEENDIAN;

    SLDataSource audioSrc = {&loc_bufq, &format_pcm};

    // configure audio sink
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&loc_outmix, NULL};

    // create audio player
    const SLInterfaceID ids[3] = {SL_IID_BUFFERQUEUE, SL_IID_EFFECTSEND,
            /*SL_IID_MUTESOLO,*/ SL_IID_VOLUME};
    const SLboolean req[3] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE,
            /*SL_BOOLEAN_TRUE,*/ SL_BOOLEAN_TRUE};
    result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &audioSrc, &audioSnk,
                                                3, ids, req);

    // realize the player
    result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
    if(SL_RESULT_SUCCESS != result){
        releaseBufferQueueAudioPlayer();
    }

    // get the play interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);
    if(SL_RESULT_SUCCESS != result){
        releaseBufferQueueAudioPlayer();
    }

    // get the buffer queue interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE,
                                             &bqPlayerBufferQueue);
    if(SL_RESULT_SUCCESS != result){
        releaseBufferQueueAudioPlayer();
    }

    // register callback on the buffer queue
    result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, this);
    if(SL_RESULT_SUCCESS != result){
        releaseBufferQueueAudioPlayer();
    }

    // get the effect send interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_EFFECTSEND,
                                             &bqPlayerEffectSend);
    if(SL_RESULT_SUCCESS != result){
        releaseBufferQueueAudioPlayer();
    }

#if 0 // mute/solo is not supported for sources that are known to be mono, as this is
    // get the mute/solo interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_MUTESOLO, &bqPlayerMuteSolo);
        if(SL_RESULT_SUCCESS != result){
        releaseBufferQueueAudioPlayer();
    }
#endif

    // get the volume interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);
    if(SL_RESULT_SUCCESS != result){
        releaseBufferQueueAudioPlayer();
    }

//    // set the player's state to playing
//    result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
//    if(SL_RESULT_SUCCESS != result){
//        releaseBufferQueueAudioPlayer();
//    }

//    char tmp[8];
//    write(tmp, 0, 8);

    return result;
}

void SLESAudioTrack::releaseBufferQueueAudioPlayer(){
    // destroy buffer queue audio player object, and invalidate all associated interfaces
    if (bqPlayerObject != NULL) {
        (*bqPlayerObject)->Destroy(bqPlayerObject);
        bqPlayerObject = NULL;
        bqPlayerPlay = NULL;
        bqPlayerBufferQueue = NULL;
        bqPlayerEffectSend = NULL;
        bqPlayerMuteSolo = NULL;
        bqPlayerVolume = NULL;
    }
}