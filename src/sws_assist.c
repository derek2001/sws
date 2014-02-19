/*
 *  sws_assist.c
 *
 *
 * By Hao Wu, Hui Zheng, Yulong Luo, Xin Sun
 * hwu9@stevens.edu
 * hzheng5@stevens.edu
 * yluo4@stevens.edu
 * xsun10@steven
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(sun) ||defined(__sun)
#include <strings.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>

#include "sws_assist.h"

extern bool g_debug;
/*
 *
 * set default option 
 *
 */
static void 
set_default_options(opt_struct * const opts)
{
    opts->ip = INADDR_ANY;
    opts->port = htons(DEFAULT_PORT);
}

/*
 * struct option init
 * 
 */
void opt_init(opt_struct * const opts)
{
    memset(opts,0,sizeof(opt_struct));
    set_default_options(opts);
}


void
set_d(opt_struct *const opts,char set_whom)
{
    assert('d' == set_whom);
    opts->debug   = ('d'==set_whom?true:false);
}

void
set_l(opt_struct *const opts,char set_whom,const char *arg)
{
    assert('l' == set_whom);
    opts->logfile = ('l'==set_whom?arg:NULL);
}

/*
 * convert string to in_port_t
 */
in_port_t convert_port(const char *str_port)
{
    in_port_t port;
    int i = atoi(str_port);
    if (i<1 || i>65535) { /* invalid port */
        fprintf(stderr,"Invalid port number, port number should between 1 and 65535\n");
        exit(EXIT_FAILURE);
    } else {
        port = htons(i);
    } 
    return port;  
}

/*
 * convert string to in_addr_t if use ipv4 or in6_addr if use ipv6
 */
void convert_ip(const char *str_ip, opt_struct * const opt)
{
    struct addrinfo *answer=NULL, hint, *add;
    struct in6_addr vaddr={};

    memset(&hint, '\0', sizeof(hint));
    int ret = getaddrinfo(str_ip, NULL, &hint, &answer);
    if(ret)
    {
        fprintf(stderr, "Invalid ip address: %s\n", str_ip);
        exit(EXIT_FAILURE);
    }
    
    for(add = answer; add != (struct addrinfo*)0; add = add->ai_next)
    {
        switch( add->ai_family)
            {
            case AF_INET6:
                opt->ipvs = 2;
                if(inet_pton(AF_INET6, str_ip, &vaddr) != 1)
		{
                    perror("converting ip address");
                    exit(EXIT_FAILURE);
                }
                opt->ipv6 = vaddr;
                break;
            case AF_INET:
                opt->ipvs = 1;
                opt->ip = inet_addr(str_ip);
                break;
            }
    }
}

void getClientIp(int client_socket, char *ipstr)
{
    static const char ip_not_found[] = "ip_not_found";
    socklen_t len;
    struct sockaddr_storage addr;
    //int port;

    len = sizeof addr;
    if (getpeername(client_socket, (struct sockaddr*)&addr, &len) < 0) {
        perror("getpeername");
        strcpy(ipstr,ip_not_found);
        return;
    }

    // deal with both IPv4 and IPv6:
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        //port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ipstr, INET6_ADDRSTRLEN);
    } else { // AF_INET6
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
        //port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ipstr, INET6_ADDRSTRLEN);
    }
}

/*
 *	Check validation of the log file
 */
void check_file_validate(opt_struct *const opts)
{
	int fd;
	
	if (opts->logfile == NULL) {
		fprintf(stderr,"Invalid log file location: %s\n",opts->logfile);
		exit(EXIT_FAILURE);
	} 
	if ((fd = open(opts->logfile,O_RDWR|O_CREAT|O_APPEND,S_IRWXU|S_IRWXG)) == -1) { 
		/*try to open or create the LOG file*/
		fprintf(stderr,"Unable to create/open log file: %s\n",strerror(errno));
		exit(EXIT_FAILURE);
	} else if (close(fd) == -1) {
		fprintf(stderr,"Closing failed: %s\n",strerror(errno));
	}
}

/*
 *  write critical error/info to syslog
 *  priority : LOG_ERR, LOG_WARNING, LOG_INFO, ...
 *  showErrnoMsg: whether record strerrno , for perror it should always be true
 * */
void
xlog(int priority, bool showErrnoMsg, const char *fmt, ...)
{
    int errno_bak = errno;
    va_list va;
    char buf[1024];
    
    va_start(va, fmt);
    bzero(buf, 1024);
    vsprintf(buf, fmt, va);
    va_end(va);
    if (g_debug) {
        if (showErrnoMsg) {
            errno = errno_bak;
            perror(buf);
        } else {
            fprintf(stderr,"%s\n",buf);
        }
    } else {
        if(showErrnoMsg)
            syslog(priority,"%s:%s",buf,strerror(errno));
        else {
            syslog(priority,"%s",buf);
        }
    }
}

/*
 * Convert a character from two hex digits
 * to the raw character. (URL decode)
 */
static char from_hex(char ch) {
    return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}


/*
 *  input:  "a%20b.html"
 *  output: "a b.html"
 *
 */
void
url_decode(char *decoded, const char *encoded)
{
    if (strstr(encoded, "%")) {
        while (*encoded) {
            if (*encoded == '%') {
                if (encoded[1] && encoded[2]) {
                    *decoded++ = from_hex(encoded[1]) << 4 | from_hex(encoded[2]);
                    encoded += 2;
                }
            } else if (*encoded == '+') {
                *decoded++ = ' ';
            } else {
                *decoded++ = *encoded;
            }
            encoded++;
        }
        *decoded = '\0';
    } else {
        strcpy(decoded,encoded);
    }
}
