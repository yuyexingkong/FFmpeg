/*
 * =====================================================================================
 *
 *       Filename:  CONTROL.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/22/2015 09:18:39 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  DAI ZHENGHUA (), djx.zhenghua@gmail.com
 *        Company:  
 *
 * =====================================================================================
 */

#ifndef  FFPLAY_CONTROL_INC
#define  FFPLAY_CONTROL_INC
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include "display.h"
#include "ptr.h"

class VideoState;
class FFPlayController 
{
    public:
        void stream_toggle_pause();
        void toggle_pause();
        void toggle_full_screen();
        void toggle_audio_display();
        void seek_chapter(int incr);
        void step_to_next_frame();
        void stream_seek(int64_t pos, int64_t rel, int seek_by_bytes);


    public:
        void Set(VideoState* is) { this->is = is;}
    public:
        int abort_request;
        int force_refresh;
        int paused;
        int last_paused;
        int queue_attachments_req;
        int seek_req;
        int seek_flags;
        int64_t seek_pos;
        int64_t seek_rel;
        int read_pause_return;
    private:
        user_ptr<VideoState> is;
};
#endif   /* ----- #ifndef FFPLAY_CONTROL_INC  ----- */
