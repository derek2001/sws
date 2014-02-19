/*
 * cgi_process.c
 */

#ifndef __linux__
    #include <string.h>
#else
    #include <bsd/string.h>
#endif
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(sun) ||defined(__sun)
#include <strings.h>
#endif
#include <syslog.h>
#include <unistd.h>
#include <sys/wait.h>

#include "cgi_process.h"
#include "file_operation.h"
#include "sws_assist.h"
#define MAX_CGI_TIMEOUT  60 //120sec
#define MAX_PORT_LENGTH 10
#define MAX_CONTENT_LENGTH 20
#define MAX_URI_LENGTH 2048
#define BUFFER_SIZE 1024
#define SERVER_NAME "sws/1.0"

#define MIN(a,b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a < _b ? _a : _b; })

static const char *DELIMIT = "/";
static const char C_DELIMIT = '/';
static const char *DOUBLE_DOT = "..";
static const char *SINGLE_DOT = ".";
static const char *QUESTION_MARK = "?";
static const char C_QUESTION_MARK = '?';
static const char *AND_MARK = "&";
static const char *DEFAULT_CGI_FILE = "default.cgi";
static const char *HEADER_CONNECTION_CLOSE = "Connection: close\r\n\r\n";
static int bytes_from_cgi = 0;

enum {
    READ_SIDE = 0,
    WRITE_SIDE
};

enum {  
        BAD_400 = 0, 
        BAD_403,
        BAD_404, 
        BAD_408, 
        BAD_500,
        BAD_CNT
        };

const char * BAD_REQUEST[BAD_CNT][2] = {
    {"400 Bad Request", "Requested file is not a regular file."},
    {"403 Forbidden.", "Permission denied."},
    {"404 Not found.", "Requested file could not be found."},
    {"408 Request Timeout.", "Requested timeout."},
    {"500 Internal Server Error", "Failed to execute CGI script."}
};


static bool
xsend(int msg_sock, const char *fmt, ... )
{
    bool re = true;
    va_list va;
    char buf[BUFFER_SIZE] = {0};

    va_start(va, fmt);
    vsprintf(buf, fmt, va);
    va_end(va);


    if(send(msg_sock,buf,strlen(buf),0) < 0) {
        xlog(LOG_ERR, 1,"send");
        re = false;
    }
    return re;        
}

/*
 * Generic text-only response with a particular status.
 * Used for bad requests mostly.
 */
static void 
generic_response(int msg_sock,const char * status,const char * message, const struct head_struct *pheader) {
    char bad_request[BUFFER_SIZE] = {0};
    sprintf(bad_request, 
            "%s %s\r\n"
            "Server: " SERVER_NAME "\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %zu\r\n"
            "\r\n"
            "%s\r\n", pheader->protocol, status, strlen(message), message);
    xsend(msg_sock,bad_request);   
}


/*
 * intput: req_right
 * e.g. /../../../123/../123/./456/../../123/456/a.pl?username=123&email=456@aol.com
 *
 * result:
 * e.g. /123/456/a.pl
 */
static void
get_real_path(char *req_right ,stru_cgi_request *cgi_req)
{

    char *tokens[MAX_TOKEN] = {0};
    int idx_tokens = 0; /* 0 means reach to root */
    bool end_with_delimit = false;

    /*seperate command with query string*/
    /*
    * query string always begins with '?' , 
    * for example:
    * GET /abc/cde/?name=123  HTTP/1.0
    */
    char *pch = strstr (req_right,QUESTION_MARK);
    if (pch && pch+1<req_right+strlen(req_right)) { /*has query string*/
        strcpy(cgi_req->querystring,pch+1);
        *pch = '\0';                                /*now req_right only has command*/
    }

    /*
     * req_right end with '/' then set default cgi
     * for example:
     *      GET /abc/def/ HTTP/1.0
     */
    if (req_right && strlen(req_right)>0 && req_right[strlen(req_right)-1] == C_DELIMIT) {
        end_with_delimit = true;
    }


    /* put token in to array tokens*/
    pch = strtok(req_right,DELIMIT);
    while (pch != NULL)
    {
        if (0 == strcmp(pch,DOUBLE_DOT)) {
            free(tokens[idx_tokens]);
            --idx_tokens;
            idx_tokens = (idx_tokens<=0?0:idx_tokens);
        } else if (0 == strcmp(pch,SINGLE_DOT)) {
            /* do nothing */
        } else {
            tokens[idx_tokens++] = strdup(pch);
        }
        pch = strtok (NULL, DELIMIT);
    }

    /* if end with /, we give a default cgi*/
    if(end_with_delimit) {
        tokens[idx_tokens++] = strdup(DEFAULT_CGI_FILE);
    }

    /* generate the real path using tokens in array tokens*/
    int i = 0;
    strcat(cgi_req->cmd_physical_path,DELIMIT);
    while(i<idx_tokens){
        if (i != idx_tokens-1) {/*not last token*/
            strcat(cgi_req->cmd_physical_path,tokens[i]);
            free(tokens[i]);
            strcat(cgi_req->cmd_physical_path,DELIMIT);
        } else {                /*last token, get command filename */
            strcat(cgi_req->cmd_physical_path, tokens[i]);
            strcpy(cgi_req->cmd_file, tokens[i]);
            free(tokens[i]);
        }
        ++i;
    }
}

static void
terminate_cgi(pid_t pid_cgi,const char* cgi_cgi_req)
{
    int status;
    switch (waitpid(pid_cgi,&status,WNOHANG)) {
    case 0:
        /*not finished yet*/
        break;
    case -1:
        /*error occur*/
        if (ECHILD == errno) { /*The process specified by pid (waitpid()) or 
                                 idtype and id (waitid()) does not exist or is not a child of
                                 the  calling  process*/
            xlog(LOG_WARNING,0,"child process [id:%d] doesn't existed, maybe already recollected by signal handler",pid_cgi);
            return;
        } else {               /*wait failed*/
            xlog(LOG_ERR,1,"waitpid");
            exit(EXIT_FAILURE);
        }
    default:                   /*cgi process already terminate*/
        if (WIFEXITED(status)) {
            xlog(LOG_INFO,0,"%s,%s",cgi_cgi_req," normally exit");
        } else {
            xlog(LOG_WARNING,0,"%s,%s",cgi_cgi_req," abnormally exit");
        }    
        return;
    }

    kill(pid_cgi, SIGTERM);
    sleep(3); /*wait 3 seconds for cgi process terminate*/
    switch (waitpid(pid_cgi,&status,WNOHANG)) {
    case 0:/*not finished*/
        xlog(LOG_ERR,1,"%s,%s",cgi_cgi_req," cannot be killed");
        break;
    case -1:/*error occur*/
        if( ECHILD != errno ) {
            xlog(LOG_ERR,1,"%s,%s",cgi_cgi_req," cannot be killed");
            break;
        }  
        /*go through*/
    default:
        xlog(LOG_INFO,0,"%s,%s",cgi_cgi_req," killed by sws");
        break;
    }
        
}

/*
       "AUTH_TYPE" |
       "CONTENT_LENGTH" | "CONTENT_TYPE" |  //mainly for POST
       "GATEWAY_INTERFACE" |
       "PATH_INFO" | "PATH_TRANSLATED" |
       "QUERY_STRING" | "REMOTE_ADDR" |
       "REMOTE_HOST" | "REMOTE_IDENT" |
       "REMOTE_USER" | "REQUEST_METHOD" |
       "SCRIPT_NAME" | "SERVER_NAME" |
       "SERVER_PORT" | "SERVER_PROTOCOL" |
       "SERVER_SOFTWARE" 
*/
static void
set_env_for_cgi(int msg_sock, const stru_cgi_request *pcgi_req, const REQ_INFO *request, const opt_struct *opts, const struct head_struct *pheader )
{
    char str_cl[MAX_CONTENT_LENGTH] = {0};
    sprintf(str_cl, "%i", (request->req_content_length <0?0:request->req_content_length));    
    setenv("CONTENT_LENGTH", str_cl, 1);  /*used by POST*/
    //setenv("CONTENT_TYPE", pheader->content_type, 1);

    if (opts->root) {
        setenv("DOCUMENT_ROOT", opts->root, 1);
    }
    setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);

    setenv("REQUEST_METHOD", request->method, 1);

    setenv("SERVER_SOFTWARE", SERVER_NAME, 1);
    char hostname[BUFFER_SIZE] = {0};
    if (gethostname(hostname, BUFFER_SIZE-1) == 0) {
        setenv("SERVER_NAME", hostname, 1);
        setenv("HTTP_HOST",   hostname, 1);
    }

    char str[MAX_PORT_LENGTH] = {0};
    sprintf(str, "%d", ntohs(opts->port));    
    setenv("SERVER_PORT", str, 1);
    setenv("SERVER_PROTOCOL", pheader->protocol, 1);

    //setenv("PATH_INFO", "", 1);
    setenv("PATH_TRANSLATED", pcgi_req->cmd_physical_path, 1);
    setenv("SCRIPT_FILENAME", pcgi_req->cmd_physical_path, 1);
    char scriptname[BUFFER_SIZE] = {0};
    strcpy(scriptname,DELIMIT);
    strcat(scriptname,pcgi_req->cmd_file);
    setenv("SCRIPT_NAME", scriptname, 1);
    if (strlen(pcgi_req->querystring)>0 ) {
        setenv("QUERY_STRING", pcgi_req->querystring, 1);
    } else {
        setenv("QUERY_STRING", "", 1);
    }

    //setenv("REMOTE_IDENT", "", 1);
    //setenv("REMOTE_USER", "", 1);
    char client_ip[INET6_ADDRSTRLEN] = {0};
    getClientIp(msg_sock,client_ip);
    setenv("REMOTE_HOST", client_ip, 1);
    setenv("REMOTE_ADDR", client_ip, 1);
}

static void 
cgi_response(stru_cgi_request *pcgi_req, const REQ_INFO *request, const opt_struct *opts, struct head_struct *pheader, int msg_sock)
{
    int fd_pipe_write_sws_read[2];
    int fd_pipe_read_sws_write[2];
    pid_t pid;

    /* parse query string*/
    char *querystring_tokens[MAX_TOKEN] = {0};
    char * pch;
    int idx_tokens = 1;
    querystring_tokens[0] = strdup(pcgi_req->cmd_file);

    char tmp_querystring[MAX_URI_LENGTH] = {0};
    strcpy(tmp_querystring,pcgi_req->querystring);
    pch = strtok(tmp_querystring,AND_MARK);
    while (pch != NULL)
    {
        querystring_tokens[idx_tokens++] = strdup(pch);
        pch = strtok (NULL, AND_MARK);
    }

    /* 
      create a pipe for transfer data between cgi and server 
     */
    if (pipe(fd_pipe_write_sws_read) < 0 || pipe(fd_pipe_read_sws_write) < 0) {
        xlog(LOG_ERR,1,"pipe");
        exit(EXIT_FAILURE);
    }
 
    /* create subprocess for cgi-script */
    if ((pid = fork()) < 0) {
        xlog(LOG_ERR,1,"fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {  /* child cgi-process */
        set_env_for_cgi(msg_sock, pcgi_req, request, opts, pheader);
        close(fd_pipe_write_sws_read[READ_SIDE]);       
        close(fd_pipe_read_sws_write[WRITE_SIDE]);       
        close(msg_sock);
        if (dup2(fd_pipe_write_sws_read[WRITE_SIDE],STDOUT_FILENO) != STDOUT_FILENO ||
            dup2(fd_pipe_write_sws_read[WRITE_SIDE],STDERR_FILENO) != STDERR_FILENO ||
            dup2(fd_pipe_read_sws_write[READ_SIDE],STDIN_FILENO) != STDIN_FILENO )
        {
            xlog(LOG_ERR,1,"dup2");
            exit(EXIT_FAILURE);
        }
        execvp(pcgi_req->cmd_physical_path,querystring_tokens);
        xlog(LOG_ERR,0,"%s execute failed.",pcgi_req->cmd_physical_path);
        exit(EXIT_FAILURE);
    } else {               /* parent */
        close(fd_pipe_write_sws_read[WRITE_SIDE]);
        close(fd_pipe_read_sws_write[READ_SIDE]);       
    }

    /*read post content send to cgi script*/
    if( strcmp(request->method,"POST") == 0) {
        char recv_buf[BUFFER_SIZE] = {0};
        ssize_t recv_bytes;
        ssize_t bytes_left = (request->req_content_length <0?0:request->req_content_length);
        int bytes_in_one_read = MIN(BUFFER_SIZE,bytes_left);
        while (bytes_left>0 && (recv_bytes = recv(msg_sock,recv_buf,bytes_in_one_read,0))>0) {
            //printf("%s\n",recv_buf);
            if (write(fd_pipe_read_sws_write[WRITE_SIDE],recv_buf,bytes_in_one_read) != bytes_in_one_read) {
                xlog(LOG_ERR,1,"write pipe");
                exit(EXIT_FAILURE);
            }
            bytes_left -= bytes_in_one_read;
            bytes_in_one_read = MIN(BUFFER_SIZE,bytes_left);
        }
    }
    close(fd_pipe_read_sws_write[WRITE_SIDE]); /*not need anymore*/
    
    struct timeval timeout;
    timeout.tv_sec = MAX_CGI_TIMEOUT;
    timeout.tv_usec = 0;
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(fd_pipe_write_sws_read[READ_SIDE], &rset);    

    int ready;
    if ((ready = select(fd_pipe_write_sws_read[READ_SIDE]+1, &rset, 0, 0, &timeout)) < 0) { /*select operation failed*/
        xlog(LOG_ERR,0,"select");
        exit(EXIT_FAILURE);
    } else if (0 == ready) { /* time out ,cgi still no response */
        xlog(LOG_ERR,0,"%s,%s","timeout when executing ",pcgi_req->cmd_physical_path);
        generic_response(msg_sock, BAD_REQUEST[BAD_408][0], BAD_REQUEST[BAD_408][1],pheader);
        terminate_cgi(pid,pcgi_req->cmd_physical_path);
        exit(EXIT_FAILURE);
    } 

    /*begin to process cgi response*/
    bytes_from_cgi = 0;
    char buf[BUFFER_SIZE];
    bzero(buf, sizeof(buf));
    ssize_t bytes_read;
    xsend(msg_sock, "%s 200 OK\r\n", pheader->protocol);
    xsend(msg_sock, "Server: " SERVER_NAME "\r\n");

    /*process cgi reponse header*/
    bool has_header = false;
    FILE *file_cgi_response = fdopen(fd_pipe_write_sws_read[READ_SIDE],"r");
    if(!file_cgi_response) {
        xlog(LOG_ERR,1,"fdopen");
        exit(EXIT_FAILURE);
    }
    while (!feof(file_cgi_response)) {
        bzero(buf, sizeof(buf));
        if (!fgets(buf, BUFFER_SIZE, file_cgi_response) && ferror(file_cgi_response)) {
            xlog(LOG_ERR,1,"fgets");
            break;
        }
        bytes_from_cgi += strlen(buf);
        /*header finished*/
        if (!strcmp(buf, "\r\n") || !strcmp(buf, "\n")) {
            break;
        }
        //printf("[header]:%s\n",buf);
        xsend(msg_sock,buf);
        has_header = true;
    }

    if (!has_header) {
        xlog(LOG_WARNING,0,"%s No header returned",pcgi_req->cmd_physical_path);
    }

    /*request method is HEAD, Done*/
    if (strcmp(request->method,"HEAD") == 0) {
        int i;
        for (i=0;i<idx_tokens;++i) {
            free(querystring_tokens[i]);
        }
        return;
    }

    /*process cgi response content*/
    xsend(msg_sock,HEADER_CONNECTION_CLOSE);

    while ((bytes_read = fread(buf, 1, BUFFER_SIZE, file_cgi_response)) > 0 ) {
        if (send(msg_sock,buf,bytes_read,0) < 0 ) {
            xlog(LOG_ERR,1,"send");
            break;
        } else {
            //printf("[content]%s\n",buf);
            bytes_from_cgi += bytes_read;
        }
    }

    if(bytes_from_cgi == 0) {
        generic_response(msg_sock, BAD_REQUEST[BAD_500][0], BAD_REQUEST[BAD_500][1], pheader);
    }

    /*
     * try to recollect the resource used by child process
     * since we have signal handler for SIGCHLD, so it may already collected by
     * signal handler, which means when call waitpid,we got ECHILD
     */
    terminate_cgi(pid,pcgi_req->cmd_physical_path);
    close(msg_sock);
    fclose(file_cgi_response);

    int i;
    for (i=0;i<idx_tokens;++i) {
        free(querystring_tokens[i]);
    }

}

/*check wether cmd_physical_path is a valid path*/
static bool
verify_physical_path(const stru_cgi_request *pcgi_req,int msg_sock, const struct head_struct *pheader)
{
    bool verify_ok = true;
    struct stat st;
    if (!get_stat(pcgi_req->cmd_physical_path,&st,USING_STAT)) {
        generic_response(msg_sock, BAD_REQUEST[BAD_404][0], BAD_REQUEST[BAD_404][1], pheader);
        verify_ok = false;
    } else if (!is_reg(pcgi_req->cmd_physical_path)) {
        generic_response(msg_sock, BAD_REQUEST[BAD_400][0], BAD_REQUEST[BAD_400][1], pheader);
        verify_ok = false;
    } else if (!is_executable(pcgi_req->cmd_physical_path)) {
        generic_response(msg_sock, BAD_REQUEST[BAD_403][0], BAD_REQUEST[BAD_403][1], pheader);
        verify_ok = false;
    }
    return verify_ok;
}

int
cgi_process(const REQ_INFO *request, const opt_struct *opts, struct head_struct *pheader, int msg_sock)
{
    stru_cgi_request cgi_req;

    /*initialize members of cgi_req*/
    bzero(cgi_req.cmd_physical_path, sizeof(cgi_req.cmd_physical_path));
    bzero(cgi_req.cmd_file, sizeof(cgi_req.cmd_file));
    bzero(cgi_req.querystring, sizeof(cgi_req.querystring));

    strcpy(cgi_req.cmd_physical_path,request->req_left);
    xlog(LOG_DEBUG,0,request->req_left);
    xlog(LOG_DEBUG,0,request->req_right);

    /* 
     ./sws -d -c ~/cgi ~/www
     GET /cgi-bin/../..////../123/../123/./456/../../123/456/a.pl?username=123&email=456@aol.com HTTP/1.0
     request->req_left:
            left->  /home/user/cgi
     request->req_right:
            right-> /../../../123/../123/./456/../../123/456/a.pl?username=123&email=456@aol.com
    */
    char tmp_right[MAX_URI_LENGTH] = {0};
    //printf("orig path:%s\n",request->req_right);
    url_decode(tmp_right,request->req_right);

    get_real_path(tmp_right, &cgi_req);
    //printf("converted:\n\tfull path:%s\n \tparameter:%s\n \tfile name:%s\n\n", cgi_req.cmd_physical_path, cgi_req.querystring, cgi_req.cmd_file);
    if (verify_physical_path(&cgi_req,msg_sock,pheader)) {
        cgi_response(&cgi_req,request,opts,pheader,msg_sock);
    }
    return bytes_from_cgi;
}


