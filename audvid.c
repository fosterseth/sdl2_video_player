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

static SDL_AudioSpec want, have;
static SDL_AudioDeviceID dev;

static AVFormatContext *fmt_ctx = NULL;
static SwrContext *swr;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
static int width, height;
static enum AVPixelFormat pix_fmt;
static AVStream *video_stream = NULL, *audio_stream = NULL;
static const char *src_filename = NULL;
static const char *video_dst_filename = NULL;
static const char *audio_dst_filename = NULL;
static FILE *video_dst_file = NULL;
static FILE *audio_dst_file = NULL;

static uint8_t *video_dst_data[4] = {NULL};
static int      video_dst_linesize[4];
static int video_dst_bufsize;

static int video_stream_idx = -1, audio_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;
static AVPacket *vpkt;
static int video_frame_count = 0;
static int audio_frame_count = 0;
static int refcount = 0;

static int enable_audio = 1;
static int ptsprev;
int got_frame = 0;

SDL_Renderer *Renderer;
SDL_Window *Window;
SDL_Texture *Texture;
SDL_Thread *thread;

typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;

PacketQueue *videoqueue;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

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
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
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
}


static int open_codec_context(int *stream_idx,
                              AVFormatContext *fmt_ctx,
							  enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), src_filename);
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

        /* Init the decoders, with or without reference counting */
        av_dict_set(&opts, "refcounted_frames", refcount ? "1" : "0", 0);
        if ((ret = avcodec_open2(dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

static int displayFrame(void *param){
	for (;;){
		if (packet_queue_get(videoqueue, vpkt, 0)){
				/* decode video frame */
			avcodec_decode_video2(video_dec_ctx, frame, &got_frame, vpkt);

			if (got_frame) {
				frame->pts = av_frame_get_best_effort_timestamp(frame);
				frame->pts = av_rescale_q(frame->pts, video_stream->time_base, AV_TIME_BASE_Q);
				printf("video_frame n:%d coded_n:%d pts:%d\n",
					   video_frame_count++,
					   frame->coded_picture_number,
					   frame->pts);
				SDL_UpdateYUVTexture(Texture,
										NULL,
										frame->data[0],
										frame->linesize[0],
										frame->data[1],
										frame->linesize[1],
										frame->data[2],
										frame->linesize[2]);
				SDL_RenderCopy(Renderer, Texture, NULL, NULL);
				SDL_RenderPresent(Renderer);
			}
		}
	}
	return 0;
}
static int decode_packet(int cached)
{
	if (pkt.stream_index == video_stream_idx){
		//packet_queue_put(videoqueue, &pkt);
		printf("video_frame\n");
    } else if (pkt.stream_index == audio_stream_idx) {
		if(enable_audio > 0){
			/* decode audio frame */
			avcodec_decode_audio4(audio_dec_ctx, frame, &got_frame, &pkt);
			
			size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(AV_SAMPLE_FMT_FLTP);
			frame->pts = av_frame_get_best_effort_timestamp(frame);
            frame->pts = av_rescale_q(frame->pts, audio_stream->time_base, AV_TIME_BASE_Q);
			printf("audio_frame queued: %d n:%d nb_samples:%d pts:%d\n",
					SDL_GetQueuedAudioSize(dev),
					audio_frame_count++,
					frame->nb_samples,
					frame->pts);
			SDL_QueueAudio(dev, frame->extended_data[0], unpadded_linesize);
		}
	}
	
	if (&got_frame && refcount)
		av_frame_unref(frame);

	return 0;
}

int main(int argc, char **argv){

    int ret = 0;
	
    src_filename = argv[1];
    
    av_register_all();
    
	if (enable_audio > 0){
		printf("%d\n", enable_audio);
		SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_EVENTS);
	}else{
		printf("audio disabled\n");
		SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS);
	}
	
	//packet_queue_init(videoqueue);
	
    // open input file, and allocate format context
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }
	
	// retrieve stream information
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }
	
	// find best audio/video streams and codecs
	if (enable_audio > 0){
		printf("%d\n", enable_audio);
		if (open_codec_context(&audio_stream_idx, fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0) {
			audio_stream = fmt_ctx->streams[audio_stream_idx];
			audio_dec_ctx = audio_stream->codec;
		}
	}
	
	if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
        video_stream = fmt_ctx->streams[video_stream_idx];
        video_dec_ctx = video_stream->codec;
    }
	
	av_dump_format(fmt_ctx, 0, src_filename, 0);
	
	if (enable_audio > 0){
		// open audio device
		SDL_zero(want);
		want.freq = audio_dec_ctx->sample_rate;
		want.format = AUDIO_F32SYS;
		want.channels = 1;
		want.samples = 4096;
		
		dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
		printf("dev: %d\n", dev);
		if (dev < 0)
			printf("Failed to open audio: %s\n", SDL_GetError());
		
		SDL_PauseAudioDevice(dev, 0);
	}
	width = video_dec_ctx->width;
	height = video_dec_ctx->height;
	
	// create window
	Window = SDL_CreateWindow(
			"MOVIE",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			width,
			height,
			SDL_WINDOW_RESIZABLE);
			
			
	width = video_dec_ctx->width;
	height = video_dec_ctx->height;
	
	if (!Window){
		fprintf(stderr, "Count not open window, aborting\n");
		ret = 1;
		goto end;
	}
	
	Renderer = SDL_CreateRenderer(Window, -1, 0);
	
	if (!Renderer){
		fprintf(stderr, "Count not open renderer, aborting\n");
		ret = 1;
		goto end;
	}
	
	Texture = SDL_CreateTexture(Renderer,
								SDL_PIXELFORMAT_IYUV,
								SDL_TEXTUREACCESS_STREAMING,
								width,
								height
								);
						
	SDL_SetRenderDrawColor( Renderer, 0, 0, 0, SDL_ALPHA_OPAQUE );
	SDL_RenderClear( Renderer );
	
	
	if (enable_audio > 0){
		if (!audio_stream) {
			fprintf(stderr, "Could not find audio stream in the input, aborting\n");
			ret = 1;
			goto end;
		}
	}

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }
	
	// initialize packet, set data to NULL, let the demuxer fill it
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
	
	//thread = SDL_CreateThread(displayFrame, "display_frame", (void *) NULL);
	
	// read frames from the file
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        AVPacket orig_pkt = pkt;
        do {
            ret = decode_packet(0);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        } while (pkt.size > 0);
        av_packet_unref(&orig_pkt);
    }
	
    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    do {
        decode_packet(1);
    } while (got_frame);
	
	if (enable_audio > 0){
		while(SDL_GetQueuedAudioSize(dev) > 0){
			continue;
		}
	}
end:
	SDL_Quit();
	avcodec_close(video_dec_ctx);
	if (enable_audio > 0)
		avcodec_close(audio_dec_ctx);
	avformat_close_input(&fmt_ctx);
	
	return 0;
}
  
  
  
  