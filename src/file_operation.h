#ifndef FILE_OPERATION_H_
#define FILE_OPERATION_H_

/*
 * file_operation.h.h
 * file operation functions
 */

#include <sys/stat.h>
#include <stdbool.h>

enum {
    FILE_EXIST = 0
};

enum {
    USING_LSTAT,
    USING_STAT
};

bool is_dir(const char *file);
bool is_reg(const char *file);
bool is_executable(const char *file);
bool get_stat(const char *file,struct stat *pst,  int method);
int lockfile(int fd);

#endif 
