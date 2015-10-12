/*
 * =====================================================================================
 *
 *       Filename:  options.cpp
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  10/10/2015 10:16:38 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  DAI ZHENGHUA (), djx.zhenghua@gmail.com
 *        Company:  
 *
 * =====================================================================================
 */

#include "ffplayer.h"

static int opt_frame_size(void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "Option -s is deprecated, use -video_size.\n");
    return opt_default(NULL, "video_size", arg);
}

static int opt_width(void *optctx, const char *opt, const char *arg)
{
    gOptions.screen_width = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

static int opt_height(void *optctx, const char *opt, const char *arg)
{
    gOptions.screen_height = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

static int opt_format(void *optctx, const char *opt, const char *arg)
{
    gOptions.file_iformat = av_find_input_format(arg);
    if (!gOptions.file_iformat) {
        av_log(NULL, AV_LOG_FATAL, "Unknown input format: %s\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_frame_pix_fmt(void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "Option -pix_fmt is deprecated, use -pixel_format.\n");
    return opt_default(NULL, "pixel_format", arg);
}

static int opt_sync(void *optctx, const char *opt, const char *arg)
{
    if (!strcmp(arg, "audio"))
        gOptions.av_sync_type = AV_SYNC_AUDIO_MASTER;
    else if (!strcmp(arg, "video"))
        gOptions.av_sync_type = AV_SYNC_VIDEO_MASTER;
    else if (!strcmp(arg, "ext"))
        gOptions.av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
    else {
        av_log(NULL, AV_LOG_ERROR, "Unknown value for %s: %s\n", opt, arg);
        exit(1);
    }
    return 0;
}

static int opt_seek(void *optctx, const char *opt, const char *arg)
{
    gOptions.start_time = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_duration(void *optctx, const char *opt, const char *arg)
{
    gOptions.duration = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_show_mode(void *optctx, const char *opt, const char *arg)
{
    gOptions.show_mode = !strcmp(arg, "video") ? SHOW_MODE_VIDEO :
        !strcmp(arg, "waves") ? SHOW_MODE_WAVES :
        !strcmp(arg, "rdft" ) ? SHOW_MODE_RDFT  :
        (ShowMode)(size_t)parse_number_or_die(opt, arg, OPT_INT, 0, SHOW_MODE_NB-1);
    return 0;
}

void opt_input_file(void *optctx, const char *filename)
{
    if (gOptions.input_filename) {
        av_log(NULL, AV_LOG_FATAL,
                "Argument '%s' provided as input filename, but '%s' was already specified.\n",
                filename, gOptions.input_filename);
        exit(1);
    }
    if (!strcmp(filename, "-"))
        filename = "pipe:";
    gOptions.input_filename = filename;
}

static int opt_codec(void *optctx, const char *opt, const char *arg)
{
    const char *spec = strchr(opt, ':');
    if (!spec) {
        av_log(NULL, AV_LOG_ERROR,
                "No media specifier was specified in '%s' in option '%s'\n",
                arg, opt);
        return AVERROR(EINVAL);
    }
    spec++;
    switch (spec[0]) {
        case 'a' :    gOptions.audio_codec_name = arg; break;
        case 's' : gOptions.subtitle_codec_name = arg; break;
        case 'v' :    gOptions.video_codec_name = arg; break;
        default:
                      av_log(NULL, AV_LOG_ERROR,
                              "Invalid media specifier '%s' in option '%s'\n", spec, opt);
                      return AVERROR(EINVAL);
    }
    return 0;
}

#if CONFIG_AVFILTER
static int opt_add_vfilter(void *optctx, const char *opt, const char *arg)
{
    gOptions.vfilters_list = (const char **) grow_array((void*)gOptions.vfilters_list , sizeof(*gOptions.vfilters_list ), &gOptions.nb_vfilters,gOptions.nb_vfilters + 1);
    gOptions.vfilters_list[gOptions.nb_vfilters - 1] = arg;
    return 0;
}
#endif


static int dummy;
extern "C"
const OptionDef options[] = {
    OptionDef((const char*)"L"          , OPT_EXIT, ((void*) show_license),     (const char*)"show license" ),
    OptionDef((const char*)"h"          , OPT_EXIT, ((void*) show_help),        (const char*)"show help",(const char*)"topic" ),
    OptionDef((const char*)"?"          , OPT_EXIT, ((void*) show_help),        (const char*)"show help",(const char*)"topic" ),
    OptionDef((const char*)"help"       , OPT_EXIT, ((void*) show_help),        (const char*)"show help",(const char*)"topic" ),
    OptionDef((const char*)"-help"      , OPT_EXIT, ((void*) show_help),        (const char*)"show help",(const char*)"topic" ),
    OptionDef((const char*)"version"    , OPT_EXIT, ((void*) show_version),     (const char*)"show version" ),
    OptionDef((const char*)"buildconf"  , OPT_EXIT, ((void*) show_buildconf),   (const char*)"show build configuration" ),
    OptionDef((const char*)"formats"    , OPT_EXIT, ((void*) show_formats  ),   (const char*)"show available formats" ),
    OptionDef((const char*)"devices"    , OPT_EXIT, ((void*) show_devices  ),   (const char*)"show available devices" ),
    OptionDef((const char*)"codecs"     , OPT_EXIT, ((void*) show_codecs   ),   (const char*)"show available codecs" ),
    OptionDef((const char*)"decoders"   , OPT_EXIT, ((void*) show_decoders ),   (const char*)"show available decoders" ),
    OptionDef((const char*)"encoders"   , OPT_EXIT, ((void*) show_encoders ),   (const char*)"show available encoders" ),
    OptionDef((const char*)"bsfs"       , OPT_EXIT, ((void*) show_bsfs     ),   (const char*)"show available bit stream filters" ),
    OptionDef((const char*)"protocols"  , OPT_EXIT, ((void*) show_protocols),   (const char*)"show available protocols" ),
    OptionDef((const char*)"filters"    , OPT_EXIT, ((void*) show_filters  ),   (const char*)"show available filters" ),
    OptionDef((const char*)"pix_fmts"   , OPT_EXIT, ((void*) show_pix_fmts ),   (const char*)"show available pixel formats" ),
    OptionDef((const char*)"layouts"    , OPT_EXIT, ((void*) show_layouts  ),   (const char*)"show standard channel layouts" ),
    OptionDef((const char*)"sample_fmts", OPT_EXIT, ((void*) show_sample_fmts ),(const char*)"show available audio sample formats" ),
    OptionDef((const char*)"colors"     , OPT_EXIT, ((void*) show_colors ),     (const char*)"show available color names" ),
    OptionDef((const char*)"loglevel"   , HAS_ARG,  ((void*) opt_loglevel),     (const char*)"set logging level",(const char*)"loglevel" ),
    OptionDef((const char*)"v",           HAS_ARG,  ((void*) opt_loglevel),     (const char*)"set logging level",(const char*)"loglevel" ),
    OptionDef((const char*)"report"     , 0,        ((void*)opt_report),(const char*)"generate a report" ),
    OptionDef((const char*)"max_alloc"  , HAS_ARG,  ((void*) opt_max_alloc),    (const char*)"set maximum size of a single allocated block",(const char*)"bytes" ),
    OptionDef((const char*)"cpuflags"   , HAS_ARG | OPT_EXPERT, ( (void*) opt_cpuflags ),(const char*)"force specific cpu flags",(const char*)"flags" ),
    OptionDef((const char*)"hide_banner", OPT_BOOL | OPT_EXPERT, (&hide_banner),    (const char*)"do not show program banner",(const char*)"hide_banner" ),
#if CONFIG_OPENCL
    OptionDef((const char*)"opencl_bench", OPT_EXIT, ((void*) opt_opencl_bench),(const char*)"run benchmark on all OpenCL devices and show results" ),
    OptionDef((const char*)"opencl_options", HAS_ARG, ((void*) opt_opencl),     (const char*)"set OpenCL environment options" ),
#endif
#if CONFIG_AVDEVICE
    OptionDef((const char*)"sources"    , OPT_EXIT | HAS_ARG, ( (void*) show_sources ),
            (const char*)"list sources of the input device",(const char*)"device" ),
    OptionDef((const char*)"sinks"      , OPT_EXIT | HAS_ARG, ( (void*) show_sinks ),
            (const char*)"list sinks of the output device",(const char*)"device" ),
#endif
    OptionDef((const char*)"x", HAS_ARG, (  (void*)opt_width ),(const char*)"force displayed width",(const char*)"width" ),
    OptionDef((const char*)"y", HAS_ARG, ((void*)(opt_height) ),(const char*)"force displayed height",(const char*)"height" ),
    OptionDef((const char*)"s", HAS_ARG | OPT_VIDEO, ( (void*)(opt_frame_size) ),(const char*)"set frame size (WxH or abbreviation)",(const char*)"size" ),
    OptionDef((const char*)"fs", OPT_BOOL, ( &gOptions.is_full_screen ),(const char*)"force full screen" ),
    OptionDef((const char*)"an", OPT_BOOL, ( &gOptions.audio_disable ),(const char*)"disable audio" ),
    OptionDef((const char*)"vn", OPT_BOOL, ( &gOptions.video_disable ),(const char*)"disable video" ),
    OptionDef((const char*)"sn", OPT_BOOL, ( &gOptions.subtitle_disable ),(const char*)"disable subtitling" ),
    OptionDef((const char*)"ast", OPT_STRING | HAS_ARG | OPT_EXPERT, ( &gOptions.wanted_stream_spec[AVMEDIA_TYPE_AUDIO] ),(const char*)"select desired audio stream",(const char*)"stream_specifier" ),
    OptionDef((const char*)"vst", OPT_STRING | HAS_ARG | OPT_EXPERT, ( &gOptions.wanted_stream_spec[AVMEDIA_TYPE_VIDEO] ),(const char*)"select desired video stream",(const char*)"stream_specifier" ),
    OptionDef((const char*)"sst", OPT_STRING | HAS_ARG | OPT_EXPERT, ( &gOptions.wanted_stream_spec[AVMEDIA_TYPE_SUBTITLE] ),(const char*)"select desired subtitle stream",(const char*)"stream_specifier" ),
    OptionDef((const char*)"ss", HAS_ARG, ( (void*)(opt_seek) ),(const char*)"seek to a given position in seconds",(const char*)"pos" ),
    OptionDef((const char*)"t", HAS_ARG, ( (void*)(opt_duration) ),(const char*)"play  \"duration\" seconds of audio/video",(const char*)"duration" ),
    OptionDef((const char*)"bytes", OPT_INT | HAS_ARG, ( &gOptions.seek_by_bytes ),(const char*)"seek by bytes 0=off 1=on -1=auto",(const char*)"val" ),
    OptionDef((const char*)"nodisp", OPT_BOOL, ( &gOptions.display_disable ),(const char*)"disable graphical display" ),
    OptionDef((const char*)"f", HAS_ARG, ( (void*)(opt_format) ),(const char*)"force format",(const char*)"fmt" ),
    OptionDef((const char*)"pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO, ( (void*)(opt_frame_pix_fmt) ),(const char*)"set pixel format",(const char*)"format" ),
    OptionDef((const char*)"stats", OPT_BOOL | OPT_EXPERT, ( &gOptions.show_status ),(const char*)"show status",(const char*)"" ),
    OptionDef((const char*)"fast", OPT_BOOL | OPT_EXPERT, ( &gOptions.fast ),(const char*)"non spec compliant optimizations",(const char*)"" ),
    OptionDef((const char*)"genpts", OPT_BOOL | OPT_EXPERT, ( &gOptions.genpts ),(const char*)"generate pts",(const char*)"" ),
    OptionDef((const char*)"drp", OPT_INT | HAS_ARG | OPT_EXPERT, ( &gOptions.decoder_reorder_pts ),(const char*)"let decoder reorder pts 0=off 1=on -1=auto",(const char*)""),
    OptionDef((const char*)"lowres", OPT_INT | HAS_ARG | OPT_EXPERT, ( &gOptions.lowres ),(const char*)"",(const char*)"" ),
    OptionDef((const char*)"sync", HAS_ARG | OPT_EXPERT, ( (void*)(opt_sync) ),(const char*)"set audio-video sync. type (type=audio/video/ext)",(const char*)"type" ),
    OptionDef((const char*)"autoexit", OPT_BOOL | OPT_EXPERT, ( &gOptions.autoexit ),(const char*)"exit at the end",(const char*)"" ),
    OptionDef((const char*)"exitonkeydown", OPT_BOOL | OPT_EXPERT, ( &gOptions.exit_on_keydown ),(const char*)"exit on key down",(const char*)"" ),
    OptionDef((const char*)"exitonmousedown", OPT_BOOL | OPT_EXPERT, ( &gOptions.exit_on_mousedown ),(const char*)"exit on mouse down",(const char*)"" ),
    OptionDef((const char*)"loop", OPT_INT | HAS_ARG | OPT_EXPERT, ( &gOptions.loop ),(const char*)"set number of times the playback shall be looped",(const char*)"loop count" ),
    OptionDef((const char*)"framedrop", OPT_BOOL | OPT_EXPERT, ( &gOptions.framedrop ),(const char*)"drop frames when cpu is too slow",(const char*)"" ),
    OptionDef((const char*)"infbuf", OPT_BOOL | OPT_EXPERT, ( &gOptions.infinite_buffer ),(const char*)"don't limit the input buffer size (useful with realtime streams)",(const char*)"" ),
    OptionDef((const char*)"window_title", OPT_STRING | HAS_ARG, ( &gOptions.window_title ),(const char*)"set window title",(const char*)"window title" ),
#if CONFIG_AVFILTER
    OptionDef((const char*)"vf", OPT_EXPERT | HAS_ARG, ( (void*)(opt_add_vfilter) ),(const char*)"set video filters",(const char*)"filter_graph" ),
    OptionDef((const char*)"af", OPT_STRING | HAS_ARG, ( &gOptions.afilters ),(const char*)"set audio filters",(const char*)"filter_graph" ),
#endif
    OptionDef((const char*)"rdftspeed", OPT_INT | HAS_ARG| OPT_AUDIO | OPT_EXPERT, ( &gOptions.rdftspeed ),(const char*)"rdft speed",(const char*)"msecs" ),
    OptionDef((const char*)"showmode", HAS_ARG, ( (void*)(opt_show_mode)),(const char*)"select show mode (0 = video, 1 = waves, 2 = RDFT)",(const char*)"mode" ),
    OptionDef((const char*)"default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, ( (void*)(opt_default )),(const char*)"generic catch all option",(const char*)"" ),
    OptionDef((const char*)"i", OPT_BOOL, ( &dummy),(const char*)"read specified file",(const char*)"input_file"),
    OptionDef((const char*)"codec", HAS_ARG, ( (void*)(opt_codec)),(const char*)"force decoder",(const char*)"decoder_name" ),
    OptionDef((const char*)"acodec", HAS_ARG | OPT_STRING | OPT_EXPERT, (    &gOptions.audio_codec_name ),(const char*)"force audio decoder",   (const char*)"decoder_name" ),
    OptionDef((const char*)"scodec", HAS_ARG | OPT_STRING | OPT_EXPERT, ( &gOptions.subtitle_codec_name ),(const char*)"force subtitle decoder",(const char*)"decoder_name" ),
    OptionDef((const char*)"vcodec", HAS_ARG | OPT_STRING | OPT_EXPERT, (    &gOptions.video_codec_name ),(const char*)"force video decoder",   (const char*)"decoder_name" ),
    OptionDef((const char*)"autorotate", OPT_BOOL, ( &gOptions.autorotate ),(const char*)"automatically rotate video",(const char*)"" ),
    OptionDef( NULL, 0, ((void*)0), NULL, NULL)
};


