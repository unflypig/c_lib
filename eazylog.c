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
enum LEVEL{DEBUG = 0, INFO = 1, ERROR = 0, WARN = 1} level;
static int print_level = DEBUG | INFO | ERROR | WARN;
#if 0
#define DEBUG       1
#define INFO        2
#define ERROR       3
#define WARNING     4
enum LEVEL{DEBUG = 1, INFO, ERROR, WARNING} level;
static int print_level = DEBUG | INFO | ERROR | WARNING;
int zt_log(enum LEVEL level,__FILE__, __FUNCTION__,__LINE__, char *fmt, ...)
{
    if(level & print_level) {
        //get time strap
        struct tm *t;
        time_t tt;time(&tt);
        t = localtime(&tt);
        char time_strap[32] = {0};
        sprintf(time_strap, "%4d-%2d-%2d %02d:%02d:%02d", t->tm_year+1990,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min, t->tm_sec);
        //printf("[%s][%s][%s][%s(%d)]:"format, #level,time_strap, __FILE__, __FUNCTION__, __LINE__, ##argv);
        printf("[%s]", time_strap);
        //printf("[%s]", __TIME__);
        //get format string
        va_list ap;
        int d;
        double f;
        char c;
        char *s;
        char flag;
        switch (level){
            case DEBUG:
                printf("[%-7s]", "DEBUG");
                break;
            case INFO:
                printf("[%-7s]", "INFO");
                break;
            case ERROR:
                printf("[%-7s]", "ERROR");
                break;
            case WARNING:
                printf("[%-7s]", "WARNING");
                break;
            default:
                return -1;
        }
        printf("[%s->%s(%d)]:",__FILE__, __FUNCTION__, __LINE__);
        va_start(ap,fmt);
        while (*fmt){
            flag=*fmt++;
            if(flag!='%'){
                putchar(flag);
                continue;
            }
            flag=*fmt++;//记得后移一位
            switch (flag){
                case 's':
                    s=va_arg(ap,char*);
                    printf("%s",s);
                    break;
                case 'd': /* int */
                    d = va_arg(ap, int);
                    printf("%d", d);
                    break;
                case 'f': /* double*/
                    d = va_arg(ap,double);
                    printf("%d", d);
                    break;
                case 'c': /* char*/
                    c = (char)va_arg(ap,int);
                    printf("%c", c);
                    break;
                default:
                    putchar(flag);
                    break;
              }
        }
        va_end(ap);
    }else{
        return -1;
    }
    return 0;
}
#define LOG_INFO(format,...)if(INFO & print_level)do{\
    printf("["__DATE__""__TIME__"[INFO]"__FILE__"->%s(%d)]"format "\n", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
}while(0)
#endif
#define LOG_INFO(format,...)if(INFO & print_level)do{\
    printf("["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n","INFO", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
}while(0)
#define LOG_DEBUG(format,...)if(DEBUG & print_level)do{\
    printf("["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n", "DEBUG", __FUNCTION__,__LINE__, ##__VA_ARGS__);\
}while(0)
#define LOG_ERROR(format,...)if(ERROR & print_level)do{\
    printf("["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n", "ERROR",__FUNCTION__,__LINE__, ##__VA_ARGS__);\
}while(0)
#define LOG_WARN(format,...)if(WARN & print_level)do{\
    printf("["__DATE__""__TIME__"[%-5s]["__FILE__"->%s(%d)]:"format "\n", "WARN",__FUNCTION__,__LINE__, ##__VA_ARGS__);\
}while(0)
#if 0
#define LOG_DEBUG(format,...)printf("["__DATE__""__TIME__"[DEBUG]"__FILE__"->%s(%d)]"format "\n", __FUNCTION__,__LINE__, ##__VA_ARGS__)
#define LOG_ERROR(format,...)printf("["__DATE__""__TIME__"[ERROR]"__FILE__"->%s(%d)]"format "\n", __FUNCTION__,__LINE__, ##__VA_ARGS__)
#define LOG_WARN(format,...)printf("["__DATE__""__TIME__"[WARN]"__FILE__"->%s(%d)]"format "\n", __FUNCTION__,__LINE__, ##__VA_ARGS__)
#endif
int main(int argc, char* argv[]){
    int i = 10;
    char *name = "zhangtao";
#if 0
    zt_log(DEBUG,"this log is awsorm, i is [%d], name is [%s]\n", i, name);
    zt_log(INFO,"this log is awsorm\n");
    zt_log(ERROR,"this log is awsorm\n");
    zt_log(WARNING,"this log is awsorm\n");
#else
    LOG_INFO("this log is awsorm, i is [%d], name is [%s]", i, name);
    LOG_DEBUG("this log is awsorm, i is [%d], name is [%s]", i, name);
    LOG_ERROR("this log is awsorm, i is [%d], name is [%s]", i, name);
    LOG_WARN("this log is awsorm, i is [%d], name is [%s]", i, name);
#if 0
    LOG("this log is awsorm, i is [%d], name is [%s]\n", i, name);
    LOG("this log is awsorm, i is [%d], name is [%s]\n", i, name);
    LOG("this log is awsorm, i is [%d], name is [%s]\n", i, name);
    LOG("this log is awsorm, i is [%d], name is [%s]\n", i, name);
    LOG("this log is awsorm, i is [%d], name is [%s]\n", i, name);
    LOG("this log is awsorm, i is [%d], name is [%s]\n", i, name);
    LOG("this log is awsorm, i is [%d], name is [%s]\n", i, name);
#endif
    return 0;
#endif
}
