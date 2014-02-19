/*
 * main.c
 * sws -- simple web server
 *
 * By Hao Wu, Hui Zheng, Yulong Luo, Xin Sun
 * hwu9@stevens.edu
 * hzheng5@stevens.edu
 * yluo4@stevens.edu
 * xsun10@steven
 */

#ifndef __linux__
    #include <stdlib.h>
#else
    #include <bsd/stdlib.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sws.h"


int
main(int argc, char **argv) {

    setprogname(argv[0]);
    if (already_running()) {
        fprintf(stderr,"another instance is already running\n");
        return EXIT_FAILURE;
    }

    opt_struct opt;
    opt_init(&opt);

    parse_option(argc,argv,&opt);


    do_sws(&opt);
    return EXIT_SUCCESS;
}

