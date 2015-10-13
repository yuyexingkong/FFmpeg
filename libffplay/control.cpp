/*
 * =====================================================================================
 *
 *       Filename:  control.cpp
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/22/2015 09:48:21 PM
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

/* seek in the stream */
void FFPlayController::stream_seek(int64_t pos, int64_t rel, int seek_by_bytes)
{
    FFPlayController* ctrl = is->getController();
    if (!ctrl->seek_req) {
        ctrl->seek_pos = pos;
        ctrl->seek_rel = rel;
        ctrl->seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes)
            ctrl->seek_flags |= AVSEEK_FLAG_BYTE;
        ctrl->seek_req = 1;
        is->continue_read_thread.signal();
    }
}

/* pause or resume the video */
void FFPlayController::stream_toggle_pause()
{
    FFPlayController* ctrl = is->getController();
    if (ctrl->paused) {
        is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
        if (ctrl->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
        }
        is->vidclk.set_clock(is->vidclk.get_clock(), is->vidclk.serial);
    }
    is->extclk. set_clock(is->extclk.get_clock(), is->extclk.serial);
    ctrl->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !ctrl->paused;
}

void FFPlayController::toggle_pause()
{
    stream_toggle_pause();
    is->step = 0;
}

void FFPlayController::step_to_next_frame()
{
    /* if the stream is paused unpause it, then step */
    if (is->getController()->paused)
        stream_toggle_pause();
    is->step = 1;
}


void FFPlayController::toggle_full_screen()
{
#if defined(__APPLE__) && SDL_VERSION_ATLEAST(1, 2, 14)
    /* OS X needs to reallocate the SDL overlays */
    int i;
    for (i = 0; i < VIDEO_PICTURE_QUEUE_SIZE; i++)
        is->pictq.queue[i].reallocate = 1;
#endif
    gOptions.is_full_screen = !gOptions.is_full_screen;
    is->getDisplay()->video_screen_open(is, 1, NULL);
}

void fill_rectangle(SDL_Surface *screen,
                                  int x, int y, int w, int h, int color, int update);
void FFPlayController::toggle_audio_display()
{
    int bgcolor = is->getDisplay()->rgb(0x00, 0x00, 0x00);
    int next = is->show_mode;
    AVStreamsParser* ps = is->getAVStreamsParser();
    do {
        next = (next + 1) % SHOW_MODE_NB;
    } while (next != is->show_mode && (next == SHOW_MODE_VIDEO && !ps->video_st || next != SHOW_MODE_VIDEO && !ps->audio_st));
    if (is->show_mode != next) {
        is->getDisplay()->fill_rectangle(
                    is->xleft, is->ytop, is->width, is->height,
                    bgcolor, 1);
        is->getController()->force_refresh = 1;
        is->show_mode = (ShowMode)next;
    }
}


double get_master_clock(VideoState *is);
void FFPlayController::seek_chapter(int incr)
{
    int64_t pos = get_master_clock(is) * AV_TIME_BASE;
    size_t i;
    AVStreamsParser* ps = is->getAVStreamsParser();

    if (!ps->ic->nb_chapters)
        return;

    /* find the current chapter */
    for (i = 0; i < ps->ic->nb_chapters; i++) {
        AVChapter *ch = ps->ic->chapters[i];
        if (av_compare_ts(pos, AV_TIME_BASE_Q, ch->start, ch->time_base) < 0) {
            i--;
            break;
        }
    }

    i += incr;
    i = FFMAX(i, 0);
    if (i >= ps->ic->nb_chapters)
        return;

    av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
    stream_seek(av_rescale_q(ps->ic->chapters[i]->start, ps->ic->chapters[i]->time_base,
                                 AV_TIME_BASE_Q), 0, 0);
}


