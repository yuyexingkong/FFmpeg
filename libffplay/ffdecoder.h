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

#ifndef  FFPLAYER_FFDECODER_HEADER_INC
#define  FFPLAYER_FFDECODER_HEADER_INC
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
#include <SDL_thread.h>

#include "cmdutils.h"

#include <assert.h>
}


class PacketQueue;
class FrameQueue;
class VideoState;
class Decoder {
    public:
        void init(AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond);
        int decode_frame(AVFrame *frame, AVSubtitle *sub);
        void destroy() ;
        void abort(FrameQueue *fq);
        void start(int (*fn)(void *), void *arg);
    public:
    AVPacket pkt;
    AVPacket pkt_temp;
    PacketQueue *queue;
    AVCodecContext *avctx;
    int pkt_serial;
    int finished;
    int packet_pending;
    SDL_cond *empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
    SDL_Thread *decoder_tid;
} ;

class AudioDecoder :public Decoder{
    public:
        void init(AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond, VideoState* is);
        static int audio_thread(void *arg);
};

class VideoDecoder :public Decoder{
    public:
        void init(AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond, VideoState* is){
            Decoder::init(avctx, queue, empty_queue_cond);
            start(video_thread, is);
        }
        static int video_thread(void *arg);
};

class SubtitleDecoder :public Decoder{
    public:
        void init(AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond, VideoState* is){
            Decoder::init(avctx, queue, empty_queue_cond);
            start(subtitle_thread, is);
        }
        static int subtitle_thread(void *arg);
};

#endif   /* ----- #ifndef FFPLAYER_FFDECODER_HEADER_INC  ----- */
