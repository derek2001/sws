/*
 *  response.c
 *
 *
 * By Hao Wu, Hui Zheng, Yulong Luo, Xin Sun
 * hwu9@stevens.edu
 * hzheng5@stevens.edu
 * yluo4@stevens.edu
 * xsun10@steven
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#if defined(sun) ||defined(__sun)
    #include <ast/fts.h>
#else
    #include <fts.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "response.h"
#include "sws_assist.h"

#define INT_ERROR_ENTITY \
    "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n" \
    "<html><head><title>500 Internal Server Error</title></head><body>\n" \
    "<h1>500 Internal Server Error</h1><p></p>\n" \
    "<hr><address>sws/1.0 (Linux) Server</address>\n" \
    "</body></html>\n" \
    "\n"

#define SERVER_NAME "sws/1.0"
#define buffer_size 1024

char* week[7] = {"Sun", "Mon", "Tue", 
	"Wed", "Thu", "Fri", "Sat"};
char* month[12] = {"Jan", "Feb", "Mar", 
	"Apr", "May", "Jun", "Jul", 
	"Aug", "Sep", "Oct", "Nov", 
	"Dev"};
char* stype[5] = {"", "k", "m", "g", "t"};

/*
 *handle the respose according to the different request
 */
void handle_response(REQ_INFO* request, struct head_struct* header, int sd, struct log_msg_struct *log_msg)
{
    struct stat st;
    /*rbuffer store the response head field*/
    struct res_buf rbuffer;
    rbuffer.len = 0;
    rbuffer.size = 0;
    /*store the indexing of the directory if needed*/
    struct res_buf ibuffer;
    ibuffer.len = 0;
    ibuffer.size = 0;
    size_t hsize;
    /*store error entity for the browser*/
    struct res_buf ebuffer;
    ebuffer.len = 0;
    ebuffer.size = 0;
    size_t esize;
    
    stat(request->uri, &st);
    if(header->status_code != 200)
    {
        esize = resp_error_entity(header, &ebuffer, sd);
		log_msg->resp_size = esize;
        /*if version is http/0.9 just send entity*/
        if(strcmp(header->protocol, "HTTP/0.9") != 0)
        { 
            header->content_length = esize;
            resp_header(header, &rbuffer, sd);
            send_response(sd, &rbuffer);
        }
        send_response(sd, &ebuffer);
    }
    else if(S_ISDIR(st.st_mode))
    {
        hsize = push_index(request, &ibuffer, sd, header);
		log_msg->resp_size = hsize;
        /*if version is http/0.9 just send entity*/
        if(strcmp(header->protocol, "HTTP/0.9") != 0)
        { 
            header->content_length = hsize;
            resp_header(header, &rbuffer, sd);
            send_response(sd, &rbuffer);
            if(strcmp(request->method, "HEAD") == 0)
                return;
        }
        send_response(sd, &ibuffer);
    }
    else
    {
        /*if version is http/0.9 just send entity*/
        if(strcmp(header->protocol, "HTTP/0.9") != 0)
        { 
            resp_header(header, &rbuffer, sd);
            send_response(sd, &rbuffer);
            if(strcmp(request->method, "HEAD") == 0)
                return;
        }
        log_msg->resp_size = send_file(request->uri, sd);
    }
}
/*
 *generate the http standard time format
 */
void gmt_time(time_t t, char* str)
{
    struct tm *gmt;
    if((gmt = gmtime(&t)) != NULL)
        sprintf(str, "%s, %d %s %d %d:%d:%d GMT",week[gmt->tm_wday],
	    gmt->tm_mday,
	    month[gmt->tm_mon],
	    gmt->tm_year-100+2000,
	    gmt->tm_hour,
	    gmt->tm_min,
	    gmt->tm_sec);
}

/*
 *construct the response head field
 */
void resp_header(struct head_struct* head, struct res_buf* rbuffer, int sd)
{
    char str_cp[50];
    char msg[512];
    time_t t = time(NULL);
    
    sprintf(msg, "%s %d %s\015\012", head->protocol, 
        head->status_code, head->status_desp);
    if(!push_buffer(rbuffer, msg, strlen(msg)))
        send_internal_error(sd, head);

    sprintf(msg, "Content-Type: %s; charset=UTF-8\015\012", head->content_type);
    if(!push_buffer(rbuffer, msg, strlen(msg)))
        send_internal_error(sd, head);

    gmt_time(t, str_cp);
    sprintf(msg, "Date: %s\015\012", str_cp);
    if(!push_buffer(rbuffer, msg, strlen(msg)))
        send_internal_error(sd, head);

    sprintf(msg, "Server: %s\015\012", SERVER_NAME);
    if(!push_buffer(rbuffer, msg, strlen(msg)))
        send_internal_error(sd, head);

    if(head->last_modify[0] != '\0')
    {
        sprintf(msg, "Last-Modified: %s\015\012", head->last_modify);
        if(!push_buffer(rbuffer, msg, strlen(msg)))
            send_internal_error(sd, head);
    }

    if(head->content_length != -1)
    {
        sprintf(msg, "Content-Length: %zu\015\012\015\012", head->content_length);
        if(!push_buffer(rbuffer, msg, strlen(msg)))
            send_internal_error(sd, head);
    }
}

/*
 *send msg line by line just in case out of memory
 *(ie. malloc fail) we could not push any
 *information to the buffer
 */ 
void send_internal_error(int sd, struct head_struct* head)
{
    char msg[128];
    char str[50];
    time_t t = time(NULL);

    if((head != NULL) && (strcmp(head->protocol, "HTTP/1.0") == 0))
    {
        sprintf(msg, "HTTP/1.0 500 Internal Server Error\015\012");
        send_msg(sd, msg, strlen(msg), 0);
	
        gmt_time(t, str);
        sprintf(msg, "Date: %s\015\012", str);
        send_msg(sd, msg, strlen(msg), 0);

        sprintf(msg, "Server: %s\015\012", SERVER_NAME);
        send_msg(sd, msg, strlen(msg), 0);

        sprintf(msg, "Content-Length: %zu\015\012", strlen(INT_ERROR_ENTITY));
        send_msg(sd, msg, strlen(msg), 0);

        sprintf(msg, "Content-Type: text/html; charset=UTF-8\015\012\015\012");
        send_msg(sd, msg, strlen(msg), 0);
    }
    sprintf(msg, "%s", INT_ERROR_ENTITY);
    send_msg(sd, msg, strlen(msg), 0);
    exit(EXIT_FAILURE);
}

/*
 *send message to the client
 */
void send_msg(int sd,  const void *msg, size_t len, int flags)
{
    if(send(sd, msg, len, flags) < 0)
        xlog(LOG_ERR, true, "send() error");  
}
/*
 *construct the response head field
 */
size_t resp_error_entity(struct head_struct* head, struct res_buf* ebuffer, int sd)
{
    char buf[buffer_size];
    size_t size=0;

    sprintf(buf, "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\
        <html><head>");
    if(!push_buffer(ebuffer, buf, strlen(buf)))
        send_internal_error(sd, head);
    size += strlen(buf);

    sprintf(buf, "<title>%d %s</title>\
        </head><body><h1>%d %s</h1><p>", head->status_code,
        head->status_desp, head->status_code, head->status_desp);
    if(!push_buffer(ebuffer, buf, strlen(buf)))
        send_internal_error(sd, head);
    size += strlen(buf);

    sprintf(buf, "</p><hr><address>%s (Linux) Server</address>\
        </body></html>", SERVER_NAME);
    if(!push_buffer(ebuffer, buf, strlen(buf)))
        send_internal_error(sd, head);
    size += strlen(buf);

    return size;
}

/*
 *push the response head field to the buffer
 */
bool push_buffer(struct res_buf* buf, char* msg, int len)
{
    if(buf->size == 0)
    {
        if((buf->buffer = malloc(sizeof(char)*buffer_size)) == NULL)
        {
            /*response internel error*/
            xlog(LOG_ERR, true, "malloc() error");
            return false;
        }
        buf->size += buffer_size;
    }
    else if((buf->len + len) >= buf->size)
    {
         if(realloc(buf->buffer, buf->size + buffer_size) == NULL)
         {
             /*response internel error*/
             xlog(LOG_ERR, true, "realloc() error");
             return false;
         }
         buf->size += buffer_size;
    }

    memcpy(&(buf->buffer[buf->len]), msg, len);
    buf->len += len;
    buf->buffer[buf->len] = '\0';
    return true;
}

/*string compare function*/
int str_cmp(const FTSENT** fsent1, const FTSENT** fsent2)
{
    return (strcmp((*fsent1)->fts_name,(*fsent2)->fts_name));
}

/*
 *read the directory
 */
FTSENT* travers_dir(char* dir)
{
    FTS* fts;
    char *dirn[]={dir, NULL};
    FTSENT* fsent = NULL;
    FTSENT* child = NULL;

    fts = fts_open(dirn, 
        FTS_PHYSICAL | FTS_NOCHDIR | FTS_SEEDOT, 
	str_cmp);

    while( (fsent = fts_read(fts)) != NULL)
    {	
	if(fsent->fts_info == FTS_D)			
	{	
       	    child = fts_children(fts,0);

            if(fsent->fts_name[0] == '.')
            {   
                fsent = fsent->fts_link;
                continue;
            }

            if (errno != 0)
            {
               xlog(LOG_ERR, true, "fts %s: %s",
		    fsent->fts_parent->fts_accpath,
		    strerror(errno));
		return NULL;
       	    }
            return child;
            fts_set(fts, fsent, FTS_SKIP);
        }
    }
    return NULL;
}

void file_size(off_t ssize, char* str)
{
    int i = 0;
    float size = ssize*1.0;
    while(size>=1024)
    {
        ++i;
        size = size/1024;
    }
   
    if(i > 0)
        sprintf(str, "%.1f%s", size, stype[i]);
    else
        sprintf(str, "%g%s", size, stype[i]);
}

/*f the request was for a directory and the directory 
 *does not contain a file named "index.html", then sws
 *will generate a directory index
 */
size_t push_index(REQ_INFO* request, struct res_buf* ibuffer, int sd, struct head_struct* head)
{
    char buf[buffer_size];
    struct tm lt;
    FTSENT* fsent = travers_dir(request->uri);
    char str[20];
    size_t size=0;

    if(fsent == NULL)
        send_internal_error(sd, head);
    sprintf(buf, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\012\
        <html>\012<head>\012\
              <title>Index of %s</title>\012\
        </head>\012<body>\012\
        <h1>Index of %s</h1>", request->relative_uri, request->relative_uri);
    if(!push_buffer(ibuffer, buf, strlen(buf)))
        send_internal_error(sd, head);
    size += strlen(buf);

    sprintf(buf, "<table><tr><th><a href=\"?C=N;O=D\">Name</a></th><th>\
        <a href=\"?C=M;O=A\">Last modified</a></th>\
        <th><a href=\"?C=S;O=A\">Size</a></th>\
        </tr><tr><th colspan=\"5\"><hr></th></tr>\012");    
    if(!push_buffer(ibuffer, buf, strlen(buf)))
        send_internal_error(sd, head);
    size += strlen(buf);
    
    while(fsent != NULL)
    {
        if((fsent->fts_name)[0] == '.')
	{
	    fsent = fsent->fts_link;
            continue;
	}
        
        time_t t = fsent->fts_statp->st_mtime;
	localtime_r(&t, &lt);
	char timbuf[80];
	strftime(timbuf, sizeof(timbuf), "%d-%b-%y %H:%M", &lt);
        file_size(fsent->fts_statp->st_size, str);
        sprintf(buf, "<tr><td>%s</td><td align=\"right\">%s</td>\
            <td align=\"right\">%s</td><td>&nbsp;</td></tr>",
            fsent->fts_name, timbuf, str);
        if(!push_buffer(ibuffer, buf, strlen(buf)))
            send_internal_error(sd, head);
        size += strlen(buf);
        fsent = fsent->fts_link;
    }

    sprintf(buf, "<tr><th colspan=\"5\"><hr></th></tr>\
        </table>\
        <address>sws/1.0 (Linux) Server</address>\
        </body></html>");
    if(!push_buffer(ibuffer, buf, strlen(buf)))
        send_internal_error(sd, head);
    size += strlen(buf);
    return size;
}

/*
 *sending the file by requested line by line just in case
 *the file is really large, and it's not possible to hold
 *the file in the memory
 */
size_t send_file(char* file, int conn)
{
    int fd;
    char buf[buffer_size];
    int count;
	size_t size = 0;

    if((fd = open(file, O_RDONLY)) == -1)
    {
        xlog(LOG_ERR, true, "open() error");
        send_internal_error(conn, NULL); 
    }

    while((count = read(fd, buf, buffer_size-1)) > 0)
    {
        send_msg(conn, buf, count, 0);
	size += strlen(buf);
    }
    close(fd);
	
    return size;
}

void send_response(int conn, struct res_buf* rbuffer)
{
    send_msg(conn, rbuffer->buffer, rbuffer->len, 0);
    free(rbuffer->buffer);
}
