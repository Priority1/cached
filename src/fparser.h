/*
 * fparser.h
 *
 *  Created on: Nov 5, 2010
 *      Author: pr1
 */

#include <fam.h>
#include <sys/select.h>
#include <sys/time.h>



#ifndef FPARSER_H_
#define FPARSER_H_
#define MAX_LEN  1024
#define REGEX_VARS 20

int stringparse(const char *);
const char *event_name(int);



#endif /* FPARSER_H_ */
