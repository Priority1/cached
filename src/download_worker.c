/*
 * download_worker.c
 *
 *  Created on: Nov 7, 2010
 *      Author: pr1
 */
#include "cached.h"
#include "download_worker.h"

static int running = 1;

void worker_init(upstreams *upstr_ptr, mqd_t msgq_id) {
	char msgcontent[MAX_MSG_LEN_CH];
	int msgsz;
	unsigned int sender;
	struct mq_attr msgq_attr;
	struct timespec tm;
	const config_setting_t *upstr_setting;

	upstr_setting = config_lookup(&cfg, "upstreams");
	upstream_count = config_setting_length(upstr_setting);

	while (running) {
		sleep(1);
		mq_getattr(msgq_id, &msgq_attr);
		error_log(DEBUG,
				"Queue \"%s\":\n\t- stores at most %ld messages\n\t- large at most %ld bytes each\n\t- currently holds %ld messages\n",
				QUEUE, msgq_attr.mq_maxmsg, msgq_attr.mq_msgsize,
				msgq_attr.mq_curmsgs);

		/* getting a message */
		clock_gettime(CLOCK_REALTIME, &tm);
		tm.tv_sec += 1;

		msgsz = mq_timedreceive(msgq_id, msgcontent, MAX_MSG_LEN_CH, &sender,
				&tm);
		if (msgsz == -1) {
			if (errno == ETIMEDOUT) {
				continue;
			}
			error_log(ERROR, "mq_receive error: %s", strerror(errno));
			exit(EXIT_FAILURE);
		}

		/* Ok, we got file to download */
		if (initpath(msgcontent) != 0) {
			error_log(ERROR, "Initpath failed");
		} else if (download(msgcontent) != 0) {
			error_log(ERROR, "Failed to download file");
		}
	}
	error_log(ERROR, "Exiting");
}

int mkpath(const char *path, mode_t mode) {
	char *pp;
	char *sp;
	int status;
	char *copypath = strdup(path);

	status = 0;
	pp = copypath;
	while (status == 0 && (sp = strchr(pp, '/')) != 0) {
		if (sp != pp) {
			/* Neither root nor double slash in path */
			*sp = '\0';
			status = do_mkdir(copypath, mode);
			*sp = '/';
		}
		pp = sp + 1;
	}
	if (status == 0)
		status = do_mkdir(path, mode);
	free(copypath);
	return (status);
}

static int do_mkdir(const char *path, mode_t mode) {
	struct stat st;
	int status = 0;

	if (stat(path, &st) != 0) {
		/* Directory does not exist */
		if (mkdir(path, mode) != 0)
			status = -1;
	} else if (!S_ISDIR(st.st_mode)) {
		errno = ENOTDIR;
		status = -1;
	}

	return (status);
}

int initpath(char* path) {
	regex_t f;
	regmatch_t p[1];
//	int i, j;
	int error;
	char erbuf[256];
	char* regex = "(.*\/)";
	char* dirname;
	char cpath[PATH_MAX];
	int returnval = 0;

	error = regcomp(&f, regex, 0);
	if (error) {
		regerror(error, &f, erbuf, 256);
		error_log(ERROR, "Error in log_regex pattern: %s , error: %s", regex,
				erbuf);
		return -1;
	}

	error = regexec(&f, path, 1, p, 0);
	if (error) {
		return 0;
	}
	if (p[0].rm_eo != -1) {
		int len = p[0].rm_eo - p[0].rm_so;
		dirname = (char *) malloc((len + 1) * sizeof(char));
		strncpy(dirname, path + p[0].rm_so, len);
		dirname[len] = 0;
		sprintf(cpath, "%s/%s", cfg_get_str("cache_store"), dirname);
		if ((returnval = mkpath(cpath, 0755)) != 0) {
			error_log(ERROR, "failed to create: %s", cpath);
		}
		free(dirname);
	}
	return 0;
}

char* getupstream() {
	int i;
	for (i = 0; i < upstream_count; i++) {
		if (i == last_upstream) {
			last_upstream = i < (upstream_count - 1) ? i + 1 : 0;
			if (upstr_ptr[last_upstream].alive) {
				break;
			} else {
				i = 0;
			}
		}
	}
	return upstr_ptr[last_upstream].upstream;
}

static int curl_trace(CURL *handle, curl_infotype type, char *data, size_t size,
		void *userp) {
//	struct data *config = (struct data *) userp;
	const char *text = "";
	(void) handle; /* prevent compiler warning */

	switch (type) {
	case CURLINFO_TEXT:
		error_log(DEBUG, "Curl_debug[info]: %s", data);
		break;
	default: /* in case a new one is introduced to shock us */
		return 0;

	case CURLINFO_HEADER_OUT:
		text = "Send header";
		return 0;
		break;
	case CURLINFO_DATA_OUT:
		text = "Send data";
		return 0;
		break;
	case CURLINFO_SSL_DATA_OUT:
		text = "Send SSL data";
		return 0;
		break;
	case CURLINFO_HEADER_IN:
		text = "Recv header";
		return 0;
		break;
	case CURLINFO_DATA_IN:
		text = "Recv data";
		return 0;
		break;
	case CURLINFO_SSL_DATA_IN:
		text = "SSL data";
		return 0;
		break;
	}

	error_log(DEBUG, "Curl_debug[%s]: %s", text, (unsigned char *) data);
	return 0;
}

static int curl_handle_error(CURLcode result, char* error) {
	switch (result) {
	case CURLE_COULDNT_RESOLVE_HOST:
	case CURLE_COULDNT_CONNECT:
	case CURLE_PARTIAL_FILE:
	case CURLE_OPERATION_TIMEDOUT:
	case CURLE_BAD_DOWNLOAD_RESUME:
	case CURLE_TOO_MANY_REDIRECTS:
	case CURLE_GOT_NOTHING:
		upstr_ptr[last_upstream].alive = 0;
		error_log(WARN,"Marking %s as dead because of [%d] curl error",
				upstr_ptr[last_upstream].upstream, result);
		upstr_ptr[last_upstream].deadtime = time(NULL);
		return 1;
	case CURLE_HTTP_RETURNED_ERROR:
		if (strcmp(error, "The requested URL returned error: 404") != 0) {
			error_log(WARN,"Marking %s as dead because of [%d] curl error",
					upstr_ptr[last_upstream].upstream, result);
			upstr_ptr[last_upstream].alive = 0;
			upstr_ptr[last_upstream].deadtime = time(NULL);
		} else {
			error_log(WARN,"File not found on %s, trying next upstream",
					upstr_ptr[last_upstream].upstream);
		}
		return 1;
	default:
		return 0;
	}
	return 0;
}

static int curl_progress_func(void* p, double TotalToDownload,
		double NowDownloaded, double TotalToUpload, double NowUploaded) {
	return running ? 0 : -1;
}

int download(char* file) {
	CURL* curl;
	CURLcode result;
	struct data config;
	FILE* fp;
	char bufferError[CURL_ERROR_SIZE];
	char url[URL_MAX];
	char tmp[PATH_MAX];
	int returnval = 0;
	int retries = 0;
	int success = 0;
	char fullpath[PATH_MAX];
	config.trace_ascii = 1;

	while (!success && retries < upstream_count) {
		sprintf(url, "http://%s/%s", getupstream(), file);
		sprintf(tmp, "%s/%s.tmp", cfg_get_str("cache_store"), file);
		sprintf(fullpath, "%s/%s", cfg_get_str("cache_store"), file);
		curl = curl_easy_init();
		if (curl) {
			fp = fopen(tmp, "wb");
			curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, bufferError);
			curl_easy_setopt(curl, CURLOPT_URL, url);
			curl_easy_setopt(curl, CURLOPT_HEADER, 0);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
			curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
			curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION,
					curl_progress_func);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT,
					cfg_get_int("curl_timeout"));
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
					cfg_get_int("curl_connect_timeout"));
			if (cfg_get_int("log_level") >= 3) {
				curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_trace);
				curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &config);
				curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
			}
			result = curl_easy_perform(curl);
			curl_easy_cleanup(curl);
			fclose(fp);
		}
		if (result == CURLE_OK) {
			if (rename(tmp, fullpath) == 0) {
				download_ok(file, cfg_get_int("fe_id"));
				error_log(WARN, "Successfully downloaded: %s", fullpath);
				returnval = 0;
				success = 1;
			} else {
				remove(tmp);
				error_log(ERROR, "Rename failure: %s to %s", tmp, fullpath);
				returnval = -1;
			}
		} else {
			retries++;
			remove(tmp);
			if (curl_handle_error(result, bufferError)) {
				returnval = -1;
				error_log(WARN, "Curl error: %s", bufferError);
			} else {
				returnval = -1;
				success = 1;
				error_log(WARN, "Curl error: %s", bufferError);
			}
		}
	}
	return returnval;
}

int download_ok (char *file, int fe_id) {
	/* Initialise MySQL connection */
	MYSQL *conn;
	static char query[QUERY_MAX];

	if ((conn = init_mysql(conn)) == NULL) {
		mysql_close(conn);
		error_log(ERROR,"MySQL initialisation failed");
		exit(EXIT_FAILURE);
	}

	if (!connect_mysql(conn)) {
		mysql_close(conn);
		error_log(ERROR,"MySQL connection failed");
		exit(EXIT_FAILURE);
	}
	sprintf(query,
			"INSERT INTO cached (id, fe_id, filename, cached_time) "
					"VALUES (md5('%s'), %d, '%s', now())",
			file, fe_id, file);
	query_mysql(conn, query);
	mysql_close(conn);
	return 0;
}

void worker_term() {
	running = 0;
}

