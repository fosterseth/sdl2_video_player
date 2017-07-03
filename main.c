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
    AVFormatContext *fmt_ctx;
    AVCodecContext *video_dec_ctx;
    AVCodecContext *audio_dec_ctx;
    AVStream *video_stream;
    AVStream *audio_stream;
    AVPacket pkt;
    AVFrame *frame;
    SwrContext *swr;
    int64_t last_audio_pts;
    int64_t last_video_pts;
    int64_t current_video_pts;
    int64_t interval;
    int64_t delay;
    int64_t queued_size;
    int64_t queued_ms;
    int set_swrContext;
    int bytes_per_sample;
    int audio_stream_idx;
    int video_stream_idx;
    int got_frame;
    int video_frame_count;
    int frame_total;
    int master_audio;
    SDL_Renderer *Renderer;
    SDL_Window *Window;
    SDL_Texture *Texture;
    SDL_Thread *thread;
    SDL_TimerID timer_dF;
    SDL_TimerID timer_qA;
    SDL_TimerID timer_printPTS;
    PacketQueue videoqueue;
    PacketQueue audioqueue;
} VideoState;
SDL_AudioSpec want, have;
SDL_AudioDeviceID dev;
int quit_signal;
int64_t last_audio_pts;


void PrintEvent(const SDL_Event * event){
    if (event->type == SDL_WINDOWEVENT) {
        switch (event->window.event) {
        case SDL_WINDOWEVENT_SHOWN:
            SDL_Log("Window %d shown", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_HIDDEN:
            SDL_Log("Window %d hidden", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_EXPOSED:
            SDL_Log("Window %d exposed", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_MOVED:
            SDL_Log("Window %d moved to %d,%d",
                    event->window.windowID, event->window.data1,
                    event->window.data2);
            break;
        case SDL_WINDOWEVENT_RESIZED:
            SDL_Log("Window %d resized to %dx%d",
                    event->window.windowID, event->window.data1,
                    event->window.data2);
            break;
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            SDL_Log("Window %d size changed to %dx%d",
                    event->window.windowID, event->window.data1,
                    event->window.data2);
            break;
        case SDL_WINDOWEVENT_MINIMIZED:
            SDL_Log("Window %d minimized", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_MAXIMIZED:
            SDL_Log("Window %d maximized", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_RESTORED:
            SDL_Log("Window %d restored", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_ENTER:
            SDL_Log("Mouse entered window %d",
                    event->window.windowID);
            break;
        case SDL_WINDOWEVENT_LEAVE:
            SDL_Log("Mouse left window %d", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            SDL_Log("Window %d gained keyboard focus",
                    event->window.windowID);
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            SDL_Log("Window %d lost keyboard focus",
                    event->window.windowID);
            break;
        case SDL_WINDOWEVENT_CLOSE:
            SDL_Log("Window %d closed", event->window.windowID);
            quit_signal = 1;
            break;
#if SDL_VERSION_ATLEAST(2, 0, 5)
        case SDL_WINDOWEVENT_TAKE_FOCUS:
            SDL_Log("Window %d is offered a focus", event->window.windowID);
            break;
        case SDL_WINDOWEVENT_HIT_TEST:
            SDL_Log("Window %d has a special hit test", event->window.windowID);
            break;
#endif
        default:
            SDL_Log("Window %d got unknown event %d",
                    event->window.windowID, event->window.event);
            break;
        }
    }
//    if (event->type == SDL_MOUSEBUTTONDOWN){
//        printPTS(vs);
//    }
}


static int open_codec_context(int *stream_idx,
                                AVFormatContext *fmt_ctx,
                                enum AVMediaType type){
    int ret, stream_index;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file\n", av_get_media_type_string(type));
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

    return ret;
}


void initiate_audio_device(){
	SDL_zero(want);
	want.freq = 48000; //vs->audio_dec_ctx->sample_rate;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 4096;
	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	SDL_PauseAudioDevice(dev, 0);
}

void initiate_renderer_window(VideoState *vs, int x_pos, int y_pos){
    int width = vs->video_dec_ctx->width;
	int height = vs->video_dec_ctx->height;
	int ret = 0;
	// create window
	vs->Window = SDL_CreateWindow(
			"MOVIE",
			x_pos,
			y_pos,
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

int decode_packet(VideoState *vs){
    int decoded = vs->pkt.size;
    int ret = 0;
//    if (0){
	if (vs->pkt.stream_index == vs->audio_stream_idx & vs->master_audio == 1){
//        printf("audio packet %s\n", vs->src_filename);
        packet_queue_put(&vs->audioqueue, &vs->pkt);
    } else if (vs->pkt.stream_index == vs->video_stream_idx){
//        printf("video packet %s\n", vs->src_filename);
        packet_queue_put(&vs->videoqueue, &vs->pkt);
    }
	
	if (vs->got_frame)
		av_frame_unref(vs->frame);
        
    return decoded;
}

int decode_thread(void *arg){
    VideoState *vs = (VideoState *)arg;
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
//        av_packet_unref(&orig_pkt);
    }
    if (vs->master_audio){
        while(SDL_GetQueuedAudioSize(dev) > 0){
            continue;
        }
    }

    while(vs->videoqueue.nb_packets > 0)
        continue;

	return 0;
}

int displayFrame(VideoState *vs){
    /* decode video frame */
    AVPacket pkt;
    int gotframe;
    AVFrame *frame;
    frame = av_frame_alloc();
    if (packet_queue_get(&vs->videoqueue, &pkt, 0)){
        avcodec_decode_video2(vs->video_dec_ctx, frame, &gotframe, &pkt);
        if (gotframe) {
            frame->pts = av_frame_get_best_effort_timestamp(frame);
            vs->current_video_pts = av_rescale_q(frame->pts, vs->video_stream->time_base, AV_TIME_BASE_Q);
            SDL_UpdateYUVTexture(vs->Texture,
                                    NULL,
                                    frame->data[0],
                                    frame->linesize[0],
                                    frame->data[1],
                                    frame->linesize[1],
                                    frame->data[2],
                                    frame->linesize[2]);
            SDL_RenderCopy(vs->Renderer, vs->Texture, NULL, NULL);
            SDL_RenderPresent(vs->Renderer);
        }
    }
    //av_packet_unref(&pkt);
    //av_free(frame);
	return 0;
}

int queueAudio(VideoState *vs){
    AVPacket pkt;
    int gotframe;
    AVFrame *frame;
    int ret, decoded;
    frame = av_frame_alloc();
    if (packet_queue_get(&vs->audioqueue, &pkt, 0)){
        ret = avcodec_decode_audio4(vs->audio_dec_ctx, frame, &gotframe, &pkt);
        decoded = FFMIN(ret, pkt.size);
        if (gotframe){
            if (vs->set_swrContext){
                // initiate resample context
                SwrContext *swr = swr_alloc();
                av_opt_set_channel_layout(swr, "in_channel_layout",  frame->channel_layout, 0);
                av_opt_set_channel_layout(swr, "out_channel_layout", AV_CH_LAYOUT_STEREO,  0);
                av_opt_set_int(swr, "in_sample_rate",     frame->sample_rate,                0);
                av_opt_set_int(swr, "out_sample_rate",    48000,                0);
                av_opt_set_sample_fmt(swr, "in_sample_fmt",  frame->format, 0);
                av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
                swr_init(swr);
                vs->swr = swr;
                vs->set_swrContext = 0;
            }
            
            frame->pts = av_frame_get_best_effort_timestamp(frame);
            last_audio_pts = av_rescale_q(frame->pts, (AVRational){1,48000}, AV_TIME_BASE_Q);
            vs->last_audio_pts = last_audio_pts; 
            uint8_t *output;
//            int out_samples = av_rescale_rnd(swr_get_delay(vs->swr, 48000) + frame->nb_samples, 44100, 48000, AV_ROUND_UP);
            int out_samples = frame->nb_samples;
            av_samples_alloc(&output, NULL, 2, out_samples, AV_SAMPLE_FMT_S16, 0);
            out_samples = swr_convert(vs->swr, &output, out_samples, frame->data, frame->nb_samples);
            
            size_t unpadded_linesize = out_samples * vs->bytes_per_sample;
            SDL_QueueAudio(dev, output, unpadded_linesize*2);
//            printf("just_queued    %d     bytes    %d\n", SDL_GetQueuedAudioSize(vs->dev), unpadded_linesize * 2);
            av_freep(&output);
        }
    }
   // av_packet_unref(&pkt);
    //av_free(frame);
    return 0;
}

Uint32 callback_queueAudio(Uint32 interval, void *param){
    VideoState *vs = (VideoState *) param;
    SDL_Event event;
    SDL_UserEvent userevent;

    /* In this example, our callback pushes an SDL_USEREVENT event
    into the queue, and causes our callback to be called again at the
    same interval: */

    userevent.type = SDL_USEREVENT;
    userevent.code = 2;
    userevent.data1 = NULL;
    userevent.data2 = NULL;
    event.user = userevent;
    event.type = SDL_USEREVENT;
    
    if ( vs->queued_ms < 1000 ) // milliseconds of queuedAudio
        queueAudio(vs);

    return (interval);
}

Uint32 callback_displayFrame(Uint32 interval, void *param){
    VideoState *vs = (VideoState *) param;
    SDL_Event event;
    SDL_UserEvent userevent;

    /* In this example, our callback pushes an SDL_USEREVENT event
    into the queue, and causes our callback to be called again at the
    same interval: */

    userevent.type = SDL_USEREVENT;
    userevent.code = 1;
    userevent.data1 = NULL;
    userevent.data2 = NULL;
    event.user = userevent;
    event.type = SDL_USEREVENT;

    Uint32 queued_size = SDL_GetQueuedAudioSize(dev);
    int64_t queued_ns = 0;
    int64_t queued_ms = 0;
    if (vs->bytes_per_sample > 0)
        queued_ns = (int64_t) ((double) queued_size / 2.0 / (double) vs->bytes_per_sample / 48000.0 * 1000000);
    vs->queued_ms = (int64_t) queued_ns / 1000;
    vs->queued_size = queued_size;
    
    vs->delay = (last_audio_pts - queued_ns - vs->current_video_pts) / 1000;
    vs->frame_total += 1;
    Uint32 new_interval = (Uint32) ((vs->current_video_pts - vs->last_video_pts) / 1000);
    if (new_interval > 0)
        interval = new_interval;
    vs->last_video_pts = vs->current_video_pts;
    if (vs->delay < -100)
        interval = interval + 3;
    if (vs->delay > 100)
        interval = interval - 3;
    
    vs->interval = interval;
//    SDL_PushEvent(&event);
    displayFrame(vs);
    if (vs->master_audio == 1)
        queueAudio(vs);
    return (interval);
}

Uint32 printPTS(Uint32 interval, void *arg){
    VideoState *vs = (VideoState *) arg;
    if (vs->interval > 0){
        printf("interval %d   last_audio %d   current_video %d   delay %d\n", vs->interval, last_audio_pts, vs->current_video_pts, vs->delay);
        printf("queued_size %d    queued_ms %d\n", vs->queued_size, vs->queued_ms);
    }
    return interval;
}


int main(int argc, char **argv){
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_EVENTS);
    av_register_all();

    int v;
    int x_pos = 500;
    int y_pos = 200;
    for (v=0; v < argc-1; v++){
        VideoState *vs;
        vs = av_mallocz(sizeof(VideoState));
        av_strlcpy(vs->src_filename, argv[v+1], sizeof(vs->src_filename));
        vs->frame_total = 0;
        
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
        if (v == 0 & vs->audio_stream_idx > -1){
            vs->master_audio = 1;
            packet_queue_init(&vs->audioqueue);
            initiate_audio_device(vs);
        }
        // open renderer window
        initiate_renderer_window(vs, x_pos, y_pos);
        x_pos += 50;
        y_pos += 50;
        // initiate packetqueue
        packet_queue_init(&vs->videoqueue);
        
        
        vs->last_video_pts = 0;
        vs->frame = av_frame_alloc();
        av_init_packet(&vs->pkt);
        vs->pkt.data = NULL;
        vs->pkt.size = 0;
        vs->bytes_per_sample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
        vs->set_swrContext = 1;
        vs->thread = SDL_CreateThread(decode_thread, "decoder", vs);
        vs->timer_dF = SDL_AddTimer(33, callback_displayFrame, (void *) vs);
        if (vs->master_audio){
            vs->timer_qA = SDL_AddTimer(20, callback_queueAudio, (void *) vs);
        }
        //vs->timer_printPTS = SDL_AddTimer(1000, printPTS, (void *) vs);
    }
    
    SDL_Event event;
    quit_signal = 0;
    while (quit_signal == 0){
        SDL_Delay(20);
        if(SDL_PollEvent(&event)){
            PrintEvent(&event);
        }
    }
    
    
    
	SDL_Quit();
	return 0;
}