
/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * simple media player based on the FFmpeg libraries
 */

#ifndef  FFPLAYER_HEADER_INC
#define  FFPLAYER_HEADER_INC

#include "config.h"
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>

extern "C"{
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavutil/fstream.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"

#if CONFIG_AVFILTER
# include "libavfilter/avfilter.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"
#endif

#include <SDL.h>

#include "cmdutils.h"

#include <assert.h>
}

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
    AVFrame *frame;
    AVSubtitle sub;
    int serial;
    double pts;           /* presentation timestamp for the frame */
    double duration;      /* estimated duration of the frame */
    int64_t pos;          /* byte position of the frame in the input file */
    SDL_Overlay *bmp;
    int allocated;
    int reallocate;
    int width;
    int height;
    AVRational sar;
} Frame;


extern AVPacket flush_pkt;
typedef struct MyAVPacketList {
    AVPacket pkt;
    struct MyAVPacketList *next;
    int serial;
} MyAVPacketList;

#include "threads.h"
class PacketQueue {
    public:
        MyAVPacketList *first_pkt = nullptr, *last_pkt = nullptr;
        int nb_packets = 0;
        int size = 0;
        int abort_request = 0;
        int serial = 0;
        Mutex mutex;
        Cond cond;
    public:
        PacketQueue():mutex(),cond(){}
        //PacketQueue(){init();  }
        int put_private(AVPacket *pkt);
        int put(AVPacket *pkt);
        int get(AVPacket *pkt, int block, int *serial);
        void start();
        void abort();
        void destroy();
        void flush();
        void init();
        int put_nullpacket(int stream_index);
};

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

class FrameQueue {
    public:
        FrameQueue():mutex(), cond(){}
    public:
        int init( PacketQueue *pktq, int max_size, int keep_last);
        void destory();
        void signal();
        Frame *peek();
        Frame *peek_next();
        Frame *peek_last();
        Frame *peek_writable();
        Frame *peek_readable();
        void push();
        void next();
        int prev();
        int nb_remaining();
        int64_t last_pos();
    public:
        Frame queue[FRAME_QUEUE_SIZE];
        int rindex =0;
        int windex =0;
        int size =0;
        int max_size = 0;
        int keep_last = 0;
        int rindex_shown = 0;
        Mutex mutex;
        Cond cond;
        PacketQueue *pktq = nullptr;
} ;

typedef enum {
    SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
} ShowMode ;

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

typedef struct AudioParams {
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

class Clock {
    public:
        void set_clock(double pts, int serial);
        double get_clock();
        void set_clock_speed(double speed);
        void set_clock_at(double pts, int serial, double time);
        void init_clock(int *queue_serial);
        void sync_clock_to_slave(Clock *slave);
    public:
        double pts;           /* clock base */
        double pts_drift;     /* clock base minus time at which we updated the clock */
        double last_updated;
        double speed;
        int serial;           /* clock is based on a packet with this serial */
        int paused;
        int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} ;

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};
struct VideoState; 
class AVStreamsParser
{
    public:
        Thread read_thread;
        int subtitle_stream;
        AVStream *subtitle_st;
        PacketQueue subtitleq;

        int audio_stream;
        AVStream *audio_st;
        PacketQueue audioq;

        int video_stream;
        AVStream *video_st;
        PacketQueue videoq;

        int last_video_stream, last_audio_stream, last_subtitle_stream;

        AVFormatContext *ic;

        char filename[1024];
        int eof;
    public:
        static int read_thread_func(void *arg);
        int  stream_component_open(VideoState *is, int stream_index);
        void stream_component_close(VideoState *is, int stream_index);
        void stream_cycle_channel(VideoState *is, int codec_type);
        static int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params);
};

#include "ffdecoder.h"
#include "control.h"
#include "display.h"

// manage the 3 decoder: for video/audio/subtitle
class DecoderManager{

    public:
        AVStreamsParser streamsParser;

        FrameQueue pictureQ;
        FrameQueue subtitleQ;
        FrameQueue audioSampleQ;

        AudioDecoder audioDecoder;
        VideoDecoder videoDecoder;
        SubtitleDecoder subtitleDecoder;

        int viddec_width;
        int viddec_height;
};

class VideoState {
    public:
        VideoState(){ 
            controller.Set(this);
        }
    public:
        AVInputFormat *iformat;
        FFPlayController controller;
        int realtime;

        Clock audclk;
        Clock vidclk;
        Clock extclk;

        DecoderManager decoders;

        FrameQueue& pictq() { return decoders.pictureQ;}
        FrameQueue& subpq() { return decoders.subtitleQ;}
        FrameQueue& sampq() { return decoders.audioSampleQ;}
        AudioDecoder& auddec(){return decoders.audioDecoder;};
        VideoDecoder& viddec(){return decoders.videoDecoder;};
        SubtitleDecoder& subdec() {return decoders.subtitleDecoder;};

        int av_sync_type;

        double audio_clock;
        int audio_clock_serial;
        double audio_diff_cum; /* used for AV difference average computation */
        double audio_diff_avg_coef;
        double audio_diff_threshold;
        int audio_diff_avg_count;
        int audio_hw_buf_size;
        uint8_t silence_buf[SDL_AUDIO_MIN_BUFFER_SIZE];
        uint8_t *audio_buf;
        uint8_t *audio_buf1;
        unsigned int audio_buf_size; /* in bytes */
        unsigned int audio_buf1_size;
        int audio_buf_index; /* in bytes */
        int audio_write_buf_size;
        struct AudioParams audio_src;
#if CONFIG_AVFILTER
        struct AudioParams audio_filter_src;
#endif
        struct AudioParams audio_tgt;
        struct SwrContext *swr_ctx;
        int frame_drops_early;
        int frame_drops_late;

        ShowMode show_mode;
        int16_t sample_array[SAMPLE_ARRAY_SIZE];
        int sample_array_index;
        int last_i_start;
        RDFTContext *rdft;
        int rdft_bits;
        FFTSample *rdft_data;
        int xpos;
        double last_vis_time;

        double frame_timer;
        double frame_last_returned_time;
        double frame_last_filter_delay;
        double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
#if !CONFIG_AVFILTER
        struct SwsContext *img_convert_ctx;
#endif
        struct SwsContext *sub_convert_ctx;
        SDL_Rect last_display_rect;

        int width, height, xleft, ytop;
        Display display;
        int step;

#if CONFIG_AVFILTER
        int vfilter_idx;
        AVFilterContext *in_video_filter;   // the first filter in the video chain
        AVFilterContext *out_video_filter;  // the last filter in the video chain
        AVFilterContext *in_audio_filter;   // the first filter in the audio chain
        AVFilterContext *out_audio_filter;  // the last filter in the audio chain
        AVFilterGraph *agraph;              // audio filter graph
#endif


        Cond continue_read_thread;

    public:
        AVStreamsParser* getAVStreamsParser() { return &(decoders.streamsParser);};
        AVFormatContext *getAVFormatContext() { return getAVStreamsParser()->ic;} 
        FFPlayController* getController(){ return &controller;}
        Display* getDisplay() { return &display;}
};

int get_master_sync_type(VideoState *is); 
double get_master_clock(VideoState *is);
void check_external_clock_speed(VideoState *is);
void do_exit(VideoState *is);

    static inline
int64_t get_valid_channel_layout(int64_t channel_layout, int channels)
{
    if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
        return channel_layout;
    else
        return 0;
}


#define FF_ALLOC_EVENT   (SDL_USEREVENT)
#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)
/* options specified by the user */
class Options{
    public:
        AVInputFormat *file_iformat;
        const char *input_filename;
        const char *window_title;
        int fs_screen_width;
        int fs_screen_height;
        int screen_width  = 0;
        int screen_height = 0;
        int audio_disable;
        int video_disable;
        int subtitle_disable;
        const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = {0};
        int seek_by_bytes = -1;
        int display_disable;
        int show_status = 1;
        int av_sync_type = AV_SYNC_AUDIO_MASTER;
        int64_t start_time = AV_NOPTS_VALUE;
        int64_t duration = AV_NOPTS_VALUE;
        int fast = 0;
        int genpts = 0;
        int lowres = 0;
        int decoder_reorder_pts = -1;
        int autoexit;
        int exit_on_keydown;
        int exit_on_mousedown;
        int loop = 1;
        int framedrop = -1;
        int infinite_buffer = -1;
        ShowMode show_mode = SHOW_MODE_NONE;
        const char *audio_codec_name;
        const char *subtitle_codec_name;
        const char *video_codec_name;
        double rdftspeed = 0.02;
        int64_t cursor_last_shown;
        int cursor_hidden = 0;
#if CONFIG_AVFILTER
        const char **vfilters_list = NULL;
        int nb_vfilters = 0;
        char *afilters = NULL;
#endif
        int autorotate = 1;
        int is_full_screen;
        int64_t audio_callback_time;
};

extern Options gOptions;
#endif   /* ----- #ifndef FFPLAYER_HEADER_INC  ----- */
