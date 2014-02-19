#if !defined(sun) && !defined(__sun)
#define _XOPEN_SOURCE 500
#define _BSD_SOURCE
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <magic.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <syslog.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "req_info.h"

#define MIN(a,b) (a<b)?(a):(b)
#define REQUEST_MAX_LEN 1024
#define TIMEOUT_ERR -1
#define REQ_TOO_LONG -2

/*send error response*/
void send_err(int msg_sock,int status, char *description, struct head_struct *header) {
  int line_len;
  char req_line[REQUEST_MAX_LEN];
  header->status_code = status;
  header->content_length = -1;
  strcpy(header->status_desp,description);
  strcpy(header->content_type,"text/html");

  if (status != 408 && status != 405 && status != 411 && strcmp(header->protocol, "HTTP/0.9") != 0) {
    while ((line_len = read_line(msg_sock, req_line, REQUEST_MAX_LEN)) > 0) {
      continue;
    }
    if (line_len == TIMEOUT_ERR) {
      send_err(msg_sock,408, "Request Timeout", header);
      xlog(LOG_INFO, false, "Response: 408 Request Timeout");
    }
  }
}

/*trim white space at the beginning and end of string*/
char *trim(char *str) {
  int len;
  char *pbg;
  char *pend;
  for (pbg=str;*pbg==' ';pbg++);
  len = strlen(pbg);
  for (pend=pbg + len - 1;*pend ==' ';pend--);        
  *(pend + 1) = '\0';
  return pbg;
}


/*
 *parse request line to tokens. If the number of tokens is not equal to 
 *the number specified, then return false 
 */
bool explode(char *req_line, char *delim, char **token, int len) {
  int i = 0;
  char *tok;
  while ((tok = strtok(req_line, delim))) {
    token[i] = trim(tok);
    req_line = NULL;
    i++;
    
    if (i > len) {
      return false;
    }
  }

  if (i < len) {
    return false;
  }

  return true;
}




/*                                         
 *read a line from the buffer, until reach the \r, \n 
 *or \r\n and return the number of read
 */
int read_line(int sd, char* buffer, int size)
{
  char c = '\0';
  int len = 0;
  int read_n;

  while((len < size) && c != '\n') {
    read_n = recv(sd, &c, 1, 0);
    if (read_n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
	return TIMEOUT_ERR;
      } else {
	xlog(LOG_ERR, true, "recv() error");
	exit(1);
      }
    }

    if(read_n > 0) {
      if(c == '\r') {
	read_n = recv(sd, &c, 1, MSG_PEEK);
	if (read_n < 0) {
	  if (errno == EAGAIN || errno == EWOULDBLOCK) {
	    return TIMEOUT_ERR;
	  } else {
	    xlog(LOG_ERR, true, "recv() error");
	    exit(1);
	  }
	}

	if((read_n > 0) && c == '\n') {
	  read_n = recv(sd, &c, 1, 0);
	  if (read_n < 0) {
	    if (errno == EAGAIN || errno == EWOULDBLOCK) {
	      return TIMEOUT_ERR;
	    } else {
	      xlog(LOG_ERR, true, "recv() error");
	      exit(1);
	    }
	  }
	} else {
	  c = '\n';
	}
      }
    } else {
      c = '\n';
    }
    if (c != '\n') {
      buffer[len++] = c;
    }
  }
  if (len == size) {
    return REQ_TOO_LONG;
  }
  buffer[len] = '\0';

  return len;
}


/*get request method*/
bool get_method(int msg_sock, char *token, char *method_buf,struct head_struct *header) {
  if (strcmp(token, "GET") == 0 || strcmp(token, "HEAD") == 0
      || strcmp(token, "POST") == 0) {
     if (strcmp(header->protocol,"HTTP/0.9")== 0 && strcmp(token, "HEAD")== 0) {
      strcpy(method_buf, "GET");
      } else {
      strcpy(method_buf,token);
      }
    return true;
  } else {
    xlog(LOG_INFO, false, "Response: 501 Not Implemented");
    send_err(msg_sock,501, "Not Implemented", header);
    return false;
  }
}



/**/
bool is_under(char *root_dir, char *path) {
  char *p;

  p = strstr(path, root_dir);
  if (p == path) {
    return true;
  } else {
    return false;
  }
}

/*decide the root dir*/
char *decide_root_dir(int msg_sock, opt_struct *opts, char *uri,
		      struct head_struct *header,bool *iscgi) {
  int len;
  char fix;
  char *temp;
  char *prefix; 
  char *root_dir;

  if (strstr(uri, "/cgi-bin") == uri && (opts->cgi_path)) {   
    if (!(prefix = realpath(opts->cgi_path, NULL))) {
      xlog(LOG_ERR, true, "cgi root path error");
      exit(1);
    }
    for (temp = uri + 1; *temp != '/' && *temp != '\0'; temp++);  
    char *t = strdup(temp);
    strcpy(uri, t);
    free(t);
    *iscgi = true;
    return prefix;    
  } 

  if (*uri == '~') {
    xlog(LOG_INFO, false, "Response: 400 Bad Request");
    send_err(msg_sock,400,"Bad Request", header);
    return NULL;
  }

  if (strstr(uri, "/~") == uri) {  
    for (temp = uri + 1; *temp != '/' && *temp != '\0'; temp++);
    fix = *temp;     
    *temp = '\0';
    len = strlen("/home") + strlen(uri) + strlen("/sws/") + 1;
    if ((root_dir = (char *)malloc(len)) == NULL) {
      xlog(LOG_ERR, true, "malloc error");
      exit(1);
    }
    *(uri + 1) = '/';
    sprintf(root_dir, "/home%s/sws/", uri + 1);
    if ((prefix = realpath(root_dir, NULL))) {
      *temp = fix;
      strcpy(uri, temp);
    } else {
      if (errno == EACCES) {
	xlog(LOG_INFO, false, "Response: 403 Forbidden");
        send_err(msg_sock,403, "Forbidden", header);
      } else {
	xlog(LOG_INFO, false, "Response: 404 Not Found");
	send_err(msg_sock,404, "Not Found",header);
      }
    }
    free (root_dir);
    return prefix;
  } 

  if (!(prefix = realpath(opts->root, NULL))) {
    xlog(LOG_ERR, true, "root path error");
    exit(1);
  }
  return prefix;
}



/*resolve uri*/
char *uri_resolve(int msg_sock, opt_struct *opts, char *uri, 
		  struct head_struct *header,REQ_INFO *request_info) {
  int len;
  char fix;
  char *temp;
  char *token;
  char *tok_cp;
  char *prefix;
  char *canonpath;

  /*decide root dir*/
  if ((prefix = decide_root_dir(msg_sock, opts, uri, header,&request_info->is_cgi)) == NULL) {
    return NULL;
  }

  request_info->req_left =  strdup(prefix);
  request_info->req_right = strdup(uri);

  /*check if there are queries in uri*/
  temp = strchr(uri, '?');
  if (temp != NULL) {
    *temp = '\0';
  }

  len = strlen(prefix) + strlen(uri) + 2;
  if ((canonpath = (char *)malloc(len)) == NULL) {
    xlog(LOG_ERR, true, "malloc error");
    exit(1);
  }
  strcpy(canonpath, prefix);

  /*we can't remove the last '/' of uri if it has*/
  len = strlen(uri);
  if (uri[len - 1] == '/') {
    fix = '/';
  } else {
    fix = '\0';
  }

  token = strtok(uri, "/");
  while (token) {
    sprintf(canonpath,"%s/%s",canonpath,token);
    tok_cp = strdup(token);
    token = strtok(NULL, "/");
    len = strlen(canonpath);
    if (token) {
      canonpath[len] = '/';
    } else {
      canonpath[len] = fix;
    }
    canonpath[len + 1] = '\0';

    if ((temp = realpath(canonpath, NULL))) {
      if (is_under(prefix, temp)) {
	strcpy(canonpath, temp);
      } else {
	if (strcmp(tok_cp, "..") == 0) {
	  strcpy(canonpath, prefix);
	} else {
	  xlog(LOG_INFO, false, "Response: 404 Not Found");
	  send_err(msg_sock,404, "Not Found", header);
	  return NULL;
	}
      }
      free(temp);
    } else {
      if (errno == EACCES) {
	xlog(LOG_INFO, false, "Response: 403 Forbidden");
        send_err(msg_sock,403, "Forbidden", header);
      } else {
	xlog(LOG_INFO, false, "Response: 404 Not Found");
        send_err(msg_sock,404, "Not Found",header);
      }
      return NULL;
    }
    free(tok_cp);
  }

  return canonpath;
}


/*check if is number*/
bool is_number(char *str) {
  int i;
  for (i = 0; i < strlen(str); i++) {
    if (str[i] < '0' || str[i] > '9') {
      return false;
    }
  }
  return true;
}


/*check if version is HTTP/number.number*/
bool is_http_num_dot_num(char *token) {
  char *p;
  char *major;
  char *minor;

  p = token + 4;
  if (*p != '/') {
    return false;
  }
  *p = '\0';

  if (strcmp(token, "HTTP") != 0) {
    return false;
  }

  major = p + 1;
  if (!(p = strchr(major, '.'))) {
    return false;
  }
  *p = '\0';
  minor = p + 1;

  if (!is_number(major)) {
    return false;
  }

  if (!is_number(minor)) {
    return false;
  }
  return true;
}


/*get http version*/
bool get_version(int msg_sock,char *token, struct head_struct *header) {
  if (strcmp(token, "HTTP/0.9") == 0
      || strcmp(token, "HTTP/1.0") == 0) {
    strcpy(header->protocol, "HTTP/1.0");
    return true;
  } else if (is_http_num_dot_num(token)) {
    xlog(LOG_INFO, false, "Response: 505 Version Not Supported");
    send_err(msg_sock,505, "Version Not Supported", header);
    return false;
  } else {
    xlog(LOG_INFO, false, "Response: 400 Bad Request");
    send_err(msg_sock,400, "Bad Request", header);
    return false;
  }
}

/*parse if-modified-since */
time_t parse_since(char *if_since_str) {
  struct tm tm;

  memset(&tm, 0, sizeof(struct tm));
  if (strptime(if_since_str, "%a, %d %b %Y %H:%M:%S GMT", &tm) != NULL) {
    return timegm(&tm); 
  }

  memset(&tm, 0, sizeof(struct tm));
  if (strptime(if_since_str, "%A, %d-%b-%y %H:%M:%S GMT", &tm) != NULL) {
    return timegm(&tm);
  }

  memset(&tm, 0, sizeof(struct tm));
  if (strptime(if_since_str, "%a %b %e %H:%M:%S %Y", &tm) != NULL) {
    return timegm(&tm);
  }

  return -1;
}


/*check the permission of file or dir*/
bool is_permit(char *path, bool is_dir) {
  if (is_dir) {
    DIR *dir;
    if (!(dir = opendir(path))) {
      if (errno == EACCES) {
        return false;
      }
    }
    closedir(dir);
  } else {
    int fd;
    if ((fd = open(path, O_RDONLY)) == -1) {
      if (errno == EACCES) {
        return false;
      }
    }
    close(fd);
  }
  return true;
}


/*get the mime by using magic*/
char *mime_type(char *path, struct stat buf) {
  const char *mime;
  magic_t magic;
 
  if (S_ISDIR(buf.st_mode)) {
    return strdup("text/html");
  } else {
    if (!(magic = magic_open(MAGIC_MIME_TYPE))) {
      xlog(LOG_ERR, true, "magic_open() error");
      exit(1);
    }
    if (magic_load(magic, NULL) == -1) {
      xlog(LOG_ERR, true, "magic_load() error");
      exit(1);
    }
    if (magic_compile(magic, NULL) == -1) {
      xlog(LOG_ERR, true, "magic_compile() error");
      exit(1);
    }
    if (!(mime = magic_file(magic, path))) {
      xlog(LOG_ERR, true, "magic_file() error");
      exit(1);
    }
    return strdup(mime);
    magic_close(magic);
  }
}


/*convert struct info to struct head_struct*/
bool convert_strcut(REQ_INFO *request_info, struct head_struct *header) {
  int len;
  bool permit;
  char *mime;
  char *index;
  time_t mtime;
  time_t last_time;
  time_t current_time;
  struct stat buf;
  struct stat index_buf;

  if (time(&current_time) == -1) {
    xlog(LOG_ERR, true, "time() error");
    exit(1);
  }
  if (stat(request_info->uri, &buf) == -1) {
    xlog(LOG_ERR, true, "stat() %s error", request_info->uri);
    exit(1);
  }
  if (S_ISDIR(buf.st_mode)) {
    len = strlen(request_info->uri) + strlen("/index.html") + 1;
    if ((index = (char *)malloc(len)) == NULL) {
      xlog(LOG_ERR, true, "malloc() error");
      exit(1);
    }
    sprintf(index, "%s/index.html", request_info->uri);
    /*index.html exists*/
    if (stat(index, &index_buf) != -1) {
      permit = is_permit(index, false);
      header->content_length = (size_t)index_buf.st_size;
      mtime = index_buf.st_mtime;
    } else {  /*not exist*/
      permit = is_permit(request_info->uri, true);
      mtime = buf.st_mtime;
    }
  } else {
    permit = is_permit(request_info->uri, false);
    header->content_length = (size_t)buf.st_size;
    mtime = buf.st_mtime;
  }

  if (!permit) {
    header->status_code = 403;
    strcpy(header->status_desp,"Forbidden");
    strcpy(header->content_type,"text/html");
    header->content_length = -1;
    xlog(LOG_INFO, false, "Response: 403 Forbidden");
    return false;
  }

  if (request_info->if_modified != -1 && 
      (request_info->if_modified > mtime && 
       request_info->if_modified < current_time)) {
    header->status_code = 304;
    strcpy(header->status_desp,"Not Modified");
    strcpy(header->content_type,"text/html");
    header->content_length = -1;
    xlog(LOG_INFO, false, "Response: 304 Not Modified");
    return false;
  } else {
    header->status_code = 200;
    strcpy(header->status_desp,"Ok");
    mime = mime_type(request_info->uri, buf);
    strcpy(header->content_type, mime);
    free(mime);
    last_time = MIN(mtime, current_time);
    gmt_time(last_time, header->last_modify);
    xlog(LOG_INFO, false, "Response: 200 Ok");
    return true;
  }
}




/*parse requst*/
bool http_request_parse(int msg_sock, opt_struct *opts,REQ_INFO *request_info, 
			struct head_struct *header, struct log_msg_struct *log_msg) {
  int line_len;
  char *temp_p;
  char *token[3];
  char req_line[REQUEST_MAX_LEN];
  char req_line_cp[REQUEST_MAX_LEN];

  /*set default http version to HTTP/1.0*/
  strcpy(header->protocol, "HTTP/1.0");

  /*parse first line*/
  while ((line_len = read_line(msg_sock, req_line, REQUEST_MAX_LEN)) == 0) {
    continue;
  }

  if (line_len == TIMEOUT_ERR) {
    xlog(LOG_INFO, false, "Response: 408 Request Timeout");
    send_err(msg_sock,408, "Request Timeout", header);
    return false;
  } else if (line_len == REQ_TOO_LONG) {
    xlog(LOG_INFO, false, "Response: 400 Bad Request");
    send_err(msg_sock,400, "Bad Request", header);
    return false;
  }
  strcpy(log_msg->first_buf,req_line);
  strcpy(req_line_cp,req_line);
  if (explode(req_line, " ", token, 2)) { /*simple request*/
    /*version*/
    strcpy(header->protocol, "HTTP/0.9");

    /*get method*/
    if (!get_method(msg_sock,token[0],request_info->method,header)) {
      return false;
    }

    /*get uri*/
    request_info->relative_uri = strdup(token[1]);
    if ((request_info->uri = uri_resolve(msg_sock,opts,token[1],header,request_info)) == NULL) {
      return false;
    }

    /*check uri permission*/
    int len;
    char *index;
    bool permit;
    struct stat buf;
    struct stat index_buf;
    if (stat(request_info->uri, &buf) == -1) {
      xlog(LOG_ERR, true, "stat() %s error", request_info->uri);
      exit(1);
    }
    if (S_ISDIR(buf.st_mode)) {
      len = strlen(request_info->uri) + strlen("/index.html") + 1;
      if ((index = (char *)malloc(len)) == NULL) {
	xlog(LOG_ERR, true, "malloc() error");
	exit(1);
      }
      sprintf(index, "%s/index.html", request_info->uri);
      /*index.html exists*/
      if (stat(index, &index_buf) != -1) {
	permit = is_permit(index, false);
      } else {  /*not exist*/
	permit = is_permit(request_info->uri, true);
      }
    } else {
      permit = is_permit(request_info->uri, false);
    }

    if (!permit) {
      header->status_code = 403;
      strcpy(header->status_desp,"Forbidden");
      header->content_length = -1;
      xlog(LOG_INFO, false, "Response: 403 Forbidden");
      return false;
    }
    
    header->status_code = 200;
    strcpy(header->status_desp,"Ok");
    xlog(LOG_INFO, false, "Response: 200 Ok");
    return true;
  } else if (explode(req_line_cp, " ", token, 3)) { /*full request*/
    /*get method*/
    if (!get_method(msg_sock,token[0],request_info->method,header)) {
      return false;
    }

    /*get uri*/
    request_info->relative_uri = strdup(token[1]);
    if ((request_info->uri = uri_resolve(msg_sock,opts,token[1],header,request_info)) == NULL) {
      return false;
    }
      
    /*http version.  We support version 0.9 and version 1.0*/
    if (!get_version(msg_sock,token[2], header)) {
      return false;
    }
  } else { 
    xlog(LOG_INFO, false, "Response: 400 Bad Request");
    send_err(msg_sock,400,"Bad Request", header);
    return false;
  }

  /*parse If-Modified-Since and Content-Length if POST is specified*/
  int if_modified_times = 0, content_length_times =0;/*indicate appears times*/
  line_len = read_line(msg_sock, req_line, REQUEST_MAX_LEN);
  while (line_len > 0) {
    if (!(temp_p = strchr(req_line, ':'))) {
      line_len = read_line(msg_sock, req_line, REQUEST_MAX_LEN);
      continue;
    }

    *temp_p = '\0';
    token[0] = trim(req_line);
    token[1] = trim(temp_p + 1);

    if (strcmp(token[0], "If-Modified-Since") == 0) {
      if_modified_times++;
      /*If-Modified-Since should not appear more than one time*/
      if (if_modified_times < 2) {
	request_info->if_modified = parse_since(token[1]);
      } else {
	request_info->if_modified = -1;
      }
    }

    if (strcmp(token[0], "Content-Length") == 0) {
      content_length_times++;
      /*Content-Length should not appear more than one time*/
      if (content_length_times > 1) {
	xlog(LOG_INFO, false, "Response: 400 Bad Request");
	send_err(msg_sock,400, "Bad Request", header);
	return false;
      }
      if (strcmp(request_info->method, "POST") == 0) {
	if (!is_number(token[1])) {
	  xlog(LOG_INFO, false, "Response: 400 Bad Request");
	  send_err(msg_sock,400, "Bad Request", header);
	  return false;
	}
	request_info->req_content_length = atoi(token[1]);
      } else { /*GET and HEAD should not give content-length*/
	xlog(LOG_INFO, false, "Response: 400 Bad Request");
	send_err(msg_sock,400, "Bad Request", header);
	return false;
      }
    }
    line_len = read_line(msg_sock, req_line, REQUEST_MAX_LEN);
  }
  if (line_len == TIMEOUT_ERR) {
    xlog(LOG_INFO, false, "Response: 408 Request Timeout");
    send_err(msg_sock,408, "Request Timeout", header);
    return false;
  } else if (line_len == REQ_TOO_LONG) {
    xlog(LOG_INFO, false, "Response: 400 Bad Request");
    send_err(msg_sock,400, "Bad Request", header);
    return false;
  }

  /*when POST method, check if content-length is given and check if the uri 
   allow POST*/
  if (strcmp(request_info->method, "POST") == 0) {
    if (request_info->req_content_length == -1) {
      xlog(LOG_INFO, false, "Response: 411 Length Required");
      send_err(msg_sock,411, "Length Required", header);
      return false;
    }

    if (!request_info->is_cgi) {
      xlog(LOG_INFO, false, "Response: 405 Method Not Allowed");
      send_err(msg_sock,405, "Method Not Allowed", header);
      return false;
    }
  }

  if (!convert_strcut(request_info, header)) {
    return false;
  }
  return true;
}



