#include "AudioDecoder.h"

#define getAVStream(track) \
	AVStream *pAVStream = NULL; \
	if ((track) >= 0 && pAVFormatCtx) { \
		pAVStream = pAVFormatCtx->streams[(track)]; \
	} 

bool AudioDecoder::globalInit = false;

static int decodePacket(AVCodecContext *codecCtx, AVPacket *pPacket, AVFrame *pFrame, int *got_frame) {
	int ret = 0;
	int decoded = pPacket->size;

	*got_frame = 0;

	/* decode audio frame */
	ret = avcodec_decode_audio4(codecCtx, pFrame, got_frame, pPacket);
	if (ret < 0) {
		return ret;
	}
	/* Some audio decoders decode only part of the packet, and have to be
	* called again with the remainder of the packet data.
	* Sample: fate-suite/lossless-audio/luckynight-partial.shn
	* Also, some decoders might over-read the packet. */
	decoded = FFMIN(ret, pPacket->size);

	/* If we use the new API with reference counting, we own the data and need
	* to de-reference it when we don't use it anymore */
	//if (*got_frame && api_mode == API_MODE_NEW_API_REF_COUNT) {
		//av_frame_unref(pFrame);
	//}

	return decoded;
}

AudioDecoder::AudioDecoder() :pAVFormatCtx(NULL), audioTrack(-1), pFrame(NULL){
	if (false == globalInit) {
		av_register_all();
		avformat_network_init();
		avcodec_register_all();
		globalInit = true;
	}

	av_init_packet(&avpacket);
	avpacket.data = NULL;
	avpacket.size = 0;

	pFrame = av_frame_alloc();
}

AudioDecoder::~AudioDecoder() {
	if (pAVFormatCtx) {
		avformat_close_input(&pAVFormatCtx);
	}
	if (pFrame) {
		av_frame_free(&pFrame);
	}
}

bool AudioDecoder::load(const char* url) {
	if (!url) {
		return false;
	}
	AVIOContext  *io_context = NULL;
	if (avio_open(&io_context, url, 0)<0) {
		return false;
	}

	// Open video file
	if (avformat_open_input(&pAVFormatCtx, url, NULL, NULL) != 0) {
		return false;  // Couldn't open file
	}

	// Retrieve stream information
	if (avformat_find_stream_info(pAVFormatCtx, NULL)<0) {
		return false; // Couldn't find stream information
	}

	// Dump information about file onto standard error
	av_dump_format(pAVFormatCtx, 0, url, 0);

	AVDictionary *optionsDict = NULL;
	AVStream *pAVStream = NULL;
	for (int i = 0; i < pAVFormatCtx->nb_streams; i++) {
		if (pAVFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			pAVStream = pAVFormatCtx->streams[i];
			audioTrack = i;
			break;
		}
	}

	if (!pAVStream) {
		audioTrack = -1;
		return false;
	}

	// Get a pointer to the codec context for the video stream
	AVCodecContext* codec_ctx = pAVStream->codec;
	AVCodec* codec = avcodec_find_decoder(codec_ctx->codec_id);
	if (codec == NULL) {
		return false;
	}

	// Open codec
	if (avcodec_open2(codec_ctx, codec, &optionsDict) < 0) {
		return false;
	}

	return true;
}


int AudioDecoder::getChannelCount() {
	getAVStream(audioTrack);
	if (pAVStream && pAVStream->codec) {
		return pAVStream->codec->channels;
	}
	return -1;
}

int AudioDecoder::getSampleRate() {
	getAVStream(audioTrack);
	if (pAVStream && pAVStream->codec) {
		return pAVStream->codec->sample_rate;
	}
	return -1;
}

int AudioDecoder::bytesPerSample() {
	getAVStream(audioTrack);
	if (pAVStream && pAVStream->codec) {
		return 2;
	}
	return -1;
}

int AudioDecoder::getBitRate() {
	getAVStream(audioTrack);
	if (pAVStream && pAVStream->codec) {
		return pAVStream->codec->bit_rate;
	}
	return -1;
}

int64_t AudioDecoder::getDuration() {
	if (pAVFormatCtx) {
		return pAVFormatCtx->duration;
	}
	return -1;
}

int AudioDecoder::seekTo(int64_t ms) {
	getAVStream(audioTrack);
	if (pAVStream) {
		int64_t timestamp = ms / (1000.0f * av_q2d(pAVStream->time_base));
		return  av_seek_frame(pAVFormatCtx, audioTrack, timestamp,
			AVSEEK_FLAG_BACKWARD);
	}
	return -1;
}

int AudioDecoder::readFrame(void **buf, int *sizeInBytes) {
	int ret = 0;
	int got_frame = 0;
	getAVStream(audioTrack);
	AVCodecContext *pCodecCtx = pAVStream->codec;
	if (!buf || !sizeInBytes || !pAVStream) {
		return ret;
	}
	*buf = NULL;
	*sizeInBytes = 0;
	ret = av_read_frame(pAVFormatCtx, &avpacket);
	if (ret < 0) {
		return ret;
	}
	if (audioTrack == avpacket.stream_index) {
		do {
			ret = decodePacket(pCodecCtx, &avpacket, pFrame, &got_frame);
			if (ret < 0) {
				break;
			}
			avpacket.size -= ret;
			avpacket.data += ret;
		} while (avpacket.size > 0);
	}
	av_free_packet(&avpacket);
	*buf = pFrame->data[0];
	*sizeInBytes = av_samples_get_buffer_size(NULL,
		pCodecCtx->channels,
		pFrame->nb_samples,
		pCodecCtx->sample_fmt, 0);
	return ret;
}