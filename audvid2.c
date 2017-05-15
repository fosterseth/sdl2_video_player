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

SDL_Renderer *Renderer;
SDL_Window *Window;
SDL_Texture *Texture;
SDL_Thread *thread;
SDL_AudioSpec want, have;
SDL_AudioDeviceID dev;

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

typedef struct VideoState {
	AVFormatContext *fmt_ctx;
	AVCodecContext *video_dec_ctx;
	AVCodecContext *audio_dec_ctx;
	AVStream *audio_stream;
	AVStream *video_stream;
	AVFrame *frame;
	AVPacket pkt;
	
	PacketQueue videoqueue;

	char src_filename[1024];

	int video_stream_idx;
	int audio_stream_idx;

	int got_frame;
	int width, height;
} VideoState;

void packet_queue_init(PacketQueue *q) {
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
};

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

	AVPacketList *pkt1;
	if(av_dup_packet(pkt) < 0) {
		return -1;
	}
	pkt1 = av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	SDL_LockMutex(q->mutex);

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
	return 0;
};

static int packet_queue_get(PacketQueue *q,
							AVPacket *pkt,
							int block)
{
	AVPacketList *pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for(;;) {

		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		} else if (!block) {
			ret = 0;
			break;
		} else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
};

int open_codec_context(int *stream_idx,
						AVFormatContext *fmt_ctx,
						AVCodecContext *dec_ctx,
						AVStream *st,
						enum AVMediaType type)
{

	AVCodec *codec = NULL;
	AVCodecContext *ctx = NULL;

    *stream_idx = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
	st = fmt_ctx->streams[*stream_idx];
	ctx = st->codec;
	codec = avcodec_find_decoder(ctx->codec_id);
	if (!codec)
		printf("codec not found\n");

    if (avcodec_open2(ctx, codec, NULL) < 0)
		printf("cannot open codec\n");
	dec_ctx = ctx;
	
	return 0;
}

void initiate_audio_device(VideoState *vs){
	SDL_zero(want);
	want.freq = vs->audio_dec_ctx->sample_rate;
	want.format = AUDIO_F32SYS;
	want.channels = 1;
	want.samples = 4096;
	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	SDL_PauseAudioDevice(dev, 0);
}

void decode_packet(VideoState *vs){
	if (vs->pkt.stream_index == vs->audio_stream_idx){
		avcodec_decode_audio4(vs->audio_dec_ctx, vs->frame, &vs->got_frame, &vs->pkt);
		size_t unpadded_linesize = vs->frame->nb_samples * av_get_bytes_per_sample(AV_SAMPLE_FMT_FLTP);
		vs->frame->pts = av_frame_get_best_effort_timestamp(vs->frame);
		vs->frame->pts = av_rescale_q(vs->frame->pts, vs->audio_stream->time_base, AV_TIME_BASE_Q);
		printf("audio_frame queued: %d nb_samples:%d pts:%d\n",
				SDL_GetQueuedAudioSize(dev),
				vs->frame->nb_samples,
				vs->frame->pts);
		SDL_QueueAudio(dev, vs->frame->extended_data[0], unpadded_linesize);
	}
	
	if (&vs->got_frame)
		av_frame_unref(vs->frame);
}

int decode_thread(void *arg){
	VideoState *vs = (VideoState *)arg;
	
	avformat_open_input(&vs->fmt_ctx, vs->src_filename, NULL, NULL);
	avformat_find_stream_info(vs->fmt_ctx, NULL);
	
	av_dump_format(vs->fmt_ctx, 0, vs->src_filename, 0);
	
	open_codec_context(&vs->audio_stream_idx, vs->fmt_ctx, vs->audio_dec_ctx, vs->audio_stream, AVMEDIA_TYPE_AUDIO);
	open_codec_context(&vs->video_stream_idx, vs->fmt_ctx, vs->video_dec_ctx, vs->video_stream, AVMEDIA_TYPE_VIDEO);

	// open audio device
	initiate_audio_device(vs);
	/*
	vs->frame = av_frame_alloc();
	av_init_packet(&vs->pkt);
	vs->pkt.data = NULL;
	vs->pkt.size = 0;
	
	int ret = 0;
	while (av_read_frame(vs->fmt_ctx, &vs->pkt) >= 0) {
        AVPacket orig_pkt = vs->pkt;
        do {
            decode_packet(vs);
            vs->pkt.data += ret;
            vs->pkt.size -= ret;
        } while (vs->pkt.size > 0);
        av_packet_unref(&orig_pkt);
    }
	*/
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

