#ifndef CGI_PROCESS_H_
#define CGI_PROCESS_H_

/*
 * cgi_process.h
 *
 */


#include "req_info.h"
enum { MAX_TOKEN =1024, MAX_URI_LENGTH = 2048};

typedef struct stru_cgi_request_ {
    char cmd_physical_path[MAX_URI_LENGTH];  /* e.g. /usr/lib/cgi-bin/a.cgi */
    char cmd_file[MAX_URI_LENGTH];           /* a.pl*/
    char querystring[MAX_URI_LENGTH];        /* e.g. user=abc&email=abc@aol.com */
} stru_cgi_request;

bool is_cgi_request(const char *uri,const opt_struct *opts);
int cgi_process(const REQ_INFO *request, const opt_struct *opts, struct head_struct *header, int fd);

#endif
