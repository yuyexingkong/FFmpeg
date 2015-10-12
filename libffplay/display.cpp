/*
 * =====================================================================================
 *
 *       Filename:  display.cpp
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/22/2015 09:23:08 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  DAI ZHENGHUA (), djx.zhenghua@gmail.com
 *        Company:  
 *
 * =====================================================================================
 */
#include "ffplayer.h"
#include "control.h"

static
void fill_rectangle(SDL_Surface *screen,
                                  int x, int y, int w, int h, int color, int update)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_FillRect(screen, &rect, color);
    if (update && w > 0 && h > 0)
        SDL_UpdateRect(screen, x, y, w, h);
}

void Display::fill_rectangle(int x, int y, int w, int h, int color, int update)
{
    ::fill_rectangle(screen, x,y,w,h,color,update);
}
/* draw only the border of a rectangle */
void  Display::fill_border(int xleft, int ytop, int width, int height, int x, int y, int w, int h, int color, int update)
{
    int w1, w2, h1, h2;

    /* fill the background */
    w1 = x;
    if (w1 < 0)
        w1 = 0;
    w2 = width - (x + w);
    if (w2 < 0)
        w2 = 0;
    h1 = y;
    if (h1 < 0)
        h1 = 0;
    h2 = height - (y + h);
    if (h2 < 0)
        h2 = 0;
    fill_rectangle(
                   xleft, ytop,
                   w1, height,
                   color, update);
    fill_rectangle(
                   xleft + width - w2, ytop,
                   w2, height,
                   color, update);
    fill_rectangle(
                   xleft + w1, ytop,
                   width - w1 - w2, h1,
                   color, update);
    fill_rectangle(
                   xleft + w1, ytop + height - h2,
                   width - w1 - w2, h2,
                   color, update);
}

#define ALPHA_BLEND(a, oldp, newp, s)\
((((oldp << s) * (255 - (a))) + (newp * (a))) / (255 << s))



#define BPP 1

static void blend_subrect(AVPicture *dst, const AVSubtitleRect *rect, int imgw, int imgh)
{
    int x, y, Y, U, V, A;
    uint8_t *lum, *cb, *cr;
    int dstx, dsty, dstw, dsth;
    const AVPicture *src = &rect->pict;

    dstw = av_clip(rect->w, 0, imgw);
    dsth = av_clip(rect->h, 0, imgh);
    dstx = av_clip(rect->x, 0, imgw - dstw);
    dsty = av_clip(rect->y, 0, imgh - dsth);
    lum = dst->data[0] + dstx + dsty * dst->linesize[0];
    cb  = dst->data[1] + dstx/2 + (dsty >> 1) * dst->linesize[1];
    cr  = dst->data[2] + dstx/2 + (dsty >> 1) * dst->linesize[2];

    for (y = 0; y<dsth; y++) {
        for (x = 0; x<dstw; x++) {
            Y = src->data[0][x + y*src->linesize[0]];
            A = src->data[3][x + y*src->linesize[3]];
            lum[0] = ALPHA_BLEND(A, lum[0], Y, 0);
            lum++;
        }
        lum += dst->linesize[0] - dstw;
    }

    for (y = 0; y<dsth/2; y++) {
        for (x = 0; x<dstw/2; x++) {
            U = src->data[1][x + y*src->linesize[1]];
            V = src->data[2][x + y*src->linesize[2]];
            A = src->data[3][2*x     +  2*y   *src->linesize[3]]
              + src->data[3][2*x + 1 +  2*y   *src->linesize[3]]
              + src->data[3][2*x + 1 + (2*y+1)*src->linesize[3]]
              + src->data[3][2*x     + (2*y+1)*src->linesize[3]];
            cb[0] = ALPHA_BLEND(A>>2, cb[0], U, 0);
            cr[0] = ALPHA_BLEND(A>>2, cr[0], V, 0);
            cb++;
            cr++;
        }
        cb += dst->linesize[1] - dstw/2;
        cr += dst->linesize[2] - dstw/2;
    }
}

void free_picture(Frame *vp)
{
     if (vp->bmp) {
         SDL_FreeYUVOverlay(vp->bmp);
         vp->bmp = NULL;
     }
}

static void calculate_display_rect(SDL_Rect *rect,
                                   int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                   int pic_width, int pic_height, AVRational pic_sar)
{
    float aspect_ratio;
    int width, height, x, y;

    if (pic_sar.num == 0)
        aspect_ratio = 0;
    else
        aspect_ratio = av_q2d(pic_sar);

    if (aspect_ratio <= 0.0)
        aspect_ratio = 1.0;
    aspect_ratio *= (float)pic_width / (float)pic_height;

    /* XXX: we suppose the screen has a 1.0 pixel ratio */
    height = scr_height;
    width = ((int)rint(height * aspect_ratio)) & ~1;
    if (width > scr_width) {
        width = scr_width;
        height = ((int)rint(width / aspect_ratio)) & ~1;
    }
    x = (scr_width - width) / 2;
    y = (scr_height - height) / 2;
    rect->x = scr_xleft + x;
    rect->y = scr_ytop  + y;
    rect->w = FFMAX(width,  1);
    rect->h = FFMAX(height, 1);
}

void  Display::video_image_display(VideoState *is)
{
    Frame *vp;
    Frame *sp;
    AVPicture pict;
    SDL_Rect rect;
    int i;
    AVStreamsParser* ps = is->getAVStreamsParser();

    vp = is->pictq().peek();
    if (vp->bmp) {
        if (ps->subtitle_st) {
            if (is->subpq().nb_remaining() > 0) {
                sp = is->subpq().peek();

                if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000)) {
                    SDL_LockYUVOverlay (vp->bmp);

                    pict.data[0] = vp->bmp->pixels[0];
                    pict.data[1] = vp->bmp->pixels[2];
                    pict.data[2] = vp->bmp->pixels[1];

                    pict.linesize[0] = vp->bmp->pitches[0];
                    pict.linesize[1] = vp->bmp->pitches[2];
                    pict.linesize[2] = vp->bmp->pitches[1];

                    for (i = 0; i < sp->sub.num_rects; i++)
                        blend_subrect(&pict, sp->sub.rects[i],
                                      vp->bmp->w, vp->bmp->h);

                    SDL_UnlockYUVOverlay (vp->bmp);
                }
            }
        }

        calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->width, vp->height, vp->sar);

        SDL_DisplayYUVOverlay(vp->bmp, &rect);

        if (rect.x != is->last_display_rect.x || rect.y != is->last_display_rect.y || rect.w != is->last_display_rect.w || rect.h != is->last_display_rect.h || is->getController()->force_refresh) {
            int bgcolor = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
            fill_border(is->xleft, is->ytop, is->width, is->height, rect.x, rect.y, rect.w, rect.h, bgcolor, 1);
            is->last_display_rect = rect;
        }
    }
}

static inline int compute_mod(int a, int b)
{
    return a < 0 ? a%b + b : a%b;
}

void  Display::video_audio_display(VideoState *s)
{
    int i, i_start, x, y1, y, ys, delay, n, nb_display_channels;
    int ch, channels, h, h2, bgcolor, fgcolor;
    int64_t time_diff;
    int rdft_bits, nb_freq;

    for (rdft_bits = 1; (1 << rdft_bits) < 2 * s->height; rdft_bits++)
        ;
    nb_freq = 1 << (rdft_bits - 1);

    /* compute display index : center on currently output samples */
    channels = s->audio_tgt.channels;
    nb_display_channels = channels;
    if (!s->getController()->paused) {
        int data_used= s->show_mode == SHOW_MODE_WAVES ? s->width : (2*nb_freq);
        n = 2 * channels;
        delay = s->audio_write_buf_size;
        delay /= n;

        /* to be more precise, we take into account the time spent since
           the last buffer computation */
        if (gOptions.audio_callback_time) {
            time_diff = av_gettime_relative() - gOptions.audio_callback_time;
            delay -= (time_diff * s->audio_tgt.freq) / 1000000;
        }

        delay += 2 * data_used;
        if (delay < data_used)
            delay = data_used;

        i_start= x = compute_mod(s->sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);
        if (s->show_mode == SHOW_MODE_WAVES) {
            h = INT_MIN;
            for (i = 0; i < 1000; i += channels) {
                int idx = (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
                int a = s->sample_array[idx];
                int b = s->sample_array[(idx + 4 * channels) % SAMPLE_ARRAY_SIZE];
                int c = s->sample_array[(idx + 5 * channels) % SAMPLE_ARRAY_SIZE];
                int d = s->sample_array[(idx + 9 * channels) % SAMPLE_ARRAY_SIZE];
                int score = a - d;
                if (h < score && (b ^ c) < 0) {
                    h = score;
                    i_start = idx;
                }
            }
        }

        s->last_i_start = i_start;
    } else {
        i_start = s->last_i_start;
    }

    bgcolor = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
    if (s->show_mode == SHOW_MODE_WAVES) {
        fill_rectangle(
                       s->xleft, s->ytop, s->width, s->height,
                       bgcolor, 0);

        fgcolor = SDL_MapRGB(screen->format, 0xff, 0xff, 0xff);

        /* total height for one channel */
        h = s->height / nb_display_channels;
        /* graph height / 2 */
        h2 = (h * 9) / 20;
        for (ch = 0; ch < nb_display_channels; ch++) {
            i = i_start + ch;
            y1 = s->ytop + ch * h + (h / 2); /* position of center line */
            for (x = 0; x < s->width; x++) {
                y = (s->sample_array[i] * h2) >> 15;
                if (y < 0) {
                    y = -y;
                    ys = y1 - y;
                } else {
                    ys = y1;
                }
                fill_rectangle(
                               s->xleft + x, ys, 1, y,
                               fgcolor, 0);
                i += channels;
                if (i >= SAMPLE_ARRAY_SIZE)
                    i -= SAMPLE_ARRAY_SIZE;
            }
        }

        fgcolor = SDL_MapRGB(screen->format, 0x00, 0x00, 0xff);

        for (ch = 1; ch < nb_display_channels; ch++) {
            y = s->ytop + ch * h;
            fill_rectangle(
                           s->xleft, y, s->width, 1,
                           fgcolor, 0);
        }
        SDL_UpdateRect(screen, s->xleft, s->ytop, s->width, s->height);
    } else {
        nb_display_channels= FFMIN(nb_display_channels, 2);
        if (rdft_bits != s->rdft_bits) {
            av_rdft_end(s->rdft);
            av_free(s->rdft_data);
            s->rdft = av_rdft_init(rdft_bits, DFT_R2C);
            s->rdft_bits = rdft_bits;
            s->rdft_data = (FFTSample*) av_malloc_array(nb_freq, 4 *sizeof(*s->rdft_data));
        }
        if (!s->rdft || !s->rdft_data){
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate buffers for RDFT, switching to waves display\n");
            s->show_mode = SHOW_MODE_WAVES;
        } else {
            FFTSample *data[2];
            for (ch = 0; ch < nb_display_channels; ch++) {
                data[ch] = s->rdft_data + 2 * nb_freq * ch;
                i = i_start + ch;
                for (x = 0; x < 2 * nb_freq; x++) {
                    double w = (x-nb_freq) * (1.0 / nb_freq);
                    data[ch][x] = s->sample_array[i] * (1.0 - w * w);
                    i += channels;
                    if (i >= SAMPLE_ARRAY_SIZE)
                        i -= SAMPLE_ARRAY_SIZE;
                }
                av_rdft_calc(s->rdft, data[ch]);
            }
            /* Least efficient way to do this, we should of course
             * directly access it but it is more than fast enough. */
            for (y = 0; y < s->height; y++) {
                double w = 1 / sqrt(nb_freq);
                int a = sqrt(w * sqrt(data[0][2 * y + 0] * data[0][2 * y + 0] + data[0][2 * y + 1] * data[0][2 * y + 1]));
                int b = (nb_display_channels == 2 ) ? sqrt(w * sqrt(data[1][2 * y + 0] * data[1][2 * y + 0]
                       + data[1][2 * y + 1] * data[1][2 * y + 1])) : a;
                a = FFMIN(a, 255);
                b = FFMIN(b, 255);
                fgcolor = SDL_MapRGB(screen->format, a, b, (a + b) / 2);

                fill_rectangle(
                            s->xpos, s->height-y, 1, 1,
                            fgcolor, 0);
            }
        }
        SDL_UpdateRect(screen, s->xpos, s->ytop, 1, s->height);
        if (!s->getController()->paused)
            s->xpos++;
        if (s->xpos >= s->width)
            s->xpos= s->xleft;
    }
}

void Display::set_default_window_size(int width, int height, AVRational sar)
{
    SDL_Rect rect;
    calculate_display_rect(&rect, 0, 0, INT_MAX, height, width, height, sar);
    default_width  = rect.w;
    default_height = rect.h;
}

/* display the current picture, if any */
int Display::video_screen_open(VideoState *is, int force_set_video_mode, Frame *vp)
{
    int flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL;
    int w,h;

    if (gOptions.is_full_screen) flags |= SDL_FULLSCREEN;
    else                flags |= SDL_RESIZABLE;

    if (vp && vp->width)
        set_default_window_size(vp->width, vp->height, vp->sar);

    if (gOptions.is_full_screen && gOptions.fs_screen_width) {
        w = gOptions.fs_screen_width;
        h = gOptions.fs_screen_height;
    } else if (!gOptions.is_full_screen && gOptions.screen_width) {
        w = gOptions.screen_width;
        h = gOptions.screen_height;
    } else {
        w = default_width;
        h = default_height;
    }
    w = FFMIN(16383, w);
    if (screen && is->width == screen->w && screen->w == w
       && is->height== screen->h && screen->h == h && !force_set_video_mode)
        return 0;
    screen = SDL_SetVideoMode(w, h, 0, flags);
    if (!screen) {
        av_log(NULL, AV_LOG_FATAL, "SDL: could not set video mode - exiting\n");
        do_exit(is);
    }
    if (!gOptions.window_title)
        gOptions.window_title = gOptions.input_filename;
    SDL_WM_SetCaption(gOptions.window_title, gOptions.window_title);

    is->width  = screen->w;
    is->height = screen->h;

    return 0;
}

void Display::video_display(VideoState *is)
{
    AVStreamsParser* ps = is->getAVStreamsParser();
    if (!screen)
        video_screen_open(is, 0, NULL);
    if (ps->audio_st && is->show_mode != SHOW_MODE_VIDEO)
        video_audio_display(is);
    else if (ps->video_st)
        video_image_display(is);
}

static double vp_duration(VideoState *is, Frame *vp, Frame *nextvp) {
    if (vp->serial == nextvp->serial) {
        double duration = nextvp->pts - vp->pts;
        if (isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
            return vp->duration;
        else
            return duration;
    } else {
        return 0.0;
    }
}

static double compute_target_delay(double delay, VideoState *is)
{
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        diff = is->vidclk.get_clock() - get_master_clock(is);

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!isnan(diff) && fabs(diff) < is->max_frame_duration) {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
            delay, -diff);

    return delay;
}

static void update_video_pts(VideoState *is, double pts, int64_t pos, int serial) {
    /* update current video pts */
    is->vidclk.set_clock(pts, serial);
    is->extclk.sync_clock_to_slave(&is->vidclk);
}

void Display::video_refresh(void *opaque, double *remaining_time)
{
    VideoState *is = (VideoState *)opaque;
    AVStreamsParser* ps = is->getAVStreamsParser();
    FFPlayController * ctrl = is->getController();
    double time;

    Frame *sp, *sp2;

    if (!is->getController()->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        check_external_clock_speed(is);

    if (!gOptions.display_disable && is->show_mode != SHOW_MODE_VIDEO && ps->audio_st) {
        time = av_gettime_relative() / 1000000.0;
        if (ctrl->force_refresh || is->last_vis_time + gOptions.rdftspeed < time) {
            video_display(is);
            is->last_vis_time = time;
        }
        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + gOptions.rdftspeed - time);
    }

    if (ps->video_st) {
        int redisplay = 0;
        if (ctrl->force_refresh)
            redisplay = is->pictq().prev();
retry:
        if (is->pictq().nb_remaining() == 0) {
            // nothing to do, no picture to display in the queue
        } else {
            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            /* dequeue the picture */
            lastvp = is->pictq().peek_last();
            vp = is->pictq().peek();

            if (vp->serial != ps->videoq.serial) {
                is->pictq().next();
                redisplay = 0;
                goto retry;
            }

            if (lastvp->serial != vp->serial && !redisplay)
                is->frame_timer = av_gettime_relative() / 1000000.0;

            if (ctrl->paused)
                goto display;

            /* compute nominal last_duration */
            last_duration = vp_duration(is, lastvp, vp);
            if (redisplay)
                delay = 0.0;
            else
                delay = compute_target_delay(last_duration, is);

            time= av_gettime_relative()/1000000.0;
            if (time < is->frame_timer + delay && !redisplay) {
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                return;
            }

            is->frame_timer += delay;
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;

            SDL_LockMutex(is->pictq().mutex);
            if (!redisplay && !isnan(vp->pts))
                update_video_pts(is, vp->pts, vp->pos, vp->serial);
            SDL_UnlockMutex(is->pictq().mutex);

            if (is->pictq().nb_remaining() > 1) {
                Frame *nextvp = is->pictq().peek_next();
                duration = vp_duration(is, vp, nextvp);
                if(!is->step && (redisplay || gOptions.framedrop>0 || (gOptions.framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration){
                    if (!redisplay)
                        is->frame_drops_late++;
                    is->pictq().next();
                    redisplay = 0;
                    goto retry;
                }
            }

            if (ps->subtitle_st) {
                while (is->subpq().nb_remaining() > 0) {
                    sp = is->subpq().peek();

                    if (is->subpq().nb_remaining() > 1)
                        sp2 = is->subpq().peek_next();
                    else
                        sp2 = NULL;

                    if (sp->serial != ps->subtitleq.serial
                            || (is->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                            || (sp2 && is->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
                    {
                        is->subpq().next();
                    } else {
                        break;
                    }
                }
            }

display:
            /* display picture */
            if (!gOptions.display_disable && is->show_mode == SHOW_MODE_VIDEO)
                video_display(is);

            is->pictq().next();

            if (is->step && !ctrl->paused)
                is->getController()->stream_toggle_pause();
        }
    }
    ctrl->force_refresh = 0;
    if (gOptions.show_status) {
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize;
        double av_diff;

        cur_time = av_gettime_relative();
        if (!last_time || (cur_time - last_time) >= 30000) {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            if (ps->audio_st)
                aqsize = ps->audioq.size;
            if (ps->video_st)
                vqsize = ps->videoq.size;
            if (ps->subtitle_st)
                sqsize = ps->subtitleq.size;
            av_diff = 0;
            if (ps->audio_st && ps->video_st)
                av_diff = is->audclk.get_clock() - is->vidclk.get_clock();
            else if (ps->video_st)
                av_diff = get_master_clock(is) - is->vidclk.get_clock();
            else if (ps->audio_st)                          
                av_diff = get_master_clock(is) - is->audclk.get_clock();
            av_log(NULL, AV_LOG_INFO,
                    "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=   \r",
                    get_master_clock(is),
                    (ps->audio_st && ps->video_st) ? "A-V" : (ps->video_st ? "M-V" : (ps->audio_st ? "M-A" : "   ")),
                    av_diff,
                    is->frame_drops_early + is->frame_drops_late,
                    aqsize / 1024,
                    vqsize / 1024,
                    sqsize,
                    ps->video_st ? ps->video_st->codec->pts_correction_num_faulty_dts : 0,
                    ps->video_st ? ps->video_st->codec->pts_correction_num_faulty_pts : 0);
            fflush(stdout);
            last_time = cur_time;
        }
    }
}


/* allocate a picture (needs to do that in main thread to avoid
   potential locking problems */
void Display::alloc_picture(VideoState *is)
{
    Frame *vp;
    int64_t bufferdiff;
    FrameQueue& pictq = is->pictq();

    vp = &pictq.queue[pictq.windex];

    free_picture(vp);

    video_screen_open(is, 0, vp);

    vp->bmp = SDL_CreateYUVOverlay(vp->width, vp->height,
            SDL_YV12_OVERLAY,
            screen);
    bufferdiff = vp->bmp ? FFMAX(vp->bmp->pixels[0], vp->bmp->pixels[1]) - FFMIN(vp->bmp->pixels[0], vp->bmp->pixels[1]) : 0;
    if (!vp->bmp || vp->bmp->pitches[0] < vp->width || bufferdiff < (int64_t)vp->height * vp->bmp->pitches[0]) {
        /* SDL allocates a buffer smaller than requested if the video
         * overlay hardware is unable to support the requested size. */
        av_log(NULL, AV_LOG_FATAL,
                "Error: the video system does not support an image\n"
                "size of %dx%d pixels. Try using -lowres or -vf \"scale=w:h\"\n"
                "to reduce the image size.\n", vp->width, vp->height );
        do_exit(is);
    }

    SDL_LockMutex(pictq.mutex);
    vp->allocated = 1;
    SDL_CondSignal(pictq.cond);
    SDL_UnlockMutex(pictq.mutex);
}


void Display::screen_resize(int w, int h, int bpp, unsigned int flags)
{
    screen = SDL_SetVideoMode(w, h, bpp, flags);
}
