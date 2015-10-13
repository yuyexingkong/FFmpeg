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

#ifndef  FFPLAYER_QUEUE_HEADER_INC
#define  FFPLAYER_QUEUE_HEADER_INC
#include "ffplayer.h"

extern AVPacket flush_pkt;
typedef struct MyAVPacketList {
    AVPacket pkt;
    struct MyAVPacketList *next;
    int serial;
} MyAVPacketList;


class PacketQueue {
    public:
        MyAVPacketList *first_pkt, *last_pkt;
        int nb_packets;
        int size;
        int abort_request;
        int serial;
        SDL_mutex *mutex;
        SDL_cond *cond;
    public:
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
        FrameQueue(){}
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
        int rindex;
        int windex;
        int size;
        int max_size;
        int keep_last;
        int rindex_shown;
        SDL_mutex *mutex;
        SDL_cond *cond;
        PacketQueue *pktq;
} ;

#endif   /* ----- #ifndef FFPLAYER_QUEUE_HEADER_INC  ----- */
