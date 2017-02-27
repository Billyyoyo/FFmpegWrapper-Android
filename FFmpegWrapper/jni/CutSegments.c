#include <stdio.h>
#include <time.h> 

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#ifdef ANDROID
#include <jni.h>
#include <android/log.h>
#define LOGE(format, ...)  __android_log_print(ANDROID_LOG_ERROR, "(>_<)", format, ##__VA_ARGS__)
#define LOGI(format, ...)  __android_log_print(ANDROID_LOG_INFO,  "(^_^)", format, ##__VA_ARGS__)
#else
#define LOGE(format, ...)  printf("(>_<) " format "\n", ##__VA_ARGS__)
#define LOGI(format, ...)  printf("(^_^) " format "\n", ##__VA_ARGS__)
#endif

int duration=3;

//Output FFmpeg's av_log()
void custom_log(void *ptr, int level, const char* fmt, va_list vl){

	//To TXT file
	FILE *fp=fopen("/storage/emulated/0/av_log.txt","a+");
	if(fp){
		vfprintf(fp,fmt,vl);
		fflush(fp);
		fclose(fp);
	}

	//To Logcat
	//LOGE(fmt, vl);
}


JNIEXPORT jint JNICALL Java_net_openwatch_ffmpegwrapper_SegmentWrapper_nativeSetDuration (JNIEnv *env, jobject obj, jint duration_jint){
	duration=(int)duration_jint;
	return 0;
}

void Java_postProgress(JNIEnv *env, int total_size){
    jclass m_CallBackClass = (*env)->FindClass(env, "net/openwatch/ffmpegwrapper/SegmentWrapper");
	if(NULL != m_CallBackClass){
		jmethodID m_CallBackQueueMethod = (*env)->GetStaticMethodID(env, m_CallBackClass, "onNativeProgress", "(I)V");
		if(NULL != m_CallBackQueueMethod){
			(*env)->CallStaticVoidMethod(env, m_CallBackClass, m_CallBackQueueMethod, total_size);
		}
		(*env)->DeleteLocalRef(env, m_CallBackClass);
	}
}

JNIEXPORT jint JNICALL Java_net_openwatch_ffmpegwrapper_SegmentWrapper_nativeCutSegment
  (JNIEnv *env, jobject obj, jstring input_jstr, jstring output_jstr)
{
  AVOutputFormat *ofmt = NULL;
	AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
	AVPacket pkt;

	int ret, i;
	char input_str[500]={0};
	char output_str[500]={0};
	char info[1000]={0};
	sprintf(input_str,"%s",(*env)->GetStringUTFChars(env,input_jstr, NULL));
	sprintf(output_str,"%s",(*env)->GetStringUTFChars(env,output_jstr, NULL));


	//FFmpeg av_log() callback
	av_log_set_callback(custom_log);

	av_register_all();
	//Network
	avformat_network_init();

	//Input
	if ((ret = avformat_open_input(&ifmt_ctx, input_str, 0, 0)) < 0) {
		LOGE( "Could not open input file.");
		goto end;
	}
	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
		LOGE( "Failed to retrieve input stream information");
		goto end;
	}

	int videoindex=-1;
	for(i=0; i<ifmt_ctx->nb_streams; i++) 
		if(ifmt_ctx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO){
			videoindex=i;
			break;
		}
	//Output
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "hls",output_str); //RTMP

	av_opt_set_int(ofmt_ctx->priv_data, "hls_time", duration, 0);
	av_opt_set_int(ofmt_ctx->priv_data, "hls_list_size", 0, 0);
	av_opt_set_int(ofmt_ctx->priv_data, "start_number", 0, 0);

	if (!ofmt_ctx) {
		LOGE( "Could not create output context\n");
		ret = AVERROR_UNKNOWN;
		goto end;
	}
	ofmt = ofmt_ctx->oformat;
	for (i = 0; i < ifmt_ctx->nb_streams; i++) {
		//Create output AVStream according to input AVStream
		AVStream *in_stream = ifmt_ctx->streams[i];
		AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
		if (!out_stream) {
			LOGE( "Failed allocating output stream\n");
			ret = AVERROR_UNKNOWN;
			goto end;
		}
		//Copy the settings of AVCodecContext
		ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
		if (ret < 0) {
			LOGE( "Failed to copy context from input to output stream codec context\n");
			goto end;
		}
		out_stream->codec->codec_tag = 0;
		if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}

	//Open output URL
	if (!(ofmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&ofmt_ctx->pb, output_str, AVIO_FLAG_WRITE);
		if (ret < 0) {
			LOGE( "Could not open output URL '%s'", output_str);
			goto end;
		}
	}
	//Write file header
	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0) {
		LOGE( "Error occurred when opening output URL\n");
		goto end;
	}

	AVBitStreamFilterContext* bitstreamFilterContext = av_bitstream_filter_init("h264_mp4toannexb");

	int frame_index=0;

	int64_t start_time=av_gettime();
	int total_size=0;
	while (1) {
		AVStream *in_stream, *out_stream;
		//Get an AVPacket
		ret = av_read_frame(ifmt_ctx, &pkt);
		if (ret < 0)
			break;
		total_size = total_size + pkt.size;
		//FIX£ºNo PTS (Example: Raw H.264)
		//Simple Write PTS
		if(pkt.pts==AV_NOPTS_VALUE){
			//Write PTS
			AVRational time_base1=ifmt_ctx->streams[videoindex]->time_base;
			//Duration between 2 frames (us)
			int64_t calc_duration=(double)AV_TIME_BASE/av_q2d(ifmt_ctx->streams[videoindex]->r_frame_rate);
			//Parameters
			pkt.pts=(double)(frame_index*calc_duration)/(double)(av_q2d(time_base1)*AV_TIME_BASE);
			pkt.dts=pkt.pts;
			pkt.duration=(double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE);
		}
		//Important:Delay
		if(pkt.stream_index==videoindex){
			AVRational time_base=ifmt_ctx->streams[videoindex]->time_base;
			AVRational time_base_q={1,AV_TIME_BASE};
			int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
			int64_t now_time = av_gettime() - start_time;
			if (pts_time > now_time)
				av_usleep(pts_time - now_time);

		}

		in_stream  = ifmt_ctx->streams[pkt.stream_index];
		out_stream = ofmt_ctx->streams[pkt.stream_index];
		/* copy packet */
		//Convert PTS/DTS
		pkt.pts = av_rescale_q(pkt.pts, in_stream->time_base, out_stream->time_base);
		pkt.dts = av_rescale_q(pkt.dts, in_stream->time_base, out_stream->time_base);
		if(pkt.stream_index==videoindex){
			//LOGE("Send %8d video frames to output URL\n",frame_index);
			av_bitstream_filter_filter(bitstreamFilterContext, out_stream->codec, NULL,&pkt.data, &pkt.size,pkt.data, pkt.size, 0);
			frame_index++;
		}
		ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

		if (ret < 0) {
			LOGE( "Error muxing packet\n");
			break;
		}
		av_free_packet(&pkt);

		if(frame_index % 10 == 0){
			Java_postProgress(env, total_size);
		}
		
	}
	LOGE("Total size : %d\n", total_size);
	av_bitstream_filter_close(bitstreamFilterContext);
	//Write file trailer
	av_write_trailer(ofmt_ctx);
end:
	avformat_close_input(&ifmt_ctx);
	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_close(ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);
	avformat_network_deinit();
	if (ret < 0 && ret != AVERROR_EOF) {
		LOGE( "Error occurred.\n");
		return -1;
	}
	return 0;
}

