#ifndef REQ_INFO_H
#define REQ_INFO_H

#include <stdbool.h>
typedef struct {
  int req_content_length;
  char method[5];
  char *uri;
  char *relative_uri;
  char version[10];   /*obsolete*/
  time_t if_modified;
  bool is_cgi;
  /* GET /cgi-bin/abc/def/a.pl?username=123&email=456 HTTP/1.0 */
  char *req_left; /* left->  /home/user1/cgi-bin */
  char *req_right;/* right-> /abc/def/a.pl?username=123&email=456 */
} REQ_INFO; 

#include "sws_assist.h"
#include "response.h" 

int read_line(int sd, char* buffer, int size);
bool http_request_parse(int msg_sock, 
    opt_struct *opts,REQ_INFO *request_info, 
    struct head_struct *header,
	struct log_msg_struct *log_msg);

bool get_version(int msg_sock,char *token, struct head_struct *header);

void send_err(int msg_sock,int status, 
    char *description, struct head_struct *header);

char *trim(char *str);
bool explode(char *req_line, char *delim, char **token, int len);

bool get_method(int msg_sock, char *token, 
    char *method_buf,struct head_struct *header);
bool is_under(char *root_dir, char *path);

char *uri_resolve(int msg_sock, opt_struct *opts,
    char *uri, struct head_struct *header,REQ_INFO *request_info);

bool convert_strcut(REQ_INFO *request_info, 
    struct head_struct *header);
#endif
