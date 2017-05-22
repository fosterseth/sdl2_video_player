//gcc -w -o audvid audvid.c -I/usr/local/include -L/usr/local/lib -llibavcodec -llibavformat -llibavutil `sdl2-config --cflags --libs`
/*
8-bit support
AUDIO_S8 signed 8-bit samples
AUDIO_U8 unsigned 8-bit samples

16-bit support
AUDIO_S16LSB signed 16-bit samples in little-endian byte order
AUDIO_S16MSB signed 16-bit samples in big-endian byte order
AUDIO_S16SYS signed 16-bit samples in native byte order
AUDIO_S16 AUDIO_S16LSB
AUDIO_U16LSB unsigned 16-bit samples in little-endian byte order
AUDIO_U16MSB unsigned 16-bit samples in big-endian byte order
AUDIO_U16SYS unsigned 16-bit samples in native byte order
AUDIO_U16 AUDIO_U16LSB

32-bit support (new to SDL 2.0)
AUDIO_S32LSB 32-bit integer samples in little-endian byte order
AUDIO_S32MSB 32-bit integer samples in big-endian byte order
AUDIO_S32SYS 32-bit integer samples in native byte order
AUDIO_S32 AUDIO_S32LSB

float support (new to SDL 2.0)
AUDIO_F32LSB 32-bit floating point samples in little-endian byte order
AUDIO_F32MSB 32-bit floating point samples in big-endian byte order
AUDIO_F32SYS 32-bit floating point samples in native byte order
AUDIO_F32 AUDIO_F32LSB

==========================================

AV_SAMPLE_FMT_NONE 	
AV_SAMPLE_FMT_U8    unsigned 8 bits
AV_SAMPLE_FMT_S16 	signed 16 bits
AV_SAMPLE_FMT_S32 	signed 32 bits
AV_SAMPLE_FMT_FLT 	float
AV_SAMPLE_FMT_DBL 	double
AV_SAMPLE_FMT_U8P 	unsigned 8 bits, planar
AV_SAMPLE_FMT_S16P 	signed 16 bits, planar
AV_SAMPLE_FMT_S32P 	signed 32 bits, planar
AV_SAMPLE_FMT_FLTP 	float, planar
AV_SAMPLE_FMT_DBLP 	double, planar
AV_SAMPLE_FMT_NB 	Number of sample formats. DO NOT USE if linking dynamically.
*/

/* difference between surface and texture
http://stackoverflow.com/questions/21007329/what-is-a-sdl-renderer/21007477#21007477
*/

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <stdio.h>
#include "packetQueue.h"

typedef struct VideoState {
    char src_filename[1024];
    AVFormatContext *fmt_ctx ;
    AVCodecContext *video_dec_ctx;
    AVCodecContext *audio_dec_ctx;
    AVStream *video_stream;
    AVStream *audio_stream;
    AVPacket pkt;
    AVFrame *frame;
    int audio_stream_idx;
    int video_stream_idx;
    int got_frame;
    int video_frame_count;
    SDL_Renderer *Renderer;
    SDL_Window *Window;
    SDL_Texture *Texture;
    SDL_Thread *thread;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
    PacketQueue videoqueue;
} VideoState;

static int open_codec_context(int *stream_idx,
                                AVFormatContext *fmt_ctx,
                                enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file\n");
        return ret;
    } else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec_ctx = st->codec;
        dec = avcodec_find_decoder(dec_ctx->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

void initiate_audio_device(VideoState *vs){
	SDL_zero(vs->want);
	vs->want.freq = vs->audio_dec_ctx->sample_rate;
	vs->want.format = AUDIO_F32SYS;
	vs->want.channels = 1;
	vs->want.samples = 4096;
	vs->dev = SDL_OpenAudioDevice(NULL, 0, &vs->want, &vs->have, 0);
	SDL_PauseAudioDevice(vs->dev, 0);
}

void initiate_renderer_window(VideoState *vs){
    int width = vs->video_dec_ctx->width;
	int height = vs->video_dec_ctx->height;
	int ret = 0;
	// create window
	vs->Window = SDL_CreateWindow(
			"MOVIE",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			width,
			height,
			SDL_WINDOW_RESIZABLE);
	
	if (!vs->Window){
		fprintf(stderr, "Count not open window, aborting\n");
		ret = 1;
	}
	
	vs->Renderer = SDL_CreateRenderer(vs->Window, -1, 0);
	
	if (!vs->Renderer){
		fprintf(stderr, "Count not open renderer, aborting\n");
		ret = 1;
	}
	
	vs->Texture = SDL_CreateTexture(
        vs->Renderer,
		SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
        );
						
	SDL_SetRenderDrawColor( vs->Renderer, 0, 0, 0, SDL_ALPHA_OPAQUE );
	SDL_RenderClear( vs->Renderer );
}

int displayFrame(VideoState *vs){
    /* decode video frame */
    avcodec_decode_video2(vs->video_dec_ctx, vs->frame, &vs->got_frame, &vs->pkt);
    if (vs->got_frame) {
        vs->frame->pts = av_frame_get_best_effort_timestamp(vs->frame);
        vs->frame->pts = av_rescale_q(vs->frame->pts, vs->video_stream->time_base, AV_TIME_BASE_Q);
        printf("video_frame n:%d coded_n:%d pts:%d\n",
               vs->video_frame_count++,
               vs->frame->coded_picture_number,
               vs->frame->pts);
        SDL_UpdateYUVTexture(vs->Texture,
                                NULL,
                                vs->frame->data[0],
                                vs->frame->linesize[0],
                                vs->frame->data[1],
                                vs->frame->linesize[1],
                                vs->frame->data[2],
                                vs->frame->linesize[2]);
        SDL_RenderCopy(vs->Renderer, vs->Texture, NULL, NULL);
        SDL_RenderPresent(vs->Renderer);
    }
	return 0;
}

int decode_packet(VideoState *vs){
    int decoded = vs->pkt.size;
    int ret = 0;
	if (vs->pkt.stream_index == vs->audio_stream_idx){
		ret = avcodec_decode_audio4(vs->audio_dec_ctx, vs->frame, &vs->got_frame, &vs->pkt);
        decoded = FFMIN(ret, vs->pkt.size);
        if (vs->got_frame){
            size_t unpadded_linesize = vs->frame->nb_samples * av_get_bytes_per_sample(AV_SAMPLE_FMT_FLTP);
            vs->frame->pts = av_frame_get_best_effort_timestamp(vs->frame);
            vs->frame->pts = av_rescale_q(vs->frame->pts, vs->audio_stream->time_base, AV_TIME_BASE_Q);
            printf("audio_frame queued: %d nb_samples:%d pts:%d\n",
                    SDL_GetQueuedAudioSize(vs->dev),
                    vs->frame->nb_samples,
                    vs->frame->pts);
            SDL_QueueAudio(vs->dev, vs->frame->extended_data[0], unpadded_linesize);
            SDL_PauseAudioDevice(vs->dev, 0);
        }
    } else if (vs->pkt.stream_index == vs->video_stream_idx){
        displayFrame(vs);
    }
	
	if (vs->got_frame)
		av_frame_unref(vs->frame);
        
    return decoded;
}

int decode_thread(void *arg){
	VideoState *vs = (VideoState *)arg;
	
	avformat_open_input(&vs->fmt_ctx, vs->src_filename, NULL, NULL);
	avformat_find_stream_info(vs->fmt_ctx, NULL);
	
	av_dump_format(vs->fmt_ctx, 0, vs->src_filename, 0);
	
	if (open_codec_context(&vs->audio_stream_idx, vs->fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0){
        vs->audio_stream = vs->fmt_ctx->streams[vs->audio_stream_idx];
        vs->audio_dec_ctx = vs->audio_stream->codec;
    }
    
    if (open_codec_context(&vs->video_stream_idx, vs->fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0){
        vs->video_stream = vs->fmt_ctx->streams[vs->video_stream_idx];
        vs->video_dec_ctx = vs->video_stream->codec;
    }


	// open audio device
	initiate_audio_device(vs);
    initiate_renderer_window(vs);

	vs->frame = av_frame_alloc();
	av_init_packet(&vs->pkt);
	vs->pkt.data = NULL;
	vs->pkt.size = 0;
	
	int ret = 0;
	while (av_read_frame(vs->fmt_ctx, &vs->pkt) >= 0) {
        AVPacket orig_pkt = vs->pkt;
        do {
            ret = decode_packet(vs);
            if (ret < 0)
                break;
            vs->pkt.data += ret;
            vs->pkt.size -= ret;
        } while (vs->pkt.size > 0);
        av_packet_unref(&orig_pkt);
    }
    while(SDL_GetQueuedAudioSize(vs->dev) > 0){
        continue;
    }

	return 0;
}

int main(int argc, char **argv){
	VideoState *vs;
	SDL_Thread *thread;
	int threadreturn;

	vs = av_mallocz(sizeof(VideoState));
	
    av_strlcpy(vs->src_filename, argv[1], sizeof(vs->src_filename));
    
    av_register_all();
    
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_EVENTS);
	
	thread = SDL_CreateThread(decode_thread, "decoder", vs);

	SDL_WaitThread(thread, &threadreturn);
	SDL_Quit;
	return 0;

}

