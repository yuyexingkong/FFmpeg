/*
 * =====================================================================================
 *
 *       Filename:  fstream.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/09/2015 07:43:13 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  DAI ZHENGHUA (), djx.zhenghua@gmail.com
 *        Company:  
 *
 * =====================================================================================
 */


#ifndef  fstream_INC
#define  fstream_INC
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __cplusplus
extern "C"{
#endif
typedef ssize_t (*ff_read_filter_func)(int __fd, void* __buf, size_t __nbytes, size_t __off);
typedef int (*ff_fstat_filter_func)(int __fd, struct stat *__buf);
typedef int64_t (*ff_seek_filter_func)(int __fd, int64_t pos, int whence);

extern ssize_t ff_read (int __fd, void *__buf, size_t __nbytes) __wur;
extern int ff_fstat (int __fd, struct stat *__buf);
extern int64_t ff_seek(int __fd, int64_t pos, int whence);

struct fstream_functors{
    ff_read_filter_func read;
    ff_fstat_filter_func stat;
    ff_seek_filter_func seek;
};

void set_fstream(struct fstream_functors* __fstream);
#ifdef __cplusplus
}
#endif
#endif   /* ----- #ifndef fstream_INC  ----- */
