/*
 * =====================================================================================
 *
 *       Filename:  fstream.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/09/2015 07:44:47 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  DAI ZHENGHUA (), djx.zhenghua@gmail.com
 *        Company:  
 *
 * =====================================================================================
 */
#include <unistd.h>
#include "fstream.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static 
ssize_t default_ff_read (int __fd, void *__buf, size_t __nbytes, size_t __off)
{
    return read(__fd, __buf, __nbytes);
}

static
int default_ff_stat (int __fd, struct stat *__buf)
{
   return fstat(__fd, __buf); 
}

static
int default_ff_seek (int __fd,  int64_t pos, int whence)
{
   return lseek (__fd, pos, whence); 
}

static ff_read_filter_func ff_stream_read =  default_ff_read;
static ff_fstat_filter_func ff_stream_stat =  default_ff_stat;
static ff_seek_filter_func ff_stream_seek =  default_ff_seek;

ssize_t ff_read (int __fd, void *__buf, size_t __nbytes)
{
    ssize_t __off = 0;
    return ff_stream_read(__fd, __buf, __nbytes, __off);
}

int ff_fstat (int __fd, struct stat *__buf)
{
   return ff_stream_stat(__fd, __buf); 
}

int64_t ff_seek(int __fd, int64_t pos, int whence)
{
    return ff_stream_seek(__fd, pos, whence);
}

void set_fstream(struct fstream_functors* __fstream)
{
    ff_stream_read = (__fstream->read == NULL)? (default_ff_read):(__fstream->read);
    ff_stream_stat = (__fstream->stat == NULL)? (default_ff_stat) : (__fstream->stat);
    ff_stream_seek = (__fstream->seek == NULL)? (default_ff_seek) : (__fstream->seek);
}
