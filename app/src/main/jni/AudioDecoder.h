#pragma once
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include "libswresample/swresample.h"
}
#ifndef __AA_AUDIO_DECODER_H
#define __AA_AUDIO_DECODER_H

class AudioDecoder {
public:
	AudioDecoder();
	virtual ~AudioDecoder();
	bool load(const char* url);
	int getChannelCount();
	int getSampleRate();
	int bytesPerSample();
	int getBitRate();
	int64_t getDuration();
	int64_t getCurrentPostion();
	int seekTo(int64_t ms);
	int readFrame(void **buf, int *sizeInBytes);
private:
	static bool globalInit;

	int convertSample(AVCodecContext * pContext);

	AVFormatContext *pAVFormatCtx;
	int audioTrack;
	AVPacket avpacket;
	AVFrame *pFrame;
	int64_t currentPostion;
	struct SwrContext *pSwrCtx;
	uint8_t *dstData;
	int maxSamples;
};

#endif