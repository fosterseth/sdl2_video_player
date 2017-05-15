//gcc -w -o play_audio play_audio.c -I/usr/local/include -L/usr/local/lib -llibavcodec -llibavformat -llibavutil `sdl2-config --cflags --libs`
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <SDL2/SDL.h>

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *audio_dec_ctx = NULL;
static AVStream *audio_stream = NULL;
static AVFrame *frame = NULL;
static AVPacket pkt;
static AVCodec *dec = NULL;
static int audio_stream_idx = -1;
static const char *src_filename = NULL;
int main(int argc, char **argv){
    
    src_filename = argv[1];
    
    av_register_all();
    
    SDL_Init(SDL_INIT_AUDIO);
    
    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }
    
    
    audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    dec = avcodec_find_decoder(fmt_ctx->streams[audio_stream_idx]->codec->codec_id);
    
    /* dump input information to stderr */
    av_dump_format(fmt_ctx, 0, src_filename, 0);
    
    frame = av_frame_alloc();
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    
    
    return 0;
}