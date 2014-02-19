/*
 * sws.c
 *
 * By Hao Wu, Hui Zheng, Yulong Luo, Xin Sun
 * hwu9@stevens.edu
 * hzheng5@stevens.edu
 * yluo4@stevens.edu
 * xsun10@steven
 */
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <errno.h>
#if defined(sun) ||defined(__sun)
    #include <ast/getopt.h>
#else
    #include <getopt.h>
    #include <unistd.h>
#endif
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "sws_assist.h"
#include "req_process.h"
#include "file_operation.h"
#include "sws.h"

#define PROGRAM_NAME "sws"
#define OPTSTRING "c:dhi:l:p:"
#define MAX_PENDING_CONNECTIONS 10
#define REQUEST_TIMEOUT 30
#define LOCKFILE "/.sws.pid"
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)

bool g_debug = false;
static bool intr_or_quit = false;

static struct option const long_opts[] =
{
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {NULL, 0, NULL, 0}
};


bool
already_running(void)
{
    int     fd;
    char    buf[16];
    char filename[FILENAME_MAX] = {0};
    struct passwd *pw = getpwuid(getuid());
    if (!pw || !pw->pw_dir) {
        perror("getpwuid");
        exit(EXIT_FAILURE);
    }
    const char *homedir = pw->pw_dir;
    
    strcpy(filename,homedir);
    strcat(filename,LOCKFILE);
    fd = open(filename, O_RDWR|O_CREAT, LOCKMODE);
    if (fd < 0) {
        xlog(LOG_ERR, 1,"can't open %s\n", filename);
        fprintf(stderr,"can't open %s\n", filename);
        exit(EXIT_FAILURE);
    }
    if (lockfile(fd) < 0) {
        if (errno == EACCES || errno == EAGAIN) {
            close(fd);
            return(true);
        }
        xlog(LOG_ERR, 1,"can't lock %s\n", filename);
        fprintf(stderr,"can't lock %s\n", filename);
        exit(EXIT_FAILURE);
    }
    ftruncate(fd, 0);
    sprintf(buf, "%ld", (long)getpid());
    write(fd, buf, strlen(buf)+1);
    /*not close this fd, will keep it until sws exit*/
    return(false);
}

void 
display_help()
{
    fputs("Usage: sws [-dh][-c dir][-i address][-l file][-p port] dir \n\n"
          "  -c dir Allow execution of CGIs from the given directory. See CGIs for details.\n"
          "  -d Enter debugging mode. That is, do not daemonize, only accept one connection at a time\n"
          "     and enable logging to stdout.\n"
          "  -h Print a short usage summary and exit.\n"
          "  -i address Bind to the given IPv4 or IPv6 address. If not provided, sws will listen on all IPv4 and\n"
          "     IPv6 addresses on this host.\n"
          "  -l file Log all requests to the given file. See LOGGING for details.\n"
          "  -p port Listen on the given port. If not provided, sws will listen on port 8080.\n"
          "         --help     display this help and exit\n"
          "         --version  output version information and exit\n\n\n"
          ,stdout);
}

void
parse_option(int argc, char **argv,opt_struct * const opt)
{
    int c;
    opt->ipvs = 0;

    while ((c = getopt_long (argc, argv, OPTSTRING,long_opts, NULL)) != -1)
    {
        switch (c) {
            case 'c':
                opt->cgi_path = optarg;
                if (!is_dir(opt->cgi_path)) {
                    fprintf(stdout,"%s is not a valid directory\n",opt->cgi_path);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'd':
                g_debug = true;
                set_d(opt,'d');
                break;
            case 'i':
                convert_ip(optarg, opt);
                opt->ipaddr = optarg;
                break;
            case 'l':
                set_l(opt,'l',optarg);
                //check whether file is valid
                check_file_validate(opt);
                break;
            case 'p':
                opt->port = convert_port(optarg);
                break;
            case '?': 
            case 'h':     
            case GETOPT_HELP:
                display_help();
                exit(c=='?'?EXIT_FAILURE:EXIT_SUCCESS);
                break;
            case GETOPT_VERSION:
                fputs("sws 0.1.0\n",stdout);
                exit(EXIT_SUCCESS);
                break;
            default: //no options
                opt->no_options = true;
                break;
        }
    }
    if ((argc - optind) != 1) {
        fputs("No root directory specified.\n",stdout);
        display_help();
        exit(EXIT_FAILURE);
    } else {
        opt->root = *(argv+optind);
        if (!is_dir(opt->root)) {
            fprintf(stdout,"%s is not a valid directory\n",opt->root);
            exit(EXIT_FAILURE);
        }
    }
}

static void 
sig_handler(int sig)
{
    int status;
    int pid;
    switch (sig) {
    case SIGCHLD:
        if ((pid=waitpid(-1,&status,WNOHANG)) > 0) {
            fprintf(stdout,"%d's child-process %d exit(status:%d)\n",getpid(),pid,status);
        }
        break;
    case SIGHUP:
        break;
    case SIGINT:  
    case SIGQUIT:
        fprintf(stdout,"customer quit\n");
        intr_or_quit = true;
        break;
    default:
        break;
    }
}


static void
reg_signal_handler()
{
    struct sigaction sa;

    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART|SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
       fprintf(stderr, "Unable to establish signal handler for SIGCHLD: %s\n",
           strerror(errno));
       exit(EXIT_FAILURE);
    }

#ifndef __linux__ /*bsd/sunos not support*/
    sa.sa_flags = 0;
#else
    sa.sa_flags = SA_INTERRUPT;
#endif
    if (sigaction(SIGINT, &sa, NULL) == -1) {
       fprintf(stderr, "Unable to establish signal handler for SIGINT: %s\n",
           strerror(errno));
       exit(EXIT_FAILURE);
    }
    if (sigaction(SIGQUIT, &sa, NULL) == -1) {
       fprintf(stderr, "Unable to establish signal handler for SIGQUIT: %s\n",
           strerror(errno));
       exit(EXIT_FAILURE);
    }

}

/*
 *sws implement
 */
bool
do_sws (opt_struct *opts)
{
    int main_sock;
    int msg_sock;
    pid_t pid;

    int max_request = opts->debug?1:MAX_PENDING_CONNECTIONS;

    /* Create socket */
    main_sock = create_socket(opts);

    /* listen */
    if (listen(main_sock, max_request) < 0) {
        xlog(LOG_ERR,1,"listen");
        exit(EXIT_FAILURE);
    }
    fprintf(stdout,"[%d] listening on %s:%d\n",getpid(),opts->ipaddr, ntohs(opts->port));

    reg_signal_handler();

    if (!opts->debug) {
        if (-1 == daemon(0,0)) {
            xlog(LOG_ERR,1,"daemon");
            exit(EXIT_FAILURE);
        }

        openlog(PROGRAM_NAME,LOG_PID|LOG_CONS,LOG_DAEMON);
    }

    if (chdir(opts->root)<0) {
        xlog(LOG_ERR,1,"chdir");
        exit(EXIT_FAILURE);
    }


    /* main loop */
    do {
        msg_sock = accept(main_sock, 0, 0);

        if (EINTR == errno && intr_or_quit) { 
            break;
        }
        if (msg_sock == -1) {  
            xlog(LOG_ERR,1,"accept");
            continue;
        }

	/*set receive time out option*/
        struct timeval tv;
        tv.tv_sec = REQUEST_TIMEOUT;
        tv.tv_usec = 0;
        if (setsockopt(msg_sock, SOL_SOCKET, SO_RCVTIMEO,(struct timeval *)&tv,sizeof(struct timeval)) < 0) {
	  xlog(LOG_ERR,1,"setsockopt(set receive time out)");
	  exit(EXIT_FAILURE);
        }

        if (!opts->debug) {
            pid = fork();
            if (pid < 0) {
                xlog(LOG_ERR,1,"fork");
                exit(EXIT_FAILURE);
            } else if (0 == pid) {   
                close(main_sock);  
                req_process(msg_sock,opts);
                exit(EXIT_SUCCESS);
            } else {              
                close(msg_sock);     
            }
        } else {
            req_process(msg_sock,opts);
        }
    } while (true);

    shutdown(main_sock,SHUT_RDWR);
    //close(main_sock);
    /*
     * Since this program has an infinite loop, the socket "sock" is
     * never explicitly closed.  However, all sockets will be closed
     * automatically when a process is killed or terminates normally. 
     */
    return EXIT_SUCCESS;

}


void 
set_sock_reuse(int sock)
{
    /*set reuse option*/
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
      xlog(LOG_ERR,1,"setsockopt(set reuse)");
      exit(EXIT_FAILURE);
    }
}

/*if ipv4 or ipv6 address is provided, then we listening on that
 *address, otherwise we listening on all ipv6 address, which can
 *accept connetion from either ipv4 or ipv6 client host.
 */ 

int create_socket(opt_struct *opts)
{
    int sd;
    struct sockaddr_in server;
    struct sockaddr_in6 server_v6;
    
    switch(opts->ipvs)
    {
        /*case no address is provided*/
        case 0:
        /*case ipv6 address is provided*/
        case 2:
            if((sd = socket(AF_INET6, SOCK_STREAM, 0)) < 0)
            {
                xlog(LOG_ERR,1,"opening stream socket");
                exit(EXIT_FAILURE);
            }

            memset(&server_v6, 0, sizeof(server_v6));
            server_v6.sin6_family = AF_INET6;
            server_v6.sin6_port = opts->port;
            if(opts->ipvs == 0)
                server_v6.sin6_addr = in6addr_any;
            else
                server_v6.sin6_addr = opts->ipv6;

            set_sock_reuse(sd);

            if(bind(sd, 
                (struct sockaddr*)&server_v6,
                sizeof(server_v6)) < 0)
            {
                xlog(LOG_ERR,1,"binding stream socket");
                exit(EXIT_FAILURE);
            }
            break;
        /*case ipv4 address is provided*/
        case 1:
            if((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            {
                xlog(LOG_ERR,1,"opening stream socket");
                exit(EXIT_FAILURE);
            }
        
            server.sin_family = AF_INET;
            server.sin_port = opts->port;
            server.sin_addr.s_addr = opts->ip;
        
            set_sock_reuse(sd);

            if(bind(sd, 
                (struct sockaddr*)&server,
                sizeof(server)) < 0)
            {
                xlog(LOG_ERR,1,"binding stream socket");
                exit(EXIT_FAILURE);
            }
            break;
    }

    return sd;
}

