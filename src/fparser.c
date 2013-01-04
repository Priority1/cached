/*
 * fparser.c
 *
 *  Created on: Nov 5, 2010
 *      Author: pr1
 */

#include "cached.h"
#include "fparser.h"

static int running = 1;

MYSQL *conn;

void fparser_init() {
	int rc, fam_fd;
	struct timespec t1, t2;
	FAMConnection fc;
	FAMRequest *frp;
	FAMEvent fe;
	FILE * logfile;
	fpos_t *position = NULL;
	char buf[MAX_LEN], *line;
	fd_set readfds;
	t1.tv_sec = 0;
	t1.tv_nsec = 500000000;
	int firstrun = 1;

	char *access_log = cfg_get_str("access_log");

	/* Initialise MySQL connection */
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
	error_log(ERROR, "Connected to MySQL.");

	frp = malloc(sizeof *frp);
	if (!frp) {
		perror("malloc");
		mysql_close(conn);
		exit(EXIT_FAILURE);
	}

	/* Open fam connection */

	if ((FAMOpen(&fc)) < 0) {
		error_log(ERROR, "FAMOpen error (Gamin): %s", strerror(errno));
		mysql_close(conn);
		exit(EXIT_FAILURE);
	}

	rc = FAMMonitorFile(&fc, access_log, frp, NULL);
	if (rc < 0) {
		error_log(ERROR, "FAMMonitor error (Gamin): %s", strerror(errno));
		mysql_close(conn);
		exit(EXIT_FAILURE);
	}

	/* Initialize select data structure */
	fam_fd = FAMCONNECTION_GETFD(&fc);
	FD_ZERO(&readfds);
	FD_SET(fam_fd, &readfds);

	while (running) {
		rc = FAMPending(&fc);
		if (rc == 0) {
			nanosleep(&t1, &t2);
			continue;
		} else if (rc == -1) {
			error_log(ERROR, "FAMPending error (Gamin)");
		}

		if (FAMNextEvent(&fc, &fe) < 0) {
			mysql_close(conn);
			error_log(ERROR, "Unable to FAMNextEvent (Gamin)");
			exit(EXIT_FAILURE);
		}
		error_log(DEBUG, event_name(fe.code));
		switch (fe.code) {
		case FAMChanged:
			if (!(logfile = fopen(access_log, "r"))) {
				error_log(WARN, "warning: Open logfile failed");
				break;
			}
			fsetpos(logfile, &position);

			query_mysql(conn, "BEGIN");
			while (!feof(logfile)) {
				line = fgets(buf, MAX_LEN, logfile);
				if (line != NULL) {
					if (strlen(line) > 10) {
						stringparse(line);
					}
				}
			}
			query_mysql(conn, "COMMIT");

			fgetpos(logfile, &position);
			fclose(logfile);
			break;
		case FAMExists:
			logfile = fopen(access_log, "r");
			if (position == NULL && logfile != NULL) {
				error_log(DEBUG, "Wow, fresh meat...(Gamin)");
				fseek(logfile, 0, SEEK_END);
				fgetpos(logfile, &position);
			} else {
				error_log(ERROR, "warning: Open logfile failed");
			}
			fclose(logfile);
			break;
		case FAMDeleted:
			if (firstrun == 1) {
				error_log(ERROR, "access_log file not found: %s", access_log);
				exit(EXIT_FAILURE);
			}
			error_log(WARN, "Logrotate in progress (Gamin)");
			break;
		case FAMCreated:
			error_log(WARN, "Logrotate complete (Gamin)");
			/* Reset position for new file */
			logfile = fopen(access_log, "r");
			if (logfile != NULL) {
				fseek(logfile, 0, SEEK_END);
				fgetpos(logfile, &position);
			} else {
				error_log(ERROR, "Open logfile failed");
			}
			fclose(logfile);
			break;
		default:
			error_log(WARN, "Unknown event (Gamin): %s", event_name(fe.code));
			break;
		}
		firstrun = 0;
	}
	error_log(ERROR, "Exititng");
	mysql_close(conn);
}

void fparser_term() {
	running = 0;
}

int stringparse(const char *str) {
	regex_t f;
	regmatch_t p[REGEX_VARS];
	int i;
	int error;
	int retval = 0;
	static char erbuf[256];
	static char query[QUERY_MAX];
	char **results = malloc(10 * sizeof(char*));

	char *log_regex = cfg_get_str("log_regex");

	error = regcomp(&f, log_regex, 0);
	if (error) {
		regerror(error, &f, erbuf, 256);
		error_log(ERROR, "Error in log_regex pattern: %s , error: %s",
				log_regex, erbuf);
		retval = -1;
	}

	error = regexec(&f, str, REGEX_VARS, p, 0);
	if (error) {
		regerror(error, &f, erbuf, 256);
		error_log(DEBUG, "Regex not found for: %s, error: %s", str, erbuf);
		retval = -1;
	}

	if (retval != -1) {
		for (i = 0; p[i].rm_eo != -1; i++) {
			if (&p[i] != NULL || p[i].rm_so >= 0) {
				int len = p[i].rm_eo - p[i].rm_so;
				results[i] = (char *) malloc((len + 1) * sizeof(char));
				strncpy(results[i], str + p[i].rm_so, len);
				results[i][len] = 0;
			}
		}

		/* Make a query*/
		sprintf(query,
				"INSERT INTO scoreboard (id, filename, lastsave, size, last_modified, fe_id)"
						" VALUES "
						"(md5('%s'), '%s', FROM_UNIXTIME(%s), %s, STR_TO_DATE('%s','%s'), %d)"
						" ON DUPLICATE KEY UPDATE count=count+1, lastsave=FROM_UNIXTIME(%s)",
				results[5], results[5], results[1], results[2], results[3],
				cfg_get_str("lm_format"), cfg_get_int("fe_id"), results[1]);

		/* Send to mysql */
		query_mysql(conn, query);

		for (i = 0; p[i].rm_eo != -1; i++) {
			if (&p[i] != NULL || p[i].rm_so >= 0) {
				free(results[i]);
			}
		}
	}
	free(results);
	regfree(&f);
	return retval;
}

const char *event_name(int code) {
	static const char *famevent[] = { "", "FAMChanged", "FAMDeleted",
			"FAMStartExecuting", "FAMStopExecuting", "FAMCreated", "FAMMoved",
			"FAMAcknowledge", "FAMExists", "FAMEndExist" };
	static char unknown_event[10];

	if (code < FAMChanged || code > FAMEndExist) {
		sprintf(unknown_event, "unknown (%d)", code);
		return unknown_event;
	}
	return famevent[code];
}
