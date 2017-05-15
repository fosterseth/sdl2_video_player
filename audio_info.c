#include <stdio.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <SDL2/SDL.h>

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *audio_dec_ctx;

static AVStream *audio_stream = NULL;
static const char *src_filename = NULL;

static const char *audio_dst_filename = NULL;
static FILE *audio_dst_file = NULL;


static int audio_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;
static int audio_frame_count = 0;
static int refcount = 0;
static int open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type)
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

void printAudioFrameInfo(const AVCodecContext* codecContext, const AVFrame* frame){
	printf("Audio frame info:\n"
	"\tSample count: %d\n"
	"\tChannel count: %d\n"
	"\tFormat: %s\n"
	"\tBytes per sample: %d\n"
	"\tIs planar? %d\n", 
	frame->nb_samples,
	codecContext->channels,
	av_get_sample_fmt_name(codecContext->sample_fmt),
	av_get_bytes_per_sample(codecContext->sample_fmt),
	av_sample_fmt_is_planar(codecContext->sample_fmt));
}

int main(int argc, char *argv[]){
	SDL_AudioSpec want, have;
	SDL_AudioDeviceID dev;
	
	int ret = 0, got_frame;
	SDL_Init(SDL_INIT_AUDIO);
	
	/* register all formats and codecs */
    av_register_all();

    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, argv[1], NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", argv[1]);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }


    if (open_codec_context(&audio_stream_idx, fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0) {
        audio_stream = fmt_ctx->streams[audio_stream_idx];
        audio_dec_ctx = audio_stream->codec;
    }

    /* dump input information to stderr */
    av_dump_format(fmt_ctx, 0, src_filename, 0);
	
	printf("Sample Rate : %d\n", audio_dec_ctx->sample_rate);
	SDL_zero(want);
	want.freq = audio_dec_ctx->sample_rate;
	want.format = AUDIO_S16;
	want.channels = 1;
	want.samples = 4096;
	
	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	printf("dev: %d\n", dev);
	if (dev < 0)
		printf("Failed to open audio: %s\n", SDL_GetError());
	
	SDL_PauseAudioDevice(dev, 0);

    if (!audio_stream) {
        fprintf(stderr, "Could not find audio stream in the input, aborting\n");
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
		exit(1);
    }

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
	
    /* read frames from the file */
	//for (;;){
	int i;
	while (av_read_frame(fmt_ctx, &pkt) >= 0){
		if (pkt.stream_index == audio_stream_idx){
			AVPacket orig_pkt = pkt;
			while (orig_pkt.size > 0){
				got_frame = 0;
				int result = avcodec_decode_audio4(audio_dec_ctx, frame, &got_frame, &orig_pkt);
				
				if (result >= 0 && got_frame){
					orig_pkt.size -= result;
					orig_pkt.data += result;
					
					//printAudioFrameInfo(audio_dec_ctx, frame);
					SDL_QueueAudio(dev, frame->data[0], 3000);
				}else{
					orig_pkt.size = 0;
					orig_pkt.data = NULL;
				}
			}
		}
		av_free_packet(&pkt);
	}
	int q;
	while (q = SDL_GetQueuedAudioSize(dev) > 0)
		printf("%d\n", q);
	//}
	
	av_frame_free(&frame);
	avcodec_close(audio_dec_ctx);
	avformat_close_input(&fmt_ctx);

	//SDL_CloseAudioDevice(dev);
    return 1;
}
	
