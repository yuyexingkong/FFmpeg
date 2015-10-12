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
#include "ffplayer.h"

int PacketQueue::put_private(AVPacket *pkt)
{
    MyAVPacketList *pkt1;
    PacketQueue *q = this; 

    if (q->abort_request)
       return -1;

    pkt1 = (MyAVPacketList *) av_malloc(sizeof(MyAVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    if (pkt == &flush_pkt)
        q->serial++;
    pkt1->serial = q->serial;

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    /* XXX: should duplicate packet data in DV case */
    SDL_CondSignal(q->cond);
    return 0;
}

int PacketQueue::put(AVPacket *pkt)
{
    int ret;
    PacketQueue *q = this;

    /* duplicate the packet */
    if (pkt != &flush_pkt && av_dup_packet(pkt) < 0)
        return -1;

    SDL_LockMutex(q->mutex);
    ret = put_private(pkt);
    SDL_UnlockMutex(q->mutex);

    if (pkt != &flush_pkt && ret < 0)
        av_free_packet(pkt);

    return ret;
}

int PacketQueue::put_nullpacket(int stream_index)
{
    AVPacket pkt1, *pkt = &pkt1;
    PacketQueue *q = this;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return put(pkt);
}

/* packet queue handling */
void PacketQueue::init()
{
    PacketQueue *q = this;
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
    q->abort_request = 1;
}

void PacketQueue::flush()
{
    MyAVPacketList *pkt, *pkt1;
    PacketQueue *q = this;

    SDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_free_packet(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    SDL_UnlockMutex(q->mutex);
}

void PacketQueue::destroy()
{
    PacketQueue *q = this;
    q->flush();
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

void PacketQueue::abort()
{
    PacketQueue *q = this;
    SDL_LockMutex(q->mutex);

    q->abort_request = 1;

    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
}

void PacketQueue::start()
{
    PacketQueue *q = this;
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    q->put_private(&flush_pkt);
    SDL_UnlockMutex(q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
int PacketQueue::get(AVPacket *pkt, int block, int *serial)
{
    MyAVPacketList *pkt1;
    int ret;
    PacketQueue *q = this;

    SDL_LockMutex(q->mutex);

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            *pkt = pkt1->pkt;
            if (serial)
                *serial = pkt1->serial;
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

static void frame_queue_unref_item(Frame *vp)
{
    av_frame_unref(vp->frame);
    avsubtitle_free(&vp->sub);
}

int FrameQueue::init(PacketQueue *pktq, int max_size, int keep_last)
{
    int i;
    FrameQueue *f = this;
    memset(f, 0, sizeof(FrameQueue));
    if (!(f->mutex = SDL_CreateMutex()))
        return AVERROR(ENOMEM);
    if (!(f->cond = SDL_CreateCond()))
        return AVERROR(ENOMEM);
    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    f->keep_last = !!keep_last;
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

void free_picture(Frame *vp);
void FrameQueue::destory()
{
    int i;
    FrameQueue *f = this;
    for (i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        frame_queue_unref_item(vp);
        av_frame_free(&vp->frame);
        free_picture(vp);
    }
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);
}

void FrameQueue::signal()
{
    FrameQueue *f = this;
    SDL_LockMutex(f->mutex);
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

Frame *FrameQueue::peek()
{
    FrameQueue *f = this;
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

Frame *FrameQueue::peek_next()
{
    FrameQueue *f = this;
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

Frame *FrameQueue::peek_last()
{
    FrameQueue *f = this;
    return &f->queue[f->rindex];
}

Frame *FrameQueue::peek_writable()
{
    FrameQueue *f = this;
    /* wait until we have space to put a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size >= f->max_size &&
           !f->pktq->abort_request) {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[f->windex];
}

Frame *FrameQueue::peek_readable()
{
    FrameQueue *f = this;
    /* wait until we have a readable a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size - f->rindex_shown <= 0 &&
           !f->pktq->abort_request) {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

void FrameQueue::push()
{
    FrameQueue *f = this;
    if (++f->windex == f->max_size)
        f->windex = 0;
    SDL_LockMutex(f->mutex);
    f->size++;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

void FrameQueue::next()
{
    FrameQueue *f = this;
    if (f->keep_last && !f->rindex_shown) {
        f->rindex_shown = 1;
        return;
    }
    frame_queue_unref_item(&f->queue[f->rindex]);
    if (++f->rindex == f->max_size)
        f->rindex = 0;
    SDL_LockMutex(f->mutex);
    f->size--;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

/* jump back to the previous frame if available by resetting rindex_shown */
int FrameQueue::prev()
{
    FrameQueue *f = this;
    int ret = f->rindex_shown;
    f->rindex_shown = 0;
    return ret;
}

/* return the number of undisplayed frames in the queue */
int FrameQueue::nb_remaining()
{
    FrameQueue *f = this;
    return f->size - f->rindex_shown;
}

/* return last shown position */
int64_t FrameQueue::last_pos()
{
    FrameQueue *f = this;
    Frame *fp = &f->queue[f->rindex];
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1;
}

static int decode_interrupt_cb(void *ctx)
{
    VideoState *is = (VideoState *)ctx;
    return is->getController()->abort_request;
}

static int is_realtime(AVFormatContext *s)
{
    if(   !strcmp(s->iformat->name, "rtp")
       || !strcmp(s->iformat->name, "rtsp")
       || !strcmp(s->iformat->name, "sdp")
    )
        return 1;

    if(s->pb && (   !strncmp(s->filename, "rtp:", 4)
                 || !strncmp(s->filename, "udp:", 4)
                )
    )
        return 1;
    return 0;
}

# define INT32_MIN              (-2147483647-1)
# define INT64_MIN              (-9223372036854775807-1)
# define INT32_MAX              (2147483647)
# define INT64_MAX              (9223372036854775807)
/* this thread gets the stream from the disk or the network */
int AVStreamsParser::read_thread(void *arg)
{
    VideoState *is = (VideoState*)arg;
    AVStreamsParser* ps = is->getAVStreamsParser();
    FFPlayController* ctrl = is->getController();

    AVFormatContext *ic = NULL;
    int err, i, ret;
    int st_index[AVMEDIA_TYPE_NB];
    AVPacket pkt1, *pkt = &pkt1;
    int64_t stream_start_time;
    int pkt_in_play_range = 0;
    AVDictionaryEntry *t;
    AVDictionary **opts;
    int orig_nb_streams;
    SDL_mutex *wait_mutex = SDL_CreateMutex();
    int scan_all_pmts_set = 0;
    int64_t pkt_ts;

    memset(st_index, -1, sizeof(st_index));
    ps->last_video_stream = ps->video_stream = -1;
    ps->last_audio_stream = ps->audio_stream = -1;
    ps->last_subtitle_stream = ps->subtitle_stream = -1;
    ps->eof = 0;

    ic = avformat_alloc_context();
    if (!ic) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = is;
    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    err = avformat_open_input(&ic, ps->filename, is->iformat, &format_opts);
    if (err < 0) {
        print_error(ps->filename, err);
        ret = -1;
        goto fail;
    }
    if (scan_all_pmts_set)
        av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

    if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret = AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }
    ps->ic = ic;

    if (gOptions.genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;

    av_format_inject_global_side_data(ic);

    opts = setup_find_stream_info_opts(ic, codec_opts);
    orig_nb_streams = ic->nb_streams;

    err = avformat_find_stream_info(ic, opts);

    for (i = 0; i < orig_nb_streams; i++)
        av_dict_free(&opts[i]);
    av_freep(&opts);

    if (err < 0) {
        av_log(NULL, AV_LOG_WARNING,
               "%s: could not find codec parameters\n", ps->filename);
        ret = -1;
        goto fail;
    }

    if (ic->pb)
        ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    if (gOptions.seek_by_bytes < 0)
        gOptions.seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);

    is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    if (!gOptions.window_title && (t = av_dict_get(ic->metadata, "title", NULL, 0)))
        gOptions.window_title = av_asprintf("%s - %s", t->value, gOptions.input_filename);

    /* if seeking requested, we execute it */
    if (gOptions.start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = gOptions.start_time;
        /* add the stream start time */
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                    ps->filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    is->realtime = is_realtime(ic);

    if (gOptions.show_status)
        av_dump_format(ic, 0, ps->filename, 0);

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        enum AVMediaType type = st->codec->codec_type;
        st->discard = AVDISCARD_ALL;
        if (gOptions.wanted_stream_spec[type] && st_index[type] == -1)
            if (avformat_match_stream_specifier(ic, st, gOptions.wanted_stream_spec[type]) > 0)
                st_index[type] = i;
    }
    for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
        if (gOptions.wanted_stream_spec[i] && st_index[i] == -1) {
            av_log(NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n", gOptions.wanted_stream_spec[i], av_get_media_type_string((AVMediaType)i));
            st_index[i] = INT_MAX;
        }
    }

    if (!gOptions.video_disable)
        st_index[AVMEDIA_TYPE_VIDEO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    if (!gOptions.audio_disable)
        st_index[AVMEDIA_TYPE_AUDIO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                st_index[AVMEDIA_TYPE_AUDIO],
                                st_index[AVMEDIA_TYPE_VIDEO],
                                NULL, 0);
    if (!gOptions.video_disable && !gOptions.subtitle_disable)
        st_index[AVMEDIA_TYPE_SUBTITLE] =
            av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                                st_index[AVMEDIA_TYPE_SUBTITLE],
                                (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                                 st_index[AVMEDIA_TYPE_AUDIO] :
                                 st_index[AVMEDIA_TYPE_VIDEO]),
                                NULL, 0);

    is->show_mode = ::gOptions.show_mode;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        AVCodecContext *avctx = st->codec;
        AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
        if (avctx->width)
            is->getDisplay()->set_default_window_size(avctx->width, avctx->height, sar);
    }

    /* open the streams */
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
       ps-> stream_component_open(is, st_index[AVMEDIA_TYPE_AUDIO]);
    }

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        ret = ps->stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]);
    }
    if (is->show_mode == SHOW_MODE_NONE)
        is->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;

    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        ps->stream_component_open(is, st_index[AVMEDIA_TYPE_SUBTITLE]);
    }

    if (ps->video_stream < 0 && ps->audio_stream < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               ps->filename);
        ret = -1;
        goto fail;
    }

    if (gOptions.infinite_buffer < 0 && is->realtime)
        gOptions.infinite_buffer = 1;

    for (;;) {
        if (ctrl->abort_request)
            break;
        if (ctrl->paused != ctrl->last_paused) {
            ctrl->last_paused = ctrl->paused;
            if (ctrl->paused)
                ctrl->read_pause_return = av_read_pause(ic);
            else
                av_read_play(ic);
        }
#if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
        if (ctrl->paused &&
                (!strcmp(ic->iformat->name, "rtsp") ||
                 (ic->pb && !strncmp(gOptions.input_filename, "mmsh:", 5)))) {
            /* wait 10 ms to avoid trying to get another packet */
            /* XXX: horrible */
            SDL_Delay(10);
            continue;
        }
#endif
        if (ctrl->seek_req) {
            int64_t seek_target = ctrl->seek_pos;
            int64_t seek_min    = ctrl->seek_rel > 0 ? seek_target - ctrl->seek_rel + 2: INT64_MIN;
            int64_t seek_max    = ctrl->seek_rel < 0 ? seek_target - ctrl->seek_rel - 2: INT64_MAX;
// FIXME the +-2 is due to rounding being not done in the correct direction in generation
//      of the seek_pos/seek_rel variables

            ret = avformat_seek_file(ps->ic, -1, seek_min, seek_target, seek_max, ctrl->seek_flags);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n", ps->ic->filename);
            } else {
                if (ps->audio_stream >= 0) {
                    ps->audioq.flush();
                    ps->audioq.put(&flush_pkt);
                }
                if (ps->subtitle_stream >= 0) {
                    ps->subtitleq.flush();
                    ps->subtitleq.put(&flush_pkt);
                }
                if (ps->video_stream >= 0) {
                    ps->videoq.flush();
                    ps->videoq.put(&flush_pkt);
                }
                if (ctrl->seek_flags & AVSEEK_FLAG_BYTE) {
                   is->extclk. set_clock(NAN, 0);
                } else {
                   is->extclk. set_clock(seek_target / (double)AV_TIME_BASE, 0);
                }
            }
            ctrl->seek_req = 0;
            ctrl->queue_attachments_req = 1;
            ps->eof = 0;
            if (ctrl->paused)
                is->getController()->step_to_next_frame();
        }
        if (ctrl->queue_attachments_req) {
            if (ps->video_st && ps->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                AVPacket copy;
                if ((ret = av_copy_packet(&copy, &ps->video_st->attached_pic)) < 0)
                    goto fail;
                ps->videoq.put( &copy);
                ps->videoq.put_nullpacket(ps->video_stream);
            }
            ctrl->queue_attachments_req = 0;
        }

        /* if the queue are full, no need to read more */
        if (gOptions.infinite_buffer<1 &&
              (ps->audioq.size + ps->videoq.size + ps->subtitleq.size > MAX_QUEUE_SIZE
            || (   (ps->audioq   .nb_packets > MIN_FRAMES || ps->audio_stream < 0 || ps->audioq.abort_request)
                && (ps->videoq   .nb_packets > MIN_FRAMES || ps->video_stream < 0 || ps->videoq.abort_request
                    || (ps->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC))
                && (ps->subtitleq.nb_packets > MIN_FRAMES || ps->subtitle_stream < 0 || ps->subtitleq.abort_request)))) {
            /* wait 10 ms */
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        }
        if (!ctrl->paused &&
            (!ps->audio_st || (is->auddec().finished == ps->audioq.serial && is->sampq().nb_remaining() == 0)) &&
            (!ps->video_st || (is->viddec().finished == ps->videoq.serial && is->pictq().nb_remaining() == 0))) {
            if (gOptions.loop != 1 && (!gOptions.loop || --gOptions.loop)) {
                is->getController()->stream_seek( gOptions.start_time != AV_NOPTS_VALUE ? gOptions.start_time : 0, 0, 0);
            } else if (gOptions.autoexit) {
                ret = AVERROR_EOF;
                goto fail;
            }
        }
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !ps->eof) {
                if (ps->video_stream >= 0)
                    ps->videoq.put_nullpacket(ps->video_stream);
                if (ps->audio_stream >= 0)
                    ps->audioq.put_nullpacket(ps->audio_stream);
                if (ps->subtitle_stream >= 0)
                    ps->subtitleq.put_nullpacket(ps->subtitle_stream);
                ps->eof = 1;
            }
            if (ic->pb && ic->pb->error)
                break;
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        } else {
            ps->eof = 0;
        }
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = ic->streams[pkt->stream_index]->start_time;
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
        pkt_in_play_range = gOptions.duration == AV_NOPTS_VALUE ||
                (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                av_q2d(ic->streams[pkt->stream_index]->time_base) -
                (double)(gOptions.start_time != AV_NOPTS_VALUE ? gOptions.start_time : 0) / 1000000
                <= ((double)gOptions.duration / 1000000);
        if (pkt->stream_index == ps->audio_stream && pkt_in_play_range) {
            ps->audioq.put(pkt);
        } else if (pkt->stream_index == ps->video_stream && pkt_in_play_range
                   && !(ps->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            ps->videoq.put(pkt);
        } else if (pkt->stream_index == ps->subtitle_stream && pkt_in_play_range) {
            ps->subtitleq.put(pkt);
        } else {
            av_free_packet(pkt);
        }
    }
    /* wait until the end */
    while (!ctrl->abort_request) {
        SDL_Delay(100);
    }

    ret = 0;
 fail:
    /* close each stream */
    if (ps->audio_stream >= 0)
        ps->stream_component_close(is, ps->audio_stream);
    if (ps->video_stream >= 0)
        ps->stream_component_close(is, ps->video_stream);
    if (ps->subtitle_stream >= 0)
        ps->stream_component_close(is, ps->subtitle_stream);
    if (ic) {
        avformat_close_input(&ic);
        ps->ic = NULL;
    }

    if (ret != 0) {
        SDL_Event event;

        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    SDL_DestroyMutex(wait_mutex);
    return 0;
}




/* copy samples for viewing in editor window */
static void update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}


int audio_decode_frame(VideoState *is);
/* prepare a new audio buffer */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    VideoState *is = (VideoState *) opaque;
    int audio_size, len1;

    gOptions.audio_callback_time = av_gettime_relative();

    while (len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {
           audio_size = audio_decode_frame(is);
           if (audio_size < 0) {
                /* if error, just output silence */
               is->audio_buf      = is->silence_buf;
               is->audio_buf_size = sizeof(is->silence_buf) / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
           } else {
               if (is->show_mode != SHOW_MODE_VIDEO)
                   update_sample_display(is, (int16_t *)is->audio_buf, audio_size);
               is->audio_buf_size = audio_size;
           }
           is->audio_buf_index = 0;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
            len1 = len;
        memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!isnan(is->audio_clock)) {
        is->audclk.set_clock_at(is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec, is->audio_clock_serial, gOptions.audio_callback_time / 1000000.0);
        is->extclk.sync_clock_to_slave(&is->audclk);
    }
}

int AVStreamsParser::audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
    SDL_AudioSpec wanted_spec, spec;
    const char *env;
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        wanted_nb_channels = atoi(env);
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = opaque;
    while (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
        av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, SDL_GetError());
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
                av_log(NULL, AV_LOG_ERROR,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }
    if (spec.format != AUDIO_S16SYS) {
        av_log(NULL, AV_LOG_ERROR,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            av_log(NULL, AV_LOG_ERROR,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels =  spec.channels;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    return spec.size;
}



int configure_audio_filters(VideoState *is, const char *afilters, int force_output_format);
/* open a given stream. Return 0 if OK */
int AVStreamsParser::stream_component_open(VideoState *is, int stream_index)
{
    AVStreamsParser* ps = is->getAVStreamsParser();
    AVFormatContext *ic = ps->ic;
    FFPlayController* ctrl = is->getController();
    AVCodecContext *avctx;
    AVCodec *codec;
    const char *forced_codec_name = NULL;
    AVDictionary *opts;
    AVDictionaryEntry *t = NULL;
    int sample_rate, nb_channels;
    int64_t channel_layout;
    int ret = 0;
    int stream_lowres = gOptions.lowres;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;
    avctx = ic->streams[stream_index]->codec;

    codec = avcodec_find_decoder(avctx->codec_id);

    switch(avctx->codec_type){
        case AVMEDIA_TYPE_AUDIO   : ps->last_audio_stream    = stream_index; forced_codec_name =    gOptions.audio_codec_name; break;
        case AVMEDIA_TYPE_SUBTITLE: ps->last_subtitle_stream = stream_index; forced_codec_name = gOptions.subtitle_codec_name; break;
        case AVMEDIA_TYPE_VIDEO   : ps->last_video_stream    = stream_index; forced_codec_name =    gOptions.video_codec_name; break;
    }
    if (forced_codec_name)
        codec = avcodec_find_decoder_by_name(forced_codec_name);
    if (!codec) {
        if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with name '%s'\n", forced_codec_name);
        else                   av_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with id %d\n", avctx->codec_id);
        return -1;
    }

    avctx->codec_id = codec->id;
    if(stream_lowres > av_codec_get_max_lowres(codec)){
        av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                av_codec_get_max_lowres(codec));
        stream_lowres = av_codec_get_max_lowres(codec);
    }
    av_codec_set_lowres(avctx, stream_lowres);

#if FF_API_EMU_EDGE
    if(stream_lowres) avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif
    if (gOptions.fast)
        avctx->flags2 |= AV_CODEC_FLAG2_FAST;
#if FF_API_EMU_EDGE
    if(codec->capabilities & AV_CODEC_CAP_DR1)
        avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif

    opts = filter_codec_opts(codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
        av_dict_set(&opts, "refcounted_frames", "1", 0);
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        goto fail;
    }
    if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret =  AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

    ps->eof = 0;
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
#if CONFIG_AVFILTER
        {
            AVFilterLink *link;

            is->audio_filter_src.freq           = avctx->sample_rate;
            is->audio_filter_src.channels       = avctx->channels;
            is->audio_filter_src.channel_layout = get_valid_channel_layout(avctx->channel_layout, avctx->channels);
            is->audio_filter_src.fmt            = avctx->sample_fmt;
            if ((ret = configure_audio_filters(is,gOptions.afilters, 0)) < 0)
                goto fail;
            link = is->out_audio_filter->inputs[0];
            sample_rate    = link->sample_rate;
            nb_channels    = link->channels;
            channel_layout = link->channel_layout;
        }
#else
        sample_rate    = avctx->sample_rate;
        nb_channels    = avctx->channels;
        channel_layout = avctx->channel_layout;
#endif

        /* prepare audio output */
        if ((ret = audio_open(is, channel_layout, nb_channels, sample_rate, &is->audio_tgt)) < 0)
            goto fail;
        is->audio_hw_buf_size = ret;
        is->audio_src = is->audio_tgt;
        is->audio_buf_size  = 0;
        is->audio_buf_index = 0;

        /* init averaging filter */
        is->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio fifo fullness,
           we correct audio sync only if larger than this threshold */
        is->audio_diff_threshold = (double)(is->audio_hw_buf_size) / is->audio_tgt.bytes_per_sec;

        ps->audio_stream = stream_index;
        ps->audio_st = ic->streams[stream_index];

        is->auddec().init(avctx, &ps->audioq, is->continue_read_thread, is);
        SDL_PauseAudio(0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        ps->video_stream = stream_index;
        ps->video_st = ic->streams[stream_index];

        is->decoders.viddec_width  = avctx->width;
        is->decoders.viddec_height = avctx->height;

        is->viddec().init(avctx, &ps->videoq, is->continue_read_thread, is);
        ctrl->queue_attachments_req = 1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        ps->subtitle_stream = stream_index;
        ps->subtitle_st = ic->streams[stream_index];

        is->subdec(). init(avctx, &ps->subtitleq, is->continue_read_thread, is);
        break;
    default:
        break;
    }

fail:
    av_dict_free(&opts);

    return ret;
}

void AVStreamsParser::stream_component_close(VideoState *is, int stream_index)
{
    AVStreamsParser* ps = is->getAVStreamsParser();
    AVFormatContext *ic = ps->ic;
    AVCodecContext *avctx;

    if (stream_index < 0 || stream_index >= (int)ic->nb_streams)
        return;
    avctx = ic->streams[stream_index]->codec;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->auddec(). abort(&is->sampq());
        SDL_CloseAudio();
        is->auddec().destroy();
        swr_free(&is->swr_ctx);
        av_freep(&is->audio_buf1);
        is->audio_buf1_size = 0;
        is->audio_buf = NULL;

        if (is->rdft) {
            av_rdft_end(is->rdft);
            av_freep(&is->rdft_data);
            is->rdft = NULL;
            is->rdft_bits = 0;
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->viddec(). abort(&is->pictq());
        is->viddec().destroy();
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subdec(). abort(&is->subpq());
        is->subdec().destroy();
        break;
    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    avcodec_close(avctx);
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        ps->audio_st = NULL;
        ps->audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        ps->video_st = NULL;
        ps->video_stream = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        ps->subtitle_st = NULL;
        ps->subtitle_stream = -1;
        break;
    default:
        break;
    }
}

void AVStreamsParser::stream_cycle_channel(VideoState *is, int codec_type)
{
    AVStreamsParser* ps = is->getAVStreamsParser();
    AVFormatContext *ic = ps->ic;
    int start_index, stream_index;
    int old_index;
    AVStream *st;
    AVProgram *p = NULL;
    int nb_streams = ic->nb_streams;

    if (codec_type == AVMEDIA_TYPE_VIDEO) {
        start_index = ps->last_video_stream;
        old_index = ps->video_stream;
    } else if (codec_type == AVMEDIA_TYPE_AUDIO) {
        start_index = ps->last_audio_stream;
        old_index = ps->audio_stream;
    } else {
        start_index = ps->last_subtitle_stream;
        old_index = ps->subtitle_stream;
    }
    stream_index = start_index;

    if (codec_type != AVMEDIA_TYPE_VIDEO && ps->video_stream != -1) {
        p = av_find_program_from_stream(ic, NULL, ps->video_stream);
        if (p) {
            nb_streams = p->nb_stream_indexes;
            for (start_index = 0; start_index < nb_streams; start_index++)
                if (p->stream_index[start_index] == stream_index)
                    break;
            if (start_index == nb_streams)
                start_index = -1;
            stream_index = start_index;
        }
    }

    for (;;) {
        if (++stream_index >= nb_streams)
        {
            if (codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                stream_index = -1;
                ps->last_subtitle_stream = -1;
                goto the_end;
            }
            if (start_index == -1)
                return;
            stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = ps->ic->streams[p ? p->stream_index[stream_index] : stream_index];
        if (st->codec->codec_type == codec_type) {
            /* check that parameters are OK */
            switch (codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (st->codec->sample_rate != 0 &&
                    st->codec->channels != 0)
                    goto the_end;
                break;
            case AVMEDIA_TYPE_VIDEO:
            case AVMEDIA_TYPE_SUBTITLE:
                goto the_end;
            default:
                break;
            }
        }
    }
 the_end:
    if (p && stream_index != -1)
        stream_index = p->stream_index[stream_index];
    av_log(NULL, AV_LOG_INFO, "Switch %s stream from #%d to #%d\n",
           av_get_media_type_string((AVMediaType)codec_type),
           old_index,
           stream_index);

    is->getAVStreamsParser()->stream_component_close(is, old_index);
    is->getAVStreamsParser()->stream_component_open(is, stream_index);
}


