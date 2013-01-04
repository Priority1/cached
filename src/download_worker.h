/*
 * download_worker.h
 *
 *  Created on: Nov 7, 2010
 *      Author: pr1
 */
#include <curl/curl.h>

#ifndef DOWNLOAD_WORKER_H_
#define DOWNLOAD_WORKER_H_


static int do_mkdir(const char*, mode_t);
int mkpath(const char*, mode_t);
int initpath(char*);
int download(char*);
int download_ok(char*, int);
static int curl_trace(CURL *, curl_infotype , char *, size_t , void *);
static int curl_handle_error(CURLcode, char*);
static int curl_progress_func(void*, double, double, double, double);
char* getupstream();

struct data {
  char trace_ascii; /* 1 or 0 */
};

upstreams *upstr_ptr;
static int upstream_count;
static int last_upstream = 1;

#endif /* DOWNLOAD_WORKER_H_ */
