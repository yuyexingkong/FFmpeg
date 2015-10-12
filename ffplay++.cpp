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
#include "libffplay/ffplayer.h"
#include "libffplay/ffdecoder.h"
#include "libffplay/display.h"
#include "libffplay/control.h"

const char program_name[] = "ffplay";
const int program_birth_year = 2003;
unsigned sws_flags = SWS_BICUBIC;

Options gOptions;
/* current context */

AVPacket flush_pkt;


void free_picture(Frame *vp);
void do_exit(VideoState *is);

static void sigterm_handler(int sig)
{
    exit(123);
}

/* called to display each frame */
VideoState *movie_open(const char *filename, AVInputFormat *iformat);

static void refresh_loop_wait_event(VideoState *is, SDL_Event *event) {
    double remaining_time = 0.0;
    AVStreamsParser* ps = is->getAVStreamsParser();
    FFPlayController* ctrl = is->getController();
    SDL_PumpEvents();
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_ALLEVENTS)) {
        if (!gOptions.cursor_hidden && av_gettime_relative() - gOptions.cursor_last_shown > CURSOR_HIDE_DELAY) {
            SDL_ShowCursor(0);
            gOptions.cursor_hidden = 1;
        }
        if (remaining_time > 0.0)
            av_usleep((int64_t)(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;
        if (is->show_mode != SHOW_MODE_NONE && (!ctrl->paused || ctrl->force_refresh))
            is->getDisplay()->video_refresh(is, &remaining_time);
        SDL_PumpEvents();
    }
}

/* handle an event sent by the GUI */
static void event_loop(VideoState *cur_movie)
{
    SDL_Event event;
    double incr, pos, frac;
    FFPlayController* ctrl = cur_movie->getController();

    for (;;) {
        double x;
        refresh_loop_wait_event(cur_movie, &event);
        switch (event.type) {
            case SDL_KEYDOWN:
                if (gOptions.exit_on_keydown) {
                    do_exit(cur_movie);
                    break;
                }
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                    case SDLK_q:
                        do_exit(cur_movie);
                        break;
                    case SDLK_f:
                        cur_movie->getController()->toggle_full_screen();
                        ctrl->force_refresh = 1;
                        break;
                    case SDLK_p:
                    case SDLK_SPACE:
                        cur_movie->getController()->toggle_pause();
                        break;
                    case SDLK_s: // S: Step to next frame
                        cur_movie->getController()->step_to_next_frame();
                        break;
                    case SDLK_a:
                        cur_movie->getAVStreamsParser()->stream_cycle_channel(cur_movie, AVMEDIA_TYPE_AUDIO);
                        break;
                    case SDLK_v:
                        cur_movie->getAVStreamsParser()->stream_cycle_channel(cur_movie, AVMEDIA_TYPE_VIDEO);
                        break;
                    case SDLK_c:
                        cur_movie->getAVStreamsParser()->stream_cycle_channel(cur_movie, AVMEDIA_TYPE_VIDEO);
                        cur_movie->getAVStreamsParser()->stream_cycle_channel(cur_movie, AVMEDIA_TYPE_AUDIO);
                        cur_movie->getAVStreamsParser()->stream_cycle_channel(cur_movie, AVMEDIA_TYPE_SUBTITLE);
                        break;
                    case SDLK_t:
                        cur_movie->getAVStreamsParser()->stream_cycle_channel(cur_movie, AVMEDIA_TYPE_SUBTITLE);
                        break;
                    case SDLK_w:
#if CONFIG_AVFILTER
                        if (cur_movie->show_mode == SHOW_MODE_VIDEO && cur_movie->vfilter_idx < gOptions.nb_vfilters - 1) {
                            if (++cur_movie->vfilter_idx >= gOptions.nb_vfilters)
                                cur_movie->vfilter_idx = 0;
                        } else {
                            cur_movie->vfilter_idx = 0;
                            cur_movie->getController()->toggle_audio_display();
                        }
#else
                        cur_movie->getController()->toggle_audio_display();
#endif
                        break;
                    case SDLK_PAGEUP:
                        if (cur_movie->getAVFormatContext()->nb_chapters <= 1) {
                            incr = 600.0;
                            goto do_seek;
                        }
                        cur_movie->getController()->seek_chapter(1);
                        break;
                    case SDLK_PAGEDOWN:
                        if (cur_movie->getAVFormatContext()->nb_chapters <= 1) {
                            incr = -600.0;
                            goto do_seek;
                        }
                        cur_movie->getController()->seek_chapter(-1);
                        break;
                    case SDLK_LEFT:
                        incr = -10.0;
                        goto do_seek;
                    case SDLK_RIGHT:
                        incr = 10.0;
                        goto do_seek;
                    case SDLK_UP:
                        incr = 60.0;
                        goto do_seek;
                    case SDLK_DOWN:
                        incr = -60.0;
do_seek:
                        if (gOptions.seek_by_bytes) {
                            pos = -1;
                            if (pos < 0 && cur_movie->getAVStreamsParser()->video_stream >= 0)
                                pos = cur_movie->pictq().last_pos();
                            if (pos < 0 && cur_movie->getAVStreamsParser()->audio_stream >= 0)
                                pos = cur_movie->sampq().last_pos();
                            if (pos < 0)
                                pos = avio_tell(cur_movie->getAVFormatContext()->pb);
                            if (cur_movie->getAVFormatContext()->bit_rate)
                                incr *= cur_movie->getAVFormatContext()->bit_rate / 8.0;
                            else
                                incr *= 180000.0;
                            pos += incr;
                            cur_movie->getController()->stream_seek( pos, incr, 1);
                        } else {
                            pos = get_master_clock(cur_movie);
                            if (isnan(pos))
                                pos = (double)ctrl->seek_pos / AV_TIME_BASE;
                            pos += incr;
                            if (cur_movie->getAVFormatContext()->start_time != AV_NOPTS_VALUE && pos < cur_movie->getAVFormatContext()->start_time / (double)AV_TIME_BASE)
                                pos = cur_movie->getAVFormatContext()->start_time / (double)AV_TIME_BASE;
                            cur_movie->getController()->stream_seek((int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
                        }
                        break;
                    default:
                        break;
                }
                break;
            case SDL_VIDEOEXPOSE:
                ctrl->force_refresh = 1;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (gOptions.exit_on_mousedown) {
                    do_exit(cur_movie);
                    break;
                }
            case SDL_MOUSEMOTION:
                if (gOptions.cursor_hidden) {
                    SDL_ShowCursor(1);
                    gOptions.cursor_hidden = 0;
                }
                gOptions.cursor_last_shown = av_gettime_relative();
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    x = event.button.x;
                } else {
                    if (event.motion.state != SDL_PRESSED)
                        break;
                    x = event.motion.x;
                }
                if (gOptions.seek_by_bytes || cur_movie->getAVFormatContext()->duration <= 0) {
                    uint64_t size =  avio_size(cur_movie->getAVFormatContext()->pb);
                    cur_movie->getController()->stream_seek(size*x/cur_movie->width, 0, 1);
                } else {
                    int64_t ts;
                    int ns, hh, mm, ss;
                    int tns, thh, tmm, tss;
                    tns  = cur_movie->getAVFormatContext()->duration / 1000000LL;
                    thh  = tns / 3600;
                    tmm  = (tns % 3600) / 60;
                    tss  = (tns % 60);
                    frac = x / cur_movie->width;
                    ns   = frac * tns;
                    hh   = ns / 3600;
                    mm   = (ns % 3600) / 60;
                    ss   = (ns % 60);
                    av_log(NULL, AV_LOG_INFO,
                            "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac*100,
                            hh, mm, ss, thh, tmm, tss);
                    ts = frac * cur_movie->getAVFormatContext()->duration;
                    if (cur_movie->getAVFormatContext()->start_time != AV_NOPTS_VALUE)
                        ts += cur_movie->getAVFormatContext()->start_time;
                    cur_movie->getController()->stream_seek(ts, 0, 0);
                }
                break;
            case SDL_VIDEORESIZE:
                cur_movie->getDisplay()->screen_resize(FFMIN(16383, event.resize.w), event.resize.h, 0,
                        SDL_HWSURFACE|(gOptions.is_full_screen?SDL_FULLSCREEN:SDL_RESIZABLE)|SDL_ASYNCBLIT|SDL_HWACCEL);
                
                if (!cur_movie->getDisplay()->hasScreen()) {
                    av_log(NULL, AV_LOG_FATAL, "Failed to set video mode\n");
                    do_exit(cur_movie);
                }
                gOptions.screen_width  = cur_movie->width  = cur_movie->getDisplay()->getScreenWidth();
                gOptions.screen_height = cur_movie->height = cur_movie->getDisplay()->getScreenHeight();
                ctrl->force_refresh = 1;
                break;
            case SDL_QUIT:
            case FF_QUIT_EVENT:
                do_exit(cur_movie);
                break;
            case FF_ALLOC_EVENT:
                cur_movie->getDisplay()->alloc_picture((VideoState*) event.user.data1);
                break;
            default:
                break;
        }
    }
}

static void show_usage(void)
{
    av_log(NULL, AV_LOG_INFO, "Simple media player\n");
    av_log(NULL, AV_LOG_INFO, "usage: %s [options] input_file\n", program_name);
    av_log(NULL, AV_LOG_INFO, "\n");
}

extern "C" const OptionDef options[]; 
void show_help_default(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:", 0, OPT_EXPERT, 0);
    show_help_options(options, "Advanced options:", OPT_EXPERT, 0, 0);
    printf("\n");
    show_help_children(avcodec_get_class(), AV_OPT_FLAG_DECODING_PARAM);
    show_help_children(avformat_get_class(), AV_OPT_FLAG_DECODING_PARAM);
#if !CONFIG_AVFILTER
    show_help_children(sws_get_class(), AV_OPT_FLAG_ENCODING_PARAM);
#else
    show_help_children(avfilter_get_class(), AV_OPT_FLAG_FILTERING_PARAM);
#endif
    printf("\nWhile playing:\n"
            "q, ESC              quit\n"
            "f                   toggle full screen\n"
            "p, SPC              pause\n"
            "a                   cycle audio channel in the current program\n"
            "v                   cycle video channel\n"
            "t                   cycle subtitle channel in the current program\n"
            "c                   cycle program\n"
            "w                   cycle video filters or show modes\n"
            "s                   activate frame-step mode\n"
            "left/right          seek backward/forward 10 seconds\n"
            "down/up             seek backward/forward 1 minute\n"
            "page down/page up   seek backward/forward 10 minutes\n"
            "mouse click         seek to percentage in file corresponding to fraction of width\n"
          );
}

static int lockmgr(void **mtx, enum AVLockOp op)
{
    switch(op) {
        case AV_LOCK_CREATE:
            *mtx = SDL_CreateMutex();
            if(!*mtx)
                return 1;
            return 0;
        case AV_LOCK_OBTAIN:
            return !!SDL_LockMutex((SDL_mutex*)*mtx);
        case AV_LOCK_RELEASE:
            return !!SDL_UnlockMutex((SDL_mutex*)*mtx);
        case AV_LOCK_DESTROY:
            SDL_DestroyMutex((SDL_mutex*)*mtx);
            return 0;
    }
    return 1;
}
///////////////////  file_filter  ////////////////////
static ssize_t file_read (int __fd, void *__buf, size_t __nbytes, size_t off)
{
    ssize_t n = read(__fd, __buf, __nbytes);
    if(0){ // test
        int i;
        char* p = (char*) __buf;
        for(i = 0; i<n;i++){
            p[i] = p[i] ^ 'c';
        }
    }
    return n;
}

static int file_fstat (int __fd, struct stat *__buf)
{
    return fstat(__fd, __buf); 
}


struct fstream_functors mystream = { file_read, file_fstat};

///////////////////  file_filter  END ////////////////


void opt_input_file(void *optctx, const char *filename);
/* Called from the main */
int main(int argc, char **argv)
{
    int flags;
    VideoState *is;
    char dummy_videodriver[] = "SDL_VIDEODRIVER=dummy";

    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    parse_loglevel(argc, argv, options);

    set_fstream(&mystream);   
    /* register all codecs, demux and protocols */
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
#if CONFIG_AVFILTER
    avfilter_register_all();
#endif
    av_register_all();
    avformat_network_init();

    init_opts();

    signal(SIGINT , sigterm_handler); /* Interrupt (ANSI).    */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */

    show_banner(argc, argv, options);

    parse_options(NULL, argc, argv, options, opt_input_file);

    if (!gOptions.input_filename) {
        show_usage();
        av_log(NULL, AV_LOG_FATAL, "An input file must be specified\n");
        av_log(NULL, AV_LOG_FATAL,
                "Use -h to get full help or, even better, run 'man %s'\n", program_name);
        exit(1);
    }

    if (gOptions.display_disable) {
        gOptions.video_disable = 1;
    }
    flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (gOptions.audio_disable)
        flags &= ~SDL_INIT_AUDIO;
    if (gOptions.display_disable)
        SDL_putenv(dummy_videodriver); /* For the event queue, we always need a video driver. */
#if !defined(_WIN32) && !defined(__APPLE__)
    flags |= SDL_INIT_EVENTTHREAD; /* Not supported on Windows or Mac OS X */
#endif
    if (SDL_Init (flags)) {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
        exit(1);
    }

    if (!gOptions.display_disable) {
        const SDL_VideoInfo *vi = SDL_GetVideoInfo();
        gOptions.fs_screen_width = vi->current_w;
        gOptions.fs_screen_height = vi->current_h;
    }

    SDL_EventState(SDL_ACTIVEEVENT, SDL_IGNORE);
    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

    if (av_lockmgr_register(lockmgr)) {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize lock manager!\n");
        do_exit(NULL);
    }

    av_init_packet(&flush_pkt);
    flush_pkt.data = (uint8_t *)&flush_pkt;

    is = movie_open(gOptions.input_filename, gOptions.file_iformat);
    if (!is) {
        av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
        do_exit(NULL);
    }

    event_loop(is);

    /* never returns */

    return 0;
}
