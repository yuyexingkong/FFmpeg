/*
 * =====================================================================================
 *
 *       Filename:  clock.cpp
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/26/2015 03:48:35 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  DAI ZHENGHUA (), djx.zhenghua@gmail.com
 *        Company:  
 *
 * =====================================================================================
 */

#include "ffplayer.h"

double Clock::get_clock()
{
    Clock* c = this;
    if (*c->queue_serial != c->serial)
        return NAN;
    if (c->paused) {
        return c->pts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

void Clock::set_clock_at(double pts, int serial, double time)
{
    Clock* c = this;
    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
    c->serial = serial;
}

void Clock::set_clock(double pts, int serial)
{
    Clock* c = this;
    double time = av_gettime_relative() / 1000000.0;
    c->set_clock_at(pts, serial, time);
}

void Clock::set_clock_speed(double speed)
{
    Clock* c = this;
    set_clock(get_clock(), c->serial);
    c->speed = speed;
}

void Clock::init_clock(int *queue_serial)
{
    Clock* c = this;
    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = queue_serial;
    set_clock(NAN, -1);
}

void Clock::sync_clock_to_slave(Clock *slave)
{
    Clock* c = this;
    double clock = c->get_clock();
    double slave_clock = slave -> get_clock();
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set_clock(slave_clock, slave->serial);
}
/* get the current master clock value */
double get_master_clock(VideoState *is)
{
    double val;

    switch (get_master_sync_type(is)) {
        case AV_SYNC_VIDEO_MASTER:
            val = is->vidclk.get_clock();
            break;
        case AV_SYNC_AUDIO_MASTER:
            val = is->audclk.get_clock();
            break;
        default:
            val = is->extclk.get_clock();
            break;
    }
    return val;
}

void check_external_clock_speed(VideoState *is) {
    AVStreamsParser* ps = is->getAVStreamsParser();
    if (ps->video_stream >= 0 && ps->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES ||
            ps->audio_stream >= 0 && ps->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) {
        is->extclk. set_clock_speed(FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
    } else if ((ps->video_stream < 0 || ps->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
            (ps->audio_stream < 0 || ps->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
        is->extclk. set_clock_speed(FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
    } else {
        double speed = is->extclk.speed;
        if (speed != 1.0)
            is->extclk.set_clock_speed(speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
    }
}



