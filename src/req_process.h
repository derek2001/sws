/*
 * req_process.h
 *
 * By Hao Wu, Hui Zheng, Yulong Luo, Xin Sun
 * hwu9@stevens.edu
 * hzheng5@stevens.edu
 * yluo4@stevens.edu
 * xsun10@steven
 */


#ifndef REQ_PROCESS_H
#define REQ_PROCESS_H
#include "sws_assist.h"
#include "response.h"

void req_process(int msgsock,opt_struct *opts);
int read_line(int sd, char* buffer, int size);
void write_log(const char * file, struct log_msg_struct *log_msg, bool debug);
void remove_rl(char * target, char * ori_str);
#endif
