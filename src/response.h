/*
 *  response.h
 *
 *
 * By Hao Wu, Hui Zheng, Yulong Luo, Xin Sun
 * hwu9@stevens.edu
 * hzheng5@stevens.edu
 * yluo4@stevens.edu
 * xsun10@steven
 */

/*
 * struct hfield store the head
 * head field information for response.
 */
#ifndef RESPONSE_H
#define RESPONSE_H

struct head_struct{
    char protocol[10];
    int status_code;
    char status_desp[40];
    char content_type[50];
    size_t content_length;
    char last_modify[50];
};
 
/*
 *buffer for response
 */
struct res_buf{
    char* buffer;
    int len;
    int size;
};

struct log_msg_struct {
	char remote_ip[INET6_ADDRSTRLEN];
	char first_buf[1024];
	char received_time[25]; // the format of the time is "www mmm dd hh:mm:ss yyyy".
	int	 resp_status;
	int  resp_size;
};

#include "req_info.h"
#include <stdbool.h>

void resp_header(struct head_struct* head, struct res_buf* rbuffer, int sd);
void gmt_time(time_t t, char* str);
size_t push_index(REQ_INFO* request, struct res_buf* ibuffer,
    int sd,struct head_struct* head);
size_t send_file(char* file, int conn);
bool push_buffer(struct res_buf* buf, char* msg, int len);
void send_response(int conn, struct res_buf* rbuffer);
void handle_response(REQ_INFO* request, 
    struct head_struct* header, int sd,
	struct log_msg_struct *log_msg);
size_t resp_error_entity(struct head_struct* head, 
    struct res_buf* ebuffer, int sd);
void send_internal_error(int sd, struct head_struct* head);
void send_msg(int sd,  const void *msg, size_t len, int flags);
#endif
