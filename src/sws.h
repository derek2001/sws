#ifndef SWS__H_
#define SWS__H_

/*
 * sws.h
 * sws main implementation
 *
 * By Hao Wu, Hui Zheng, Yulong Luo, Xin Sun
 * hwu9@stevens.edu
 * hzheng5@stevens.edu
 * yluo4@stevens.edu
 * xsun10@steven
 */

#include <sys/stat.h>

#include "sws_assist.h"

enum {
    MAX_PARALLEL_REQUEST = 50
};

void parse_option(int argc, char **argv,opt_struct *opts);
bool do_sws (opt_struct *opts);
int create_socket(opt_struct *opts);
bool already_running(void);
#endif 
