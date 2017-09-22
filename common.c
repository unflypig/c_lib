/**************************************************************************
 * @brief       : this file is include common function
 * @author      : zhangtao@melinkr.com 
 * @date        : 2017.09.22
 * @note        : no
 **************************************************************************/

/*************************include head file below**************************/

#include <stdio.h>
#include <common.h>

/******************************define below********************************/
#define STAT_SUCCESS 0

/**************************************************************************
 * @author      : zhangtao@melinkr.com
 * @date        : 2017.09.22
 * @brief       : get file content via input path 
 * @param[in]   : char *file_path     file path you want read
 *                char *file_content  file content
 * @param[out]  : -1    open file error
 *                 0    read success
 * @others      : no
 **************************************************************************/
int read_file(char *file_path, char *file_content){
    FILE *fp;
    char line[N+1];
    if((fp=fopen(file_path, "rt")) == NULL){
        //printf("open file [%s] fail!\n", file_path);
        return -1;
    }else{
        printf("open file [%s] success!\n", file_path);
    }
    while(fgets(line, N, fp) != NULL){
        //printf("line = [%s]", line);
        int line_lenth = sizeof(line);
        line[line_lenth -1] = '\n';
        strcat(file_content, line);
    }
    fp == NULL;
    //printf("file content is [%s]\n", file_content);
    return STAT_SUCCESS;
}
