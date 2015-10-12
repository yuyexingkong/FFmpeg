/*
 * =====================================================================================
 *
 *       Filename:  display.h
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

#ifndef  FFPLAY_DISPLAY_INC
#define  FFPLAY_DISPLAY_INC

#include <SDL.h>
class VideoState;
class Display
{
    public:
        void fill_rectangle(int x, int y, int w, int h, int color, int update);
        void fill_border(int xleft, int ytop, int width, int height, int x, int y, int w, int h, int color, int update);
        void video_image_display(VideoState *is);
        void alloc_picture(VideoState *is);
        int video_screen_open(VideoState *is, int force_set_video_mode, Frame *vp);
        void set_default_window_size(int width, int height, AVRational sar);
        void video_audio_display(VideoState *s);
        void video_display(VideoState *is);
        void video_refresh(void *opaque, double *remaining_time);
        void screen_resize(int w, int h, int bpp, unsigned int flags);
        bool hasScreen(){ return screen != NULL;}

        int getScreenWidth(){return screen->w;}
        int getScreenHeight(){return screen->h;}
        int rgb(unsigned char r, unsigned char g, unsigned b){return  SDL_MapRGB(screen->format, r, g, b);}
    public:
        int default_width  = 640;
        int default_height = 480;
        SDL_Surface *screen;

};
#endif   /* ----- #ifndef FFPLAY_DISPLAY_INC  ----- */
