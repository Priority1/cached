/*
 ============================================================================
 Name        : cached.c
 Author      : Andrey I. Feldman
 Version     :
 Copyright   : pr1.ru
 Description : Cached main file
 ============================================================================
 */

#include "cached.h"
#include <mysql.h>

static pid_t fparser_pid, downloader_pid, pid;
char *configfile;
static int running = 1;
const char name[30];
const char wname[20];
const char str[10];
struct flock log_lock = { F_WRLCK, SEEK_SET, 0, 0, 0 };
int logfile;
int logfile_opened = 0;
workers wrk[MAX_WORKERS];
int shmid;
upstreams *upstr_ptr;
mqd_t msgq_id;

int main(int argc, char *argv[]) {
	int argv0size = 10;
	int pidFilehandle;
	struct passwd *pw;

	if (parse_cmd_line(argc, argv) != 0) {
		printf("usage: %s -f /path/to/config/file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	/* Initialize the configuration */
	config_init(&cfg);
	if (!config_read_file(&cfg, configfile)) {
		printf("Error opening config file: %s\n", config_error_text(&cfg));
		exit(EXIT_FAILURE);
	}
	/* let's become a non-root */
	if ((pw = getpwnam(cfg_get_str("user")))) {
		setgid(pw->pw_gid);
		setuid(pw->pw_uid);
	} else {
		printf("Can not sudo to user \"%s\"\n", cfg_get_str("user"));
		exit(EXIT_FAILURE);
	}

	/* Open log file */
	if (!(logfile = fopen(cfg_get_str("log"), "a+"))) {
		printf("Can not open logfile: %s\n", cfg_get_str("log"));
		exit(EXIT_FAILURE);
	}
	logfile_opened = 1;

	/* Ok, daemonizing.... */
	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* obtain a new process group */
	setsid();

	/* Write pid to file */
	pidFilehandle = open(cfg_get_str("pid"), O_RDWR | O_CREAT, 0600);
	if (pidFilehandle == -1) {
		/* Couldn't open lock file */
		error_log("Could not open PID lock file %s, exiting",
				cfg_get_str("pid"));
		exit(EXIT_FAILURE);
	}

	/* Try to lock file */
	if (lockf(pidFilehandle, F_TLOCK, 0) == -1) {
		/* Couldn't get lock on lock file */
		error_log("Could not lock file %s, exiting", cfg_get_str("pid"));
		exit(EXIT_FAILURE);
	}
	/* Get and format PID */
	sprintf(str, "%d\n", getpid());
	/* write pid to lockfile */
	write(pidFilehandle, str, strlen(str));

	setname("master");
	/* Change process names */
	memset(argv[0], 0, strlen(argv[0]));
	memset(argv[1], 0, strlen(argv[1]));
	memset(argv[2], 0, strlen(argv[2]));
	strncpy(argv[0], "cached:", strlen("cached:"));
	strncpy(argv[1], "master", strlen("master"));

	/*Init fparser worker*/
	if ((fparser_pid = fork()) < 0) {
		perror("fork error");
		exit(EXIT_FAILURE);
	}
	if (fparser_pid == 0) {
		strncpy(argv[1], "fparser", strlen("fparser"));
		setname("fparser");
		signal(SIGTERM, fparser_term);
		signal(SIGINT, fparser_term);
		signal(SIGHUP, rotate_log);
		rotate_log();
		fparser_init();
		exit(EXIT_SUCCESS);
	}

	/*Init downloader worker*/
	if ((downloader_pid = fork()) < 0) {
		perror("fork error");
		exit(1);
	}
	if (downloader_pid == 0) {
		strncpy(argv[1], "downloader", strlen("downloader"));
		setname("downloader");
		signal(SIGTERM, downloader_term);
		signal(SIGINT, downloader_term);
		signal(SIGHUP, rotate_log);
		rotate_log();
		upstr_ptr = init_shm_upstr(shmid);
		msgq_id = init_mq();
		downloader_init(upstr_ptr, msgq_id);
		close_shm_upstr(shmid, upstr_ptr);
		close_mq(msgq_id);
		exit(EXIT_SUCCESS);
	}

	/* Init download workers */
	int i;
	for (i = 0; i < MAX_WORKERS && i < cfg_get_int("workers"); i++) {
		wrk[i].id = i;
		wrk[i].wpid = spawn_worker(i, argv, argv0size);
	}
	// Parent >>
	signal(SIGINT, master_term);
	signal(SIGTERM, master_term);
	signal(SIGCHLD, master_chld);
	signal(SIGHUP, rotate_log_master);
	error_log(ERROR,"All workers started");

	while (running) {
		if ((kill(fparser_pid, 0) != 0 || kill(downloader_pid, 0) != 0)
				&& running == 1) {
			error_log(ERROR,"Ooops, critical process dies unexpectedly...");
			master_term();
			return EXIT_SUCCESS;
		}

		for (i = 0; i < MAX_WORKERS && i < cfg_get_int("workers"); i++) {
			if (kill(wrk[i].wpid, 0) != 0 && running == 1) {
				error_log( ERROR,
						"Ooops, worker die, pid: %d id:%d , trying to respawn...",
						wrk[i].wpid, wrk[i].id);
				wrk[i].wpid = spawn_worker(i, argv, argv0size);
			}
		}
		sleep(1);
	}

	waitpid(fparser_pid, NULL, 0);
	waitpid(downloader_pid, NULL, 0);

	for (i = 0; i < MAX_WORKERS && i < cfg_get_int("workers"); i++) {
		waitpid(wrk[i].wpid, NULL, 0);
		error_log("Killed: pid: %d id:%d", wrk[i].wpid, wrk[i].id);
	}
	error_log(ERROR, "Ok, all dead, exiting");
	close(pidFilehandle);
	config_destroy(&cfg);
	return EXIT_SUCCESS;
}

pid_t spawn_worker(int id, char *argv[], int argv0size) {
	pid_t pid;
	if ((pid = fork()) < 0) {
		perror("fork error");
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		snprintf(wname, sizeof(wname), "worker #%d", id);
		setname(wname);
		memset(argv[1], 0, strlen(argv[1]));
		strncpy(argv[1], wname, strlen(wname));
		signal(SIGTERM, worker_term);
		signal(SIGINT, worker_term);
		signal(SIGHUP, rotate_log);
		rotate_log();
		upstr_ptr = init_shm_upstr(shmid);
		msgq_id = init_mq();
		worker_init(upstr_ptr, msgq_id);
		close_shm_upstr(shmid, upstr_ptr);
		close_mq(msgq_id);
		exit(EXIT_SUCCESS);
	}
	return pid;
}

void error_log(int level, char* buff, ...) {
	va_list ap;
	time_t date;
	if (level > cfg_get_int("log_level")) {
		return;
	}
	char* level_name;
	date = time(NULL);
	char date_str[26];
	char string[2000];
	int len;
	int fd = fileno(logfile);
	struct tm* datetime = localtime(&date);
	strftime(date_str, 26, "%c", datetime);
	va_start(ap, buff);
	vsnprintf(string, 2000, buff, ap);
	len = strlen(string);
	if (string[len - 1] == '\n') {
		string[len - 1] = 0;
	}
	va_end(ap);
	log_lock.l_pid = getpid();
	switch(level) {
		case ERROR: level_name = "ERROR"; break;
		case WARN:	level_name = "WARN"; break;
		case DEBUG: level_name = "DEBUG"; break;
		default: level_name = "UNKN"; break;
	}
	if (fcntl(fd, F_SETLKW, &log_lock) == -1) {
		sprintf(stdout, "Error locking logfile from %s pid: %d", getname(),
				getpid());
		perror("fcntl");
		exit(EXIT_FAILURE);
	}
	if (fprintf(logfile, "[%s] [%s] (%s)%d# %s\n", date_str, level_name, getname(), getpid(),
			string) < 0) {
		printf("Cant write to: %s , fprintf() failed", cfg_get_str("log"));
		rotate_log();
	} else {
		fflush(logfile);
	}
	log_lock.l_type = F_UNLCK;
	if (fcntl(fd, F_SETLK, &log_lock) == -1) {
		printf("Error unlocking logfile: from %s pid: %d", getname(), getpid());
		perror("fcntl");
		exit(EXIT_FAILURE);
	}
}

void rotate_log_master() {
	rotate_log();
	int i;
	for (i = 0; i < MAX_WORKERS && i < cfg_get_int("workers"); i++) {
		kill(wrk[i].wpid, SIGHUP);
	}
	kill(downloader_pid, SIGHUP);
	kill(fparser_pid, SIGHUP);
	error_log(ERROR, "Log rotate complete");

}

void rotate_log() {
	fclose(logfile);
	if (!(logfile = fopen(cfg_get_str("log"), "a+"))) {
		printf("Cant open logfile: %s", cfg_get_str("log"));
		master_term();
		exit(EXIT_FAILURE);
	}
}

void setname(char* wname) {
	strncpy(name, wname, sizeof(name));
}

char* getname() {
	return name;
}

void master_term() {
	running = 0;
	signal(SIGCHLD, NULL);
	int i;
	for (i = 0; i < MAX_WORKERS && i < cfg_get_int("workers"); i++) {
		kill(wrk[i].wpid, SIGINT);
	}
	kill(downloader_pid, SIGINT);
	kill(fparser_pid, SIGINT);
}

void master_chld(int sig) {
	pid_t pid;
	pid = wait(NULL);
	error_log("%d dies with signal %d", pid, sig);
}

int parse_cmd_line(int argc, char *argv[]) {
	int c;
	extern char *optarg;
	extern int optind, optopt, opterr;

	while ((c = getopt(argc, argv, "f:")) != -1) {
		switch (c) {
		case 'f':
			configfile = optarg;
			return EXIT_SUCCESS;
		case '?':
			fprintf(stderr, "Unrecognized option: -%c\n", optopt);
			return EXIT_FAILURE;
		case ':':
			fprintf(stderr, "-%c without filename\n", optopt);
			return EXIT_FAILURE;
		}
	}
	return EXIT_FAILURE;
}

char* cfg_get_str(char* conf_string) {
	char *value = NULL;
	if (!config_lookup_string(&cfg, conf_string, &value)) {
		if (logfile_opened) {
			error_log("failed to read string variable from config: %s",
					conf_string);
		} else {
			printf("failed to read string variable from config: %s",
					conf_string);
		}
		exit(EXIT_FAILURE);
	}
	return value;
}

int cfg_get_int(char* conf_string) {
	long value;
	if (!config_lookup_int(&cfg, conf_string, &value)) {
		if (logfile_opened) {
			error_log("failed to read string variable from config: %s",
					conf_string);
		} else {
			printf("failed to read string variable from config: %s",
					conf_string);
		}
		exit(EXIT_FAILURE);
	}
	return (int) (value + 0.5);
}

MYSQL* init_mysql(MYSQL* conn) {
	conn = mysql_init(NULL);
	my_bool reconnect = 1;
	mysql_options(&conn, MYSQL_OPT_RECONNECT, &reconnect);

	if (conn == NULL) {
		error_log(ERROR,"Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
		return NULL;
	}
	return conn;
}

int connect_mysql(MYSQL* conn) {
	if (mysql_real_connect(conn, cfg_get_str("mysql_host"),
			cfg_get_str("mysql_login"), cfg_get_str("mysql_password"),
			cfg_get_str("mysql_db"), 0, NULL, 0) == NULL) {
		error_log(ERROR,"Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
		return 0;
	}
	return 1;
}

int query_mysql(MYSQL* conn, char* query) {
	int retval;
	int retries = 0;
	db_retry: if ((retval = mysql_query(conn, query)) != 0 && retries <= 5) {
		switch (mysql_errno(conn)) {
		case CR_COMMANDS_OUT_OF_SYNC:
			error_log(ERROR, "Commands were executed in an improper order.");
			break;
		case CR_SERVER_GONE_ERROR:
		case CR_SERVER_LOST:
			error_log(WARN,
					"The connection to the server lost. Trying to reconnect... ");
			connect_mysql(conn);
			sleep(3);
			retries++;
			goto db_retry;
			break;
		case CR_UNKNOWN_ERROR:
			error_log(ERROR,"An unknown error occurred. ");
			return retval;
			break;
		default:
			error_log(ERROR,"[MYSQL] An error occured. We try to execute \"%s\" but got: %u: %s", query,
					mysql_errno(conn), mysql_error(conn));
			break;
		}
	}
	return retval;
}


upstreams* init_shm_upstr(int shmid) {
	/* Initialise struct of upstreams, and place them into SHM */
    key_t key;

	int n;
	const config_setting_t *upstr_setting;
	static int upstream_count;

	upstr_setting = config_lookup(&cfg, "upstreams");
	upstream_count = config_setting_length(upstr_setting);
    key = SH_KEY+upstream_count;

	if (upstream_count < 1) {
		error_log(ERROR,"failed to read upstreams from config");
		exit(EXIT_FAILURE);
	}

	upstreams *upstr_ptr;
	upstreams upstr[upstream_count];

	shmctl(shmid, IPC_RMID, NULL);

    if ((shmid = shmget(key, sizeof(upstreams)*upstream_count, IPC_CREAT | 0666)) < 0) {
		error_log(ERROR,"failed to shmget");
		exit(EXIT_FAILURE);
    }

	if((upstr_ptr = (upstreams *) shmat(shmid, (void*)0, 0)) == (char *) -1) {
		error_log(ERROR,"failed to shmat");
		exit(EXIT_FAILURE);
	}

	for (n = 0; n < upstream_count; n++) {
		strcpy(upstr[n].upstream, config_setting_get_string_elem(
				upstr_setting, n));
		upstr[n].alive = 1;
		upstr[n].deadtime = time(NULL);
		memcpy(&upstr_ptr[n], &upstr[n], sizeof(upstreams) );
	}

	return upstr_ptr;
}

int close_shm_upstr(int shmid, upstreams *upstr_ptr) {
	shmdt(upstr_ptr);
	shmctl(shmid, IPC_RMID, NULL);
	return 0;
}

mqd_t init_mq() {
	mqd_t msgq_id;
	unsigned int msgprio = 1;
	msgq_id = mq_open(QUEUE, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG, NULL);
	if (msgq_id == (mqd_t) -1) {
		error_log("mq_open error :  %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	return msgq_id;
}

int close_mq() {
	mq_close(QUEUE);
	mq_unlink(QUEUE);
	return 0;
}
