/*
 * downloader.c
 *
 *  Created on: Nov 5, 2010
 *      Author: pr1
 */

#include "cached.h"
#include "downloader.h"

static int running = 1;

MYSQL *conn;

void downloader_init(upstreams *upstr_ptr, mqd_t msgq_id) {

	/* Initialise MySQL connection */
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_fields;

	if ((conn = init_mysql(conn)) == NULL) {
		mysql_close(conn);
		error_log(ERROR, "MySQL initialisation failed");
		exit(EXIT_FAILURE);
	}

	if (!connect_mysql(conn)) {
		mysql_close(conn);
		error_log(ERROR, "MySQL connection failed");
		exit(EXIT_FAILURE);
	}

	int n;
	const config_setting_t *upstr_setting;
	static int upstream_count;

	upstr_setting = config_lookup(&cfg, "upstreams");
	upstream_count = config_setting_length(upstr_setting);

	error_log(ERROR, "Connected to MySQL.");
	struct mq_attr msgq_attr;
	int fe_id = cfg_get_int("fe_id");
	static char query[QUERY_MAX];
	unsigned int msgprio = 1;

	while (running) {
		/* Schedule upstreams */
		for (n = 0; n < upstream_count; n++) {
			if (!upstr_ptr[n].alive
					&& (time(NULL) - upstr_ptr[n].deadtime)
							> cfg_get_int("upstream_dead_timeout")) {
				error_log(DEBUG,
						"Making %s live again, time of dead: %d, now: %d",
						upstr_ptr[n].upstream, upstr_ptr[n].deadtime,
						time(NULL));
				upstr_ptr[n].alive = 1;
			}
			error_log(DEBUG, "Upstream: %s, active: %d", upstr_ptr[n].upstream,
					upstr_ptr[n].alive);
		}
		/* Get latest data */
		mq_getattr(msgq_id, &msgq_attr);
		if (!msgq_attr.mq_curmsgs) {
			if (cfg_get_int("dmode") == 2) {
				/* Query for robin mode*/
				sprintf(query,
						"SELECT filename, size from scoreboard WHERE id NOT "
								"IN(SELECT id from cached where fe_id=%d) AND count >=%d "
								"order by count DESC, last_modified DESC, size limit %d",
						fe_id, cfg_get_int("cache_req_count"),
						cfg_get_int("workers"));
			} else {
				/* Query for shard mode*/
				sprintf(query,
						"SELECT filename, size from scoreboard WHERE id NOT "
								"IN(SELECT id from cached where fe_id=%d) AND count >=%d "
								"AND fe_id=%d "
								"order by count DESC, last_modified DESC, size limit %d",
						fe_id, cfg_get_int("cache_req_count"), fe_id,
						cfg_get_int("workers"));
			}

			query_mysql(conn, query);
			result = mysql_store_result(conn);
			num_fields = mysql_num_fields(result);

			while ((row = mysql_fetch_row(result))) {
				if (row[0] && row[1]) {
					if (check_file_exists(row[0], atoi(row[1])) != 0) {
						continue;
					}
					mq_send(msgq_id, row[0], strlen(row[0]) + 1, msgprio);
				}
			}
			mysql_free_result(result);
		}
		sleep(5);
	}
	error_log(ERROR, "Exiting");
}

int check_file_exists(char* file, int size) {
	struct stat st;
	char fullpath[PATH_MAX];
	char tmppath[PATH_MAX];

	sprintf(fullpath, "%s/%s", cfg_get_str("cache_store"), file);
	sprintf(tmppath, "%s.tmp", fullpath);
	if (stat(fullpath, &st) == 0) {
		if (st.st_size < size) {
			error_log(WARN,
					"File already exists: %s, but size in db is: %d, and on disk: %d, redownload it.",
					fullpath, size, st.st_size);
			return 0;
		} else {
			error_log(WARN, "File already exists: %s", fullpath);
			return 1;
		}
	}
	if (stat(tmppath, &st) == 0) {
		error_log(WARN, "File download in progress, found tmp: %s", tmppath);
		return 1;
	}
	return 0;
}

void downloader_term() {
	running = 0;
}
