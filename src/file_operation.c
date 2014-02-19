/*
 * file_operation.c
 * file_operation functions
 *
 */

#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#include "sws_assist.h"
#include "file_operation.h"

/*
 *
    get file stats
return:
    ture if file exist
 * 
 */
bool 
get_stat(const char *file,struct stat *pst,  int method)
{
    int err;
    if(USING_LSTAT == method){
        err = (0 == lstat (file, pst)?0:errno);
    } else {
        err = (0 == stat (file, pst)?0:errno);
    }
    if ( err && ENOENT != err ) {
        xlog(LOG_ERR,1,"stat");
        exit(EXIT_FAILURE);
    }
    return FILE_EXIST == err ;
}

/*
 *
 * check if file is directory
 *
 */
bool 
is_dir(const char *file)
{
    struct stat st;
    return (get_stat(file,&st,USING_STAT) && S_ISDIR (st.st_mode));
}

/*
 *
 * check if file is directory
 *
 */
bool 
is_reg(const char *file)
{
    struct stat st;
    return (get_stat(file,&st,USING_STAT) && S_ISREG (st.st_mode));
}


/*
 *
 * check if file is executable
 *
 */
bool 
is_executable(const char *file)
{
    return !access(file,X_OK);
}

/*
    lock file
*/
int
lockfile(int fd)
{
    struct flock fl;

    fl.l_type = F_WRLCK;
    fl.l_start = 0;
    fl.l_whence = SEEK_SET;
    fl.l_len = 0;
    return(fcntl(fd, F_SETLK, &fl));
}
