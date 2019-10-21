/*================================================================
 *   Copyright (C) 2019 Sangfor Ltd. All rights reserved.
 *
 *   文件名称：log.c
 *   创 建 者：zt
 *   创建日期：2019年10月19日
 *   描    述：
 *
 ================================================================*/
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
/*打印宏开关*/
typedef enum {DEBUG = 1, INFO = 1, ERROR = 1, WARN = 1} ENUM_LEVEL;
/*写入文件开关0/1 写入文件/不写入文件*/
static int print_level = DEBUG | INFO | ERROR | WARN;
#define WRITE_LOG_TO_FILE_FLAG 1
#define LOG_FILE_PATH "/tmp/my.log"
#define LOG_INFO(format,...)if(INFO & print_level)do{\
    printf("["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n","INFO", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
    if(WRITE_LOG_TO_FILE_FLAG){\
        FILE *log_p;\
        log_p = fopen(LOG_FILE_PATH, "a+");\
        if(log_p) {\
            fprintf(log_p, "["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n","INFO", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
            fclose(log_p);\
        }\
    }\
}while(0)
#define LOG_DEBUG(format,...)if(DEBUG & print_level)do{\
    printf("["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n","DEBUG", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
    if(WRITE_LOG_TO_FILE_FLAG){\
        FILE *log_p;\
        log_p = fopen(LOG_FILE_PATH, "a+");\
        if(log_p) {\
            fprintf(log_p, "["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n","DEBUG", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
            fclose(log_p);\
        }\
    }\
}while(0)
#define LOG_ERROR(format,...)if(ERROR & print_level)do{\
    printf("["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n","ERROR", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
    if(WRITE_LOG_TO_FILE_FLAG){\
        FILE *log_p;\
        log_p = fopen(LOG_FILE_PATH, "a+");\
        if(log_p) {\
            fprintf(log_p, "["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n","ERROR", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
            fclose(log_p);\
        }\
    }\
}while(0)
#define LOG_WARN(format,...)if(WARN & print_level)do{\
    printf("["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n","WARN", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
    if(WRITE_LOG_TO_FILE_FLAG){\
        FILE *log_p;\
        log_p = fopen(LOG_FILE_PATH, "a+");\
        if(log_p) {\
            fprintf(log_p, "["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n","WARN", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
            fclose(log_p);\
        }\
    }\
}while(0)
int main(int argc, char* argv[]){
    int i = 10;
    char *name = "zhangtao";
    LOG_DEBUG("this log is awsorm, i is [%d], name is [%s]", i, name);
    return 0;
}
