/*
 * =====================================================================================
 *
 *       Filename:  ffstream.cpp
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/24/2015 09:25:26 PM
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



void movie_close(VideoState *is)
{
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->getController()->abort_request = 1;
    AVStreamsParser* ps = is->getAVStreamsParser();
    ps->read_thread.join();
    ps->videoq.destroy();
    ps->audioq.destroy();
    ps->subtitleq.destroy();

    /* free all pictures */
    is->pictq().destory();
    is->sampq().destory();
    is->subpq().destory();
    is->continue_read_thread.destroy();
#if !CONFIG_AVFILTER
    sws_freeContext(is->img_convert_ctx);
#endif
    sws_freeContext(is->sub_convert_ctx);
    is->~VideoState();
    av_free(is);
}

VideoState *movie_open(const char *filename, AVInputFormat *iformat)
{
    VideoState *is;

    is = (VideoState *) av_mallocz(sizeof(VideoState));
    is = new (is)VideoState();
    if (!is)
        return NULL;
    AVStreamsParser* ps = is->getAVStreamsParser();
    av_strlcpy(ps->filename, filename, sizeof(ps->filename));
    is->iformat = iformat;
    is->ytop    = 0;
    is->xleft   = 0;

    /* start video display */
    if (is->pictq().init(&ps->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        goto fail;
    if (is->subpq().init(&ps->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        goto fail;
    if (is->sampq().init(&ps->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        goto fail;

    ps->videoq.init();
    ps->audioq.init();
    ps->subtitleq.init();

    is->continue_read_thread.create() ;

    is->vidclk.init_clock(&ps->videoq.serial);
    is->audclk.init_clock(&ps->audioq.serial);
    is->extclk.init_clock(&is->extclk.serial);
    is->audio_clock_serial = -1;
    is->av_sync_type = gOptions.av_sync_type;
    ps->read_thread.start(AVStreamsParser::read_thread_func, is);
    if (!(bool)ps->read_thread) {
fail:
        movie_close(is);
        return NULL;
    }
    return is;
}

void do_exit(VideoState *is)
{
    if (is) {
        movie_close(is);
    }
    av_lockmgr_register(NULL);
    uninit_opts();
#if CONFIG_AVFILTER
    av_freep(&gOptions.vfilters_list);
#endif
    avformat_network_deinit();
    if (gOptions.show_status)
        printf("\n");
    SDL_Quit();
    av_log(NULL, AV_LOG_QUIET, "%s", "");
    exit(0);
}


