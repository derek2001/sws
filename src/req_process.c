/*
 * req_process.c
 *
 * By Hao Wu, Hui Zheng, Yulong Luo, Xin Sun
 * hwu9@stevens.edu
 * hzheng5@stevens.edu
 * yluo4@stevens.edu
 * xsun10@steven
 */

#ifndef __linux__
    #include <string.h>
#else
    #include <bsd/string.h>
#endif
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>

#include "req_process.h"
#include "response.h"
#include "req_info.h"
#include "cgi_process.h"
#include "sws_assist.h"

void req_process(int msg_sock,opt_struct *opts)
{
    time_t t;
    struct tm * timeinfo;
    struct log_msg_struct log_msg;
	
    REQ_INFO request;
    request.req_left = NULL;
    request.req_right = NULL;
    request.is_cgi = false;
    request.if_modified = -1;
    request.req_content_length = -1;

    struct head_struct header;
    header.content_length = -1;
    header.last_modify[0] = '\0';
	
    char client_ip[INET6_ADDRSTRLEN] = {0};
    getClientIp(msg_sock,client_ip);
    fprintf(stdout,"connected with [%s]\n", client_ip);
    strcpy(log_msg.remote_ip,client_ip);
	
    time(&t);
    timeinfo = gmtime(&t); // using GMT time
    remove_rl(log_msg.received_time,asctime(timeinfo));	

    bool b = http_request_parse(msg_sock, opts, &request, &header, &log_msg);
    log_msg.resp_status = header.status_code;
    if (request.is_cgi && b) {  /* cgi request */
        log_msg.resp_size = 0;
        cgi_process(&request, opts, &header ,msg_sock);
    } else {
        handle_response(&request, &header, msg_sock, &log_msg);
    }
	
    //write log
    if (opts->logfile != NULL) {
        if (opts->debug) {
            write_log(opts->logfile, &log_msg, 1);
        } else {
            write_log(opts->logfile, &log_msg, 0); 
        }
    }
	
    close(msg_sock);
    fprintf(stdout,"disconnected with [%s]\n",client_ip);
}

/*
 * write the record into the log file
 */
void write_log(const char * file, struct log_msg_struct *log_msg, bool debug)
{
	FILE * fp = NULL;
	char * log_str;
	
	int length = strlen(log_msg->remote_ip) + strlen(log_msg->first_buf) + strlen(log_msg->received_time) + 3 + 255;
	if((log_str = (char *)malloc(sizeof(char) * length)) == NULL) {
		xlog(LOG_ERR,1,"malloc");
	}
	   
	sprintf(log_str, "%s %s \"%s\" >%d %d",log_msg->remote_ip,log_msg->received_time,log_msg->first_buf,log_msg->resp_status,log_msg->resp_size);
	if (debug) { /*if in debug mode, display on stdout*/
		fprintf(stdout,"%s\n", log_str);
	}

	if ((fp = fopen(file,"a+")) == NULL) {
		xlog(LOG_ERR,1,"Open file");
	}
	   
	fprintf(fp,"%s\n",log_str);
	   
	if (fclose(fp) == -1) {
		xlog(LOG_ERR,1,"Close file");
	}
}
	
/*
 * Remove the '\r' and '\n' from the string 
 */
void remove_rl(char * target, char * ori_str) 
{
	int i,n = 0;
	for (i = 0; i<strlen(ori_str); i++) { 
		if (ori_str[i] != '\n' && ori_str[i] != '\r') {
			target[n] = ori_str[i];
			n++;
		} else {
			continue;
		}
	}
	target[n] = '\0';
}
	

