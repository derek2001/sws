#ifndef SWS_ASSIST_H_
#define SWS_ASSIST_H_

/*
 * sws_assist.h
 * sws_assist functions used by sws
 *
 *
 * By Hao Wu, Hui Zheng, Yulong Luo, Xin Sun
 * hwu9@stevens.edu
 * hzheng5@stevens.edu
 * yluo4@stevens.edu
 * xsun10@steven
 */


#include <netinet/in.h>
#include <stdbool.h>

/* command options */
#define GETOPT_HELP_OPTION_DECL \
      "help", no_argument, NULL, GETOPT_HELP
#define GETOPT_VERSION_OPTION_DECL \
      "version", no_argument, NULL, GETOPT_VERSION


enum
{ 
      GETOPT_HELP    = 1000,
      GETOPT_VERSION = 2000
};



enum {
    DEFAULT_PORT = 8080
};

typedef struct opt_struct_{
    bool no_options;    
    const char *cgi_path;    // −c dir Allow execution of CGIs from the given directory. See CGIs for details.
    bool debug;              // −d Enter debugging mode. That is, do not daemonize, only accept one connection at a time
                             //    and enable logging to stdout.
    bool help;               // −h Print a short usage summary and exit.
    in_addr_t ip;            // −i address Bind to the given IPv4 or IPv6 address. If not provided, sws will listen on all IPv4 and
    struct in6_addr ipv6;    //    IPv6 addresses on this host.
    int ipvs;                //    ip protocol version, 1 for v4, 2 for v6, 0 for no specify version.
    char* ipaddr;
    const char *logfile;     // −l file Log all requests to the given file. See LOGGING for details.
    in_port_t port;          // −p port Listen on the given port. If not provided, sws will listen on port 8080.
    const char *root;        // root path of sws
} opt_struct;

void opt_init(opt_struct *opts);
void set_d(opt_struct *const opts,char set_whom);
void set_l(opt_struct *const opts,char set_whom,const char *arg);
in_port_t convert_port(const char *str_port);
void convert_ip(const char *str_ip, opt_struct * const opt);
void getClientIp(int client_socket, char *ipstr);
void check_file_validate(opt_struct *const opts);
void xlog(int priority, bool showErrnoMsg, const char *fmt, ...);
void url_decode(char* decoded, const char *encoded);
#endif 
