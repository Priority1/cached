/*
 * cached.h
 *
 *  Created on: Nov 5, 2010
 *      Author: pr1
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include <my_global.h>
#include <mysql.h>
#include <mysql/errmsg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pcreposix.h>
#include <libconfig.h>
#include <string.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h> //shm_open
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pwd.h>


#ifndef CACHED_H_
#define CACHED_H_

#define MAX_WORKERS 20
#define MAX_MSG_LEN 256
#define MAX_MSG_LEN_CH 10000
#define PATH_MAX 4096 /* # chars in a path name including nul */
#define URL_MAX	2100
#define QUERY_MAX 4096
#define QUEUE	"/workers_queue"
#define SH_KEY 5678

/* Loglevels */
#define ERROR 1
#define WARN 2
#define DEBUG 3

/* Workers control struct */
typedef struct {
	pid_t wpid;
	int id;
} workers;

/* Upstreams for SHM*/
typedef struct {
	char upstream[255];
	int	alive;
	time_t deadtime;
} upstreams;

// Process controls
void fparser_init();
void downloader_init(upstreams*, mqd_t);

void fparser_term();
void downloader_term();

void worker_init(upstreams*, mqd_t);
void worker_term();
pid_t spawn_worker(int, char*[], int);

void master_term();
void master_chld(int);
void rotate_log();
void rotate_log_master();

void error_log(int, char*, ...);
int parse_cmd_line(int, char*[]);

// Config struct
struct config_t cfg;
char* cfg_get_str(char*);
int cfg_get_int(char*);

// SHM && MQ
upstreams* init_shm_upstr(int);
int close_shm_upstr(int, upstreams*);
mqd_t init_mq();
int close_mq();

// MySQL
MYSQL* init_mysql(MYSQL*);
int connect_mysql(MYSQL*);
int query_mysql(MYSQL*, char*);

// Some callbacks
void setname(char*);
char* getname();

void die(char *);

#endif /* CACHED_H_ */
