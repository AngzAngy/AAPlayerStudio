/*
 * mediaplayer.cpp
 * zhangshiyang1234
 */

//#define LOG_NDEBUG 0
#define TAG "FFMpegMediaPlayer"

//#include <sys/types.h>
//#include <sys/time.h>
//#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

#include "gles/include/SelfDef.h"
#include "gles/include/GLYUV420PRender.h"

extern "C" {
	
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/log.h"
#include "libavutil/pixdesc.h"
	
} // end of extern C

#include <android/log.h>

#include "mediaplayer.h"
#include "SLESAudioTrack.h"
#include "jnilogger.h"
#include "image-util.h"
#include "SelfDef.h"

#define FPS_DEBUGGING true

//#define DUM_A

MediaPlayer::MediaPlayer():mListener(NULL),mCookie(NULL),
                           mDuration(-1),mRender(NULL),mCurrentPosition(-1),
                           mSeekPosition(-1),mCurrentState(MEDIA_PLAYER_IDLE),
                           mPrepareSync(false),mPrepareStatus(NO_ERROR),
                           mLoop(false),mLeftVolume(1.0),mRightVolume(1.0),
                           mVideoWidth(0),mVideoHeight(0),mAVFormatCtx(NULL),
                           mVideoConvertCtx(NULL),mAudioSwrCtx(NULL),mDecoderAudio(NULL),
                           mVideoStreamIndex(-1),mAudioStreamIndex(-1),mRawAudioBuf(NULL),
                           mAudioQueue(NULL),mVideoQueue(NULL),mAudioFrame(NULL),
                           mVideoFrame(NULL),mAudioTrack(NULL){
    av_register_all();
}

MediaPlayer::~MediaPlayer() {
    deleteC(mRender);
}

void MediaPlayer::onSurfaceCreated(){

}
void MediaPlayer::onSurfaceChanged(int width, int height){
    mRender->createGLProgram(YUV420P_VS, YUV420P_FS);
}
status_t MediaPlayer::prepareAudio()
{
	LOGI("prepareAudio");
	mAudioStreamIndex = -1;
	AVDictionary *optionsDict = NULL;
	for (int i = 0; i < mAVFormatCtx->nb_streams; i++) {
		if (mAVFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			mAudioStreamIndex = i;
			break;
		}
	}
	
	if (mAudioStreamIndex == -1) {
		return INVALID_OPERATION;
	}

	AVStream* stream = mAVFormatCtx->streams[mAudioStreamIndex];
	// Get a pointer to the codec context for the video stream
	AVCodecContext* codec_ctx = stream->codec;
	AVCodec* codec = avcodec_find_decoder(codec_ctx->codec_id);
	if (codec == NULL) {
		return INVALID_OPERATION;
	}
	
	// Open codec
	if (avcodec_open2(codec_ctx, codec, &optionsDict) < 0) {
		return INVALID_OPERATION;
	}
	return NO_ERROR;
}

status_t MediaPlayer::prepareVideo()
{
	LOGI("prepareVideo");
	// Find the first video stream
	mVideoStreamIndex = -1;
	AVDictionary *optionsDict = NULL;
	for (int i = 0; i < mAVFormatCtx->nb_streams; i++) {
		if (mAVFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			mVideoStreamIndex = i;
			LOGI("prepareVideo,index: %d",mVideoStreamIndex);
			break;
		}
	}
	
	if (mVideoStreamIndex == -1) {
		return INVALID_OPERATION;
	}
	
	AVStream* stream = mAVFormatCtx->streams[mVideoStreamIndex];
	// Get a pointer to the codec context for the video stream
	AVCodecContext* codec_ctx = stream->codec;
	AVCodec* codec = avcodec_find_decoder(codec_ctx->codec_id);
	if (codec == NULL) {
	   LOGE("prepareVideo finddecoder fail");
		return INVALID_OPERATION;
	}
	
	// Open codec
	if (avcodec_open2(codec_ctx, codec, &optionsDict) < 0) {
	    LOGE("prepareVideo open fail");
		return INVALID_OPERATION;
	}
    /*
     *mult thread decode video
     */
    codec_ctx->active_thread_type |= FF_THREAD_FRAME;
    codec->capabilities |= CODEC_CAP_DELAY;

	mVideoWidth = codec_ctx->width;
	mVideoHeight = codec_ctx->height;
	mDuration =  mAVFormatCtx->duration;
	
	LOGI("prepareVideo vSize(%dx%d)",mVideoWidth,mVideoHeight);

	return NO_ERROR;
}

status_t MediaPlayer::prepare() {
    LOGI("prepare");
	status_t ret;
	mCurrentState = MEDIA_PLAYER_PREPARING;
	av_log_set_callback(ffmpegNotify);
	if ((ret = prepareVideo()) != NO_ERROR) {
		mCurrentState = MEDIA_PLAYER_STATE_ERROR;
		return ret;
	}
	if ((ret = prepareAudio()) != NO_ERROR) {
		mCurrentState = MEDIA_PLAYER_STATE_ERROR;
		return ret;
	}
	mCurrentState = MEDIA_PLAYER_PREPARED;
    if(mListener){
        mListener->notify((int)MEDIA_PLAYER_PREPARED, 0 , 0);
    }
	return NO_ERROR;
}

status_t MediaPlayer::setListener(MediaPlayerListener* listener)
{
    LOGI("setListener");
    mListener = listener;
    return NO_ERROR;
}

status_t MediaPlayer::setDataSource(const char *fn)
{
    LOGI("setDataSource == %s", fn);

    status_t err = BAD_VALUE;
    AVIOContext  *io_context=NULL;
    if (avio_open(&io_context, fn, 0)<0){
        LOGE("Unable to open I/O %s", fn);
      return INVALID_OPERATION;
    }

    // Open video file
    if(avformat_open_input(&mAVFormatCtx, fn, NULL, NULL)!=0){
        LOGE("Unable to open input %s", fn);
      return INVALID_OPERATION;  // Couldn't open file
    }

    // Retrieve stream information
    if(avformat_find_stream_info(mAVFormatCtx, NULL)<0){
        LOGE("Unable to find stream %s", fn);
      return INVALID_OPERATION; // Couldn't find stream information
    }

    // Dump information about file onto standard error
    av_dump_format(mAVFormatCtx, 0, fn, 0);

	mCurrentState = MEDIA_PLAYER_INITIALIZED;
    return NO_ERROR;
}

void* MediaPlayer::startPlayer(void* ptr)
{
    LOGI("main player thread");
    MediaPlayer *pPlayer = (MediaPlayer*)ptr;
    pPlayer->doDecode(ptr);
}
status_t MediaPlayer::start()
{
//    if (mCurrentState != MEDIA_PLAYER_PREPARED) {
//        __android_log_print(ANDROID_LOG_ERROR, TAG, "starting player thread error state :%d",mCurrentState);
//        return INVALID_OPERATION;
//    }
    status_t ret = NO_ERROR;
    if(mCurrentState == MEDIA_PLAYER_PAUSED){

    }else if(mCurrentState == MEDIA_PLAYER_PREPARED){
        pthread_create(&mPlayerThread, NULL, startPlayer, this);
    }else{
        status_t ret = INVALID_OPERATION;
        LOGE("starting player thread error state :%d",mCurrentState);
    }
    return ret;
}

status_t MediaPlayer::suspend() {
    LOGI("suspend");
	
	mCurrentState = MEDIA_PLAYER_STOPPED;
	if(mDecoderAudio != NULL) {
		mDecoderAudio->stop();
	}
	if(mDecoderVideo != NULL) {
		mDecoderVideo->stop();
	}
	
	if(pthread_join(mPlayerThread, NULL) != 0) {
		LOGE("Couldn't cancel player thread");
	}
	
	// Close the codec
    if(mDecoderAudio != NULL) {
        delete mDecoderAudio;
    }
    if(mDecoderVideo != NULL) {
        delete mDecoderVideo;
    }
	
	// Close the video file
    avformat_close_input(&mAVFormatCtx);

    return NO_ERROR;
}

bool MediaPlayer::shouldCancel(PacketQueue* queue)
{
	return (mCurrentState == MEDIA_PLAYER_STATE_ERROR || mCurrentState == MEDIA_PLAYER_STOPPED ||
			 ((mCurrentState == MEDIA_PLAYER_DECODED || mCurrentState == MEDIA_PLAYER_STARTED) 
			  && queue->size() == 0));
}

void MediaPlayer::renderVideoCB(Image *pImg, void *userData){
    MediaPlayer *player = (MediaPlayer *)userData;
    player->renderVideo(pImg);

      if(FPS_DEBUGGING) {
          timeval pTime;
          static int frames = 0;
          static double t1 = -1;
          static double t2 = -1;

          gettimeofday(&pTime, NULL);
          t2 = pTime.tv_sec + (pTime.tv_usec / 1000000.0);
          if (t1 == -1 || t2 > t1 + 1) {
              LOGI("Video fps:%i", frames);
              t1 = t2;
              frames = 0;
          }
          frames++;
      }
}

void MediaPlayer::renderVideo(Image *pImg){
    AVStream* stream = NULL;
    int pts = 0;
    int got_frame = 0;
    if(mVideoQueue && pImg){
        stream = mAVFormatCtx->streams[mVideoStreamIndex];
        if(mVideoQueue->get(&mVideoPacket, true) < 0){
            return;
        }
        //timeval t0,t1;
        //gettimeofday(&t0, NULL);
        avcodec_decode_video2(stream->codec, mVideoFrame, &got_frame, &mVideoPacket);
        //gettimeofday(&t1, NULL);
        //LOGE("decodev2 mydt==%ld",(1000000 * (t1.tv_sec - t0.tv_sec) + t1.tv_usec - t0.tv_usec)/1000);

        if (got_frame) {
            /*gettimeofday(&t0, NULL);
            LOGE("decodev2 videoFmt: %s",av_get_pix_fmt_name(stream->codec->pix_fmt));
            LOGE("decodev2 ImgSize: %d x %d",pImg->width, pImg->height);
            LOGE("decodev2 VideoSize: %d x %d",mVideoFrame->width, mVideoFrame->height);
            LOGE("decodev2 linesize:0-%d ,1-%d, 2-%d",mVideoFrame->linesize[0],
                 mVideoFrame->linesize[1],  mVideoFrame->linesize[2]);*/
            fillYUV420PImage(pImg, mVideoFrame->data, mVideoFrame->linesize, pImg->width, pImg->height);
            //gettimeofday(&t1, NULL);
            //LOGE("fill420PImage mydt==%ld",(1000000 * (t1.tv_sec - t0.tv_sec) + t1.tv_usec - t0.tv_usec)/1000);
#ifdef DUM_A
            static int idx = 0;
            if(idx<10){
                char fn[256]={0,};
                int bufsize = (pImg->width * pImg->height) * 3 / 2;
                sprintf(fn,"/sdcard/DCIM/my_%d_%d_%d_dump.yuv",pImg->width,pImg->height,idx);
                imageUtil::saveBufToFile((char*)(pImg->plane[0]),bufsize, fn);
                idx++;
            }
#endif//DUM_A
        }
        av_free_packet(&mVideoPacket);
    }
}

void MediaPlayer::audioFrameCB(void* userdata){
    MediaPlayer *player = (MediaPlayer*)userdata;
    player->decodeAudioFrame();

    if(FPS_DEBUGGING) {
        timeval pTime;
        static int frames = 0;
        static double t1 = -1;
        static double t2 = -1;

        gettimeofday(&pTime, NULL);
        t2 = pTime.tv_sec + (pTime.tv_usec / 1000000.0);
        if (t1 == -1 || t2 > t1 + 1) {
            LOGI("Audio fps:%i", frames);
            t1 = t2;
            frames = 0;
        }
        frames++;
    }
}
void MediaPlayer::decodeAudioFrame(){
    int got_frame = 0;
    int data_size = 0;
    if(!mAudioTrack){
        return;
    }
    AVStream* stream = mAVFormatCtx->streams[mAudioStreamIndex];
    AVCodecContext* codec_ctx = stream->codec;
    if(mAudioSwrCtx==NULL){
        mAudioSwrCtx = swr_alloc_set_opts(NULL,
            codec_ctx->channel_layout, AV_SAMPLE_FMT_S16, 44100,
            codec_ctx->channel_layout,
            codec_ctx->sample_fmt ,
            codec_ctx->sample_rate,
            0, NULL);
        swr_init(mAudioSwrCtx);
    }
    if(mAudioSwrCtx==NULL){
        return;
    }
    if(!mAudioQueue->get(&mAudioPacket, true)){
        return;
    }

    avcodec_decode_audio4(codec_ctx, mAudioFrame, &got_frame, &mAudioPacket);
    if(!got_frame){
        return;
    }
    data_size = av_samples_get_buffer_size(NULL,
            codec_ctx->channels,
            mAudioFrame->nb_samples,
            AV_SAMPLE_FMT_S16, 0);
     if (data_size > 0) {
         if(mRawAudioBuf==NULL){
             mRawAudioBuf = new uint8_t[data_size];
         }
         swr_convert(mAudioSwrCtx,
                 &mRawAudioBuf, mAudioFrame->nb_samples,
                 (const uint8_t**)mAudioFrame->data, mAudioFrame->nb_samples);

         mAudioTrack->write(mRawAudioBuf, data_size);
     }
    av_free_packet(&mAudioPacket);
}

void MediaPlayer::doDecode(void* ptr)
{
	AVPacket pPacket;
	
//	AVStream* stream_audio = mAVFormatCtx->streams[mAudioStreamIndex];
//	mDecoderAudio = new DecoderAudio(stream_audio);
//	mDecoderAudio->onDecode = decodeAudioCB;
//	mDecoderAudio->userData=this;
//	mDecoderAudio->startAsync();
    if(mAudioStreamIndex) {
        AVStream* stream_audio = mAVFormatCtx->streams[mAudioStreamIndex];
        mAudioTrack = new SLESAudioTrack(44100, SL_PCMSAMPLEFORMAT_FIXED_16, stream_audio->codec->channels);
        mAudioTrack->setAudioCallback(audioFrameCB, this);
        mAudioFrame = av_frame_alloc();
        mAudioQueue = new PacketQueue();

        mAudioTrack->start();
        char tmp[8];
        mAudioTrack->write(tmp, 8);
    }

    mVideoFrame = av_frame_alloc();
    mVideoQueue = new PacketQueue();
	
//	AVStream* stream_video = mAVFormatCtx->streams[mVideoStreamIndex];
//	mDecoderVideo = new DecoderVideo(stream_video);
//	mDecoderVideo->onDecode = decodeVideoCB;
//	mDecoderVideo->userData = this;
//	mDecoderVideo->startAsync();
	
	mCurrentState = MEDIA_PLAYER_STARTED;
	while (mCurrentState != MEDIA_PLAYER_DECODED && mCurrentState != MEDIA_PLAYER_STOPPED &&
		   mCurrentState != MEDIA_PLAYER_STATE_ERROR)
	{
//		if (mDecoderVideo->packets() > FFMPEG_PLAYER_MAX_QUEUE_SIZE &&
//				mDecoderAudio->packets() > FFMPEG_PLAYER_MAX_QUEUE_SIZE) {
//			usleep(200);
//			continue;
//		}
		
		if(av_read_frame(mAVFormatCtx, &pPacket) < 0) {
//			mCurrentState = MEDIA_PLAYER_DECODED;
			continue;
		}
		// Is this a packet from the video stream?
		if (pPacket.stream_index == mVideoStreamIndex) {
//		    __android_log_print(ANDROID_LOG_ERROR, TAG, "decode %ix%i", mVideoWidth, mVideoHeight);
//			mDecoderVideo->enqueue(&pPacket);
		    if(mVideoQueue){
		        mVideoQueue->put(&pPacket);
		    }
		} 
		else if (pPacket.stream_index == mAudioStreamIndex) {
//			mDecoderAudio->enqueue(&pPacket);
		    if(mAudioQueue){
		        mAudioQueue->put(&pPacket);
		    }
		}
		else {
			// Free the packet that was allocated by av_read_frame
			av_free_packet(&pPacket);
		}
	}
	
	//waits on end of video thread
	__android_log_print(ANDROID_LOG_ERROR, TAG, "waiting on video thread");
	int ret = -1;
	if((ret = mDecoderVideo->wait()) != 0) {
		__android_log_print(ANDROID_LOG_ERROR, TAG, "Couldn't cancel video thread: %i", ret);
	}
	
	__android_log_print(ANDROID_LOG_ERROR, TAG, "waiting on audio thread");
	if((ret = mDecoderAudio->wait()) != 0) {
		__android_log_print(ANDROID_LOG_ERROR, TAG, "Couldn't cancel audio thread: %i", ret);
	}

    deleteC(mAudioTrack);
	if(mAudioSwrCtx){
	    swr_free(&mAudioSwrCtx);
	    mAudioSwrCtx=NULL;
	}
	if(mRawAudioBuf){
	    delete []mRawAudioBuf;
	    mRawAudioBuf=NULL;
	}
    deleteC(mAudioQueue);

	if(mAudioFrame){
	    av_free(mAudioFrame);
	    mAudioFrame = NULL;
	}
    deleteC(mVideoQueue);

    if(mVideoFrame){
        av_free(mVideoFrame);
        mVideoFrame = NULL;
    }
	if(mCurrentState == MEDIA_PLAYER_STATE_ERROR) {
		__android_log_print(ANDROID_LOG_INFO, TAG, "playing err");
	}
	mCurrentState = MEDIA_PLAYER_PLAYBACK_COMPLETE;
	__android_log_print(ANDROID_LOG_INFO, TAG, "end of playing");
}

status_t MediaPlayer::stop()
{
	//pthread_mutex_lock(&mLock);
	mCurrentState = MEDIA_PLAYER_STOPPED;
	//pthread_mutex_unlock(&mLock);
    return NO_ERROR;
}

status_t MediaPlayer::pause()
{
	//pthread_mutex_lock(&mLock);
	mCurrentState = MEDIA_PLAYER_PAUSED;
	//pthread_mutex_unlock(&mLock);
	return NO_ERROR;
}

bool MediaPlayer::isPlaying()
{
    return mCurrentState == MEDIA_PLAYER_STARTED || 
		mCurrentState == MEDIA_PLAYER_DECODED;
}

status_t MediaPlayer::getVideoWidth(int *w)
{
	if (mCurrentState < MEDIA_PLAYER_PREPARED) {
		return INVALID_OPERATION;
	}
	*w = mVideoWidth;
    return NO_ERROR;
}

status_t MediaPlayer::getVideoHeight(int *h)
{
	if (mCurrentState < MEDIA_PLAYER_PREPARED) {
		return INVALID_OPERATION;
	}
	*h = mVideoHeight;
    return NO_ERROR;
}

status_t MediaPlayer::getCurrentPosition(int *msec)
{
	if (mCurrentState < MEDIA_PLAYER_PREPARED) {
		return INVALID_OPERATION;
	}
	*msec = 0/*av_gettime()*/;
	//__android_log_print(ANDROID_LOG_INFO, TAG, "position %i", *msec);
	return NO_ERROR;
}

status_t MediaPlayer::getDuration(int *msec)
{
	if (mCurrentState < MEDIA_PLAYER_PREPARED) {
		return INVALID_OPERATION;
	}
	*msec = mDuration / 1000;
    return NO_ERROR;
}

status_t MediaPlayer::seekTo(int msec)
{
    return INVALID_OPERATION;
}

status_t MediaPlayer::reset()
{
    return INVALID_OPERATION;
}

status_t MediaPlayer::setAudioStreamType(int type)
{
	return NO_ERROR;
}

void MediaPlayer::ffmpegNotify(void* ptr, int level, const char* fmt, va_list vl) {
	
	switch(level) {
			/**
			 * Something went really wrong and we will crash now.
			 */
		case AV_LOG_PANIC:
			__android_log_print(ANDROID_LOG_ERROR, TAG, "AV_LOG_PANIC: %s", fmt);
			//sPlayer->mCurrentState = MEDIA_PLAYER_STATE_ERROR;
			break;
			
			/**
			 * Something went wrong and recovery is not possible.
			 * For example, no header was found for a format which depends
			 * on headers or an illegal combination of parameters is used.
			 */
		case AV_LOG_FATAL:
			__android_log_print(ANDROID_LOG_ERROR, TAG, "AV_LOG_FATAL: %s", fmt);
			//sPlayer->mCurrentState = MEDIA_PLAYER_STATE_ERROR;
			break;
			
			/**
			 * Something went wrong and cannot losslessly be recovered.
			 * However, not all future data is affected.
			 */
		case AV_LOG_ERROR:
			__android_log_print(ANDROID_LOG_ERROR, TAG, "AV_LOG_ERROR: %s", fmt);
			//sPlayer->mCurrentState = MEDIA_PLAYER_STATE_ERROR;
			break;
			
			/**
			 * Something somehow does not look correct. This may or may not
			 * lead to problems. An example would be the use of '-vstrict -2'.
			 */
		case AV_LOG_WARNING:
			__android_log_print(ANDROID_LOG_ERROR, TAG, "AV_LOG_WARNING: %s", fmt);
			break;
			
		case AV_LOG_INFO:
			__android_log_print(ANDROID_LOG_INFO, TAG, "%s", fmt);
			break;
			
		case AV_LOG_DEBUG:
			__android_log_print(ANDROID_LOG_DEBUG, TAG, "%s", fmt);
			break;
			
	}
}

void MediaPlayer::notify(int msg, int ext1, int ext2)
{
    //__android_log_print(ANDROID_LOG_INFO, TAG, "message received msg=%d, ext1=%d, ext2=%d", msg, ext1, ext2);
    bool send = true;
    bool locked = false;

    if ((mListener != 0) && send) {
       //__android_log_print(ANDROID_LOG_INFO, TAG, "callback application");
       mListener->notify(msg, ext1, ext2);
       //__android_log_print(ANDROID_LOG_INFO, TAG, "back from callback");
	}
}
