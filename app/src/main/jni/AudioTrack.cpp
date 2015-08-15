#include <stdlib.h>
#include "AudioTrack.h"

AudioTrack::AudioTrack():mAudioCB(NULL),mUserData(NULL){}

AudioTrack::~AudioTrack(){
    release();
}

void AudioTrack::start(){}

void AudioTrack::pause(){}

void AudioTrack::stop(){}

void AudioTrack::release(){
    mAudioCB  = NULL;
    mUserData = NULL;
}

/*int32_t AudioTrack::write(void *buf, int32_t offset, int32_t length){
     return 0;
 }*/

 void AudioTrack::setAudioCallback(AudioCB audioCB, void *userData){
     mAudioCB = audioCB;
     mUserData=userData;
 }
