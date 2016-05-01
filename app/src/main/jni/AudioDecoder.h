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
	int seekTo(int64_t ms);
	int readFrame(void **buf, int *sizeInBytes);
private:
	static bool globalInit;

	AVFormatContext *pAVFormatCtx;
	int audioTrack;
	AVPacket avpacket;
	AVFrame *pFrame;
};

#endif