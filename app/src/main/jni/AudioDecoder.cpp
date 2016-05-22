#include "AudioDecoder.h"
#include "jnilogger.h"

const static AVSampleFormat OUT_FMT = AV_SAMPLE_FMT_S16;

#define getAVStream(track) \
	AVStream *pAVStream = NULL; \
	if ((track) >= 0 && pAVFormatCtx) { \
		pAVStream = pAVFormatCtx->streams[(track)]; \
		}

bool AudioDecoder::globalInit = false;

static int decodePacket(AVCodecContext *codecCtx, AVPacket *pPacket, AVFrame *pFrame, int *got_frame) {
    //LOGI("in func:%s",__FUNCTION__);
	int ret = 0;
	int decoded = pPacket->size;

	*got_frame = 0;

	/* decode audio frame */
	ret = avcodec_decode_audio4(codecCtx, pFrame, got_frame, pPacket);
	//LOGI("decode_audio4 func:%s",__FUNCTION__);
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

//LOGI("out func:%s",__FUNCTION__);
	return decoded;
}

AudioDecoder::AudioDecoder() :pAVFormatCtx(NULL), audioTrack(-1), pFrame(NULL),
currentPostion(0), pSwrCtx(NULL), dstData(NULL), maxSamples(0){
  //  LOGI("in func:%s",__FUNCTION__);
	//if (false == globalInit) {
		av_register_all();
		avformat_network_init();
		avcodec_register_all();
	//	globalInit = true;
	//}

	av_init_packet(&avpacket);
	avpacket.data = NULL;
	avpacket.size = 0;

	pFrame = av_frame_alloc();

	//LOGI("out func:%s",__FUNCTION__);
}

AudioDecoder::~AudioDecoder() {
	if (pAVFormatCtx) {
		avformat_close_input(&pAVFormatCtx);
	}
	if (pFrame) {
		av_frame_free(&pFrame);
	}
	if (pSwrCtx) {
		swr_free(&pSwrCtx);
		pSwrCtx = NULL;
	}
    if(dstData){
        av_free(dstData);
    }
}

bool AudioDecoder::load(const char* url) {
    //LOGI("in func:%s",__FUNCTION__);
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

	if (codec_ctx->sample_fmt != OUT_FMT) {
		pSwrCtx = swr_alloc_set_opts(NULL,
			codec_ctx->channel_layout, OUT_FMT, codec_ctx->sample_rate,
			codec_ctx->channel_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
			0, NULL);
		swr_init(pSwrCtx);
	}
	//LOGI("out func:%s",__FUNCTION__);
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
   //LOGI("in func:%s",__FUNCTION__);
	int64_t duration = 0;
	if (pAVFormatCtx) {
		duration = av_rescale(pAVFormatCtx->duration, 1000, AV_TIME_BASE);
	}
	//LOGI("out func:%s",__FUNCTION__);
	return duration;
}

int64_t AudioDecoder::getCurrentPostion() {
//	int64_t pos = av_rescale(currentPostion, AV_TIME_BASE, 1000);
//	return pos;
	return currentPostion;
}

int AudioDecoder::seekTo(int64_t ms) {
    //LOGI("in func:%s",__FUNCTION__);
	getAVStream(audioTrack);
	if (pAVStream) {
		int64_t timestamp = ms / (1000.0f * av_q2d(pAVStream->time_base));
		return  av_seek_frame(pAVFormatCtx, audioTrack, timestamp,
			AVSEEK_FLAG_BACKWARD);
	}
	//LOGI("out func:%s",__FUNCTION__);
	return -1;
}

int  AudioDecoder::convertSample(AVCodecContext * pCodecCtx) {

    int ret = -1;
    int dst_linesize = 0;
    getAVStream(audioTrack);
	if (pSwrCtx && pAVStream) {
        int srcRate = pAVStream->codec->sample_rate;
      //  LOGI("in func:%s,,srcRate:%d,,nb_samples:%d",__FUNCTION__,srcRate,pFrame->nb_samples);
        int dst_nb_samples = av_rescale_rnd(swr_get_delay(pSwrCtx, srcRate) + pFrame->nb_samples,
            srcRate, srcRate, AV_ROUND_UP);
        if (NULL == dstData || dst_nb_samples > maxSamples) {
            if(dstData){
                av_free(dstData);
            }
        //    LOGI("in av_samples_alloc,,dst_nb_samples:%d",dst_nb_samples);
            ret = av_samples_alloc(&dstData, &dst_linesize, pAVStream->codec->channels,
                                       dst_nb_samples, OUT_FMT, 0);
          //  LOGI("in av_samples_alloc,,ret:%d",ret);
            if (ret < 0){
            //    LOGI(" av_samples_alloc fail func:%s",__FUNCTION__);
                return ret;
            }
            maxSamples = dst_nb_samples;
		}
		//LOGI("convert func:%s,,bufVArrd:%ld",__FUNCTION__,dstData);
		ret = swr_convert(pSwrCtx, &dstData, dst_nb_samples,
			(const uint8_t**)pFrame->data, pFrame->nb_samples);
		//LOGI("afterconvert func:%s,,ret:%d",__FUNCTION__,ret);
	    ret = av_samples_get_buffer_size(&dst_linesize, pAVStream->codec->channels, ret, OUT_FMT, 0);
	//	LOGI("afterconvert func:%s",__FUNCTION__);
	}
	return ret;
}

int AudioDecoder::readFrame(void **buf, int *sizeInBytes) {
    //LOGI("in func:%s",__FUNCTION__);
	int ret = -1;
	int got_frame = 0;
	//int isPlanar = 0;
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
	//isPlanar = av_sample_fmt_is_planar(pCodecCtx->sample_fmt);
	if (audioTrack == avpacket.stream_index) {
		do {
			ret = decodePacket(pCodecCtx, &avpacket, pFrame, &got_frame);
			if (ret < 0) {
				break;
			}
			avpacket.size -= ret;
			avpacket.data += ret;
		} while (avpacket.size > 0);

		if(got_frame){
			if(AV_NOPTS_VALUE == (avpacket.pts)){
				currentPostion = avpacket.dts * (1000.0f * av_q2d(pAVStream->time_base));
			}else{
				currentPostion = avpacket.pts * (1000.0f * av_q2d(pAVStream->time_base));
			}
			int sampleBytes = convertSample(pCodecCtx);
            if (sampleBytes > 0) {
                *sizeInBytes = sampleBytes;
                *buf = dstData;
      //          LOGI("outBytes:%d,func:%s",*sizeInBytes, __FUNCTION__);
            }else {
                *sizeInBytes = pFrame->nb_samples * av_get_bytes_per_sample((AVSampleFormat)(pFrame->format));
                *buf = pFrame->data[0];
            }
	    }else{
     	    ret = -1;
     	}
	}
	av_free_packet(&avpacket);
	//LOGI("out func:%s",__FUNCTION__);
	return ret;
}