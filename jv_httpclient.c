/*******   http¿Í»§¶Ë³ÌÐò   httpclient.c   ************/
#include   <stdio.h>
#include   <stdlib.h>
#include   <string.h>
#include   <sys/types.h>
#include   <sys/socket.h>
#include   <errno.h>
#include   <unistd.h>
#include   <netinet/in.h>
#include   <limits.h>
#include   <netdb.h>
#include   <arpa/inet.h>
#include   <ctype.h>
#include   <time.h>
#include   <sys/time.h>
#include   "jv_httpclient.h"
//////////////////////////////httpclient.c   ¿ªÊŒ///////////////////////////////////////////
#include <fcntl.h>

#define HOST_NAME_LEN   256
#define URI_MAX_LEN     2048
#define RECV_BUF        8192
#define RCV_SND_TIMEOUT (10*1000)   //收发数据超时时间(ms)

typedef struct {
    int sock;                       //与服务器通信的socket
    FILE *in;                       //sock描述符转为文件指针，方便读写
    char host_name[HOST_NAME_LEN];  //主机名
    int port;                       //主机端口号
    char uri[URI_MAX_LEN];          //资源路径
    char buffer[RECV_BUF];          //读写缓冲
    int status_code;                //http状态码
    int chunked_flag;               //chunked传输的标志位
    int len;                        //Content-length里的长度
    char location[URI_MAX_LEN];     //重定向地址
    char *save_path;                //保存内容的路径指针
    FILE *save_file;                //保存内容的文件指针
    int recv_data_len;              //收到数据的总长度
    time_t start_recv_time;         //开始接受数据的时间
    time_t end_recv_time;           //结束接受数据的时间
} http_t;

/* 打印宏 */
#define MSG_DEBUG   0x01
#define MSG_INFO    0x02
#define MSG_ERROR   0x04

static int print_level = /*MSG_DEBUG |*/ MSG_INFO | MSG_ERROR;

#define lprintf(level, format, argv...) do{     \
    if(level & print_level)     \
        printf("[%s][%s(%d)]:"format, #level, __FUNCTION__, __LINE__, ##argv);  \
}while(0)

#define MIN(x, y) ((x) > (y) ? (y) : (x))

#define HTTP_OK         200
#define HTTP_REDIRECT   302
#define HTTP_NOT_FOUND  404

/* 不区分大小写的strstr */
static char *strncasestr(char *str, char *sub)
{
    if(!str || !sub)
        return NULL;

    int len = strlen(sub);
    if (len == 0)
    {
        return NULL;
    }

    while (*str)
    {
        if (strncasecmp(str, sub, len) == 0)
        {
            return str;
        }
        ++str;
    }
    return NULL;
}

/* 解析URL, 成功返回0，失败返回-1 */
/* http://127.0.0.1:8080/testfile */
static int parser_URL(char *url, http_t *info)
{
    char *tmp = url, *start = NULL, *end = NULL;
    int len = 0;

    /* 跳过http:// */
    if(strncasestr(tmp, "http://"))
    {
        tmp += strlen("http://");
    }
    start = tmp;
    if(!(tmp = strchr(start, '/')))
    {
        lprintf(MSG_ERROR, "url invaild\n");
        return -1;
    }
    end = tmp;

    /*解析端口号和主机*/
    info->port = 80;   //先附默认值80

    len = MIN(end - start, HOST_NAME_LEN - 1);
    strncpy(info->host_name, start, len);
    info->host_name[len] = '\0';

    if((tmp = strchr(start, ':')) && tmp < end)
    {
        info->port = atoi(tmp + 1);
        if(info->port <= 0 || info->port >= 65535)
        {
            lprintf(MSG_ERROR, "url port invaild\n");
            return -1;
        }
        /* 覆盖之前的赋值 */
        len = MIN(tmp - start, HOST_NAME_LEN - 1);
        strncpy(info->host_name, start, len);
        info->host_name[len] = '\0';
    }

    /* 复制uri */
    start = end;
    strncpy(info->uri, start, URI_MAX_LEN - 1);

    lprintf(MSG_INFO, "parse url ok\nhost:%s, port:%d, uri:%s\n",
        info->host_name, info->port, info->uri);
    return 0;
}

/* dns解析,返回解析到的第一个地址，失败返回-1，成功则返回相应地址 */
static unsigned long dns(char* host_name)
{

    struct hostent* host;
    struct in_addr addr;
    char **pp;

    host = gethostbyname(host_name);
    if (host == NULL)
    {
        lprintf(MSG_ERROR, "gethostbyname %s failed\n", host_name);
        return -1;
    }

    pp = host->h_addr_list;

    if (*pp!=NULL)
    {
        addr.s_addr = *((unsigned int *)*pp);
        lprintf(MSG_INFO, "%s address is %s\n", host_name, inet_ntoa(addr));
        pp++;
        return addr.s_addr;
    }

    return -1;
}

/* 设置发送接收超时 */
static int set_socket_option(int sock)
{
    struct timeval timeout;

    timeout.tv_sec = RCV_SND_TIMEOUT/1000;
    timeout.tv_usec = RCV_SND_TIMEOUT%1000*1000;
    lprintf(MSG_DEBUG, "%ds %dus\n", (int)timeout.tv_sec, (int)timeout.tv_usec);
    //设置socket为非阻塞
    // fcntl(sock ,F_SETFL, O_NONBLOCK); //以非阻塞的方式，connect需要重新处理

    // 设置发送超时
    if(-1 == setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
            sizeof(struct timeval)))
    {
        lprintf(MSG_ERROR, "setsockopt error: %m\n");
        return -1;
    }

    // 设置接送超时
    if(-1 == setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
            sizeof(struct timeval)))
    {
        lprintf(MSG_ERROR, "setsockopt error: %m\n");
        return -1;
    }

    return 0;
}

/* 连接到服务器 */
static int connect_server(http_t *info)
{
    int sockfd;
    struct sockaddr_in server;
    unsigned long addr = 0;
    unsigned short port = info->port;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd)
    {
        lprintf(MSG_ERROR, "socket create failed\n");
        goto failed;
    }

    if(-1 == set_socket_option(sockfd))
    {
        goto failed;
    }

    if ((addr = dns(info->host_name)) == -1)
    {
        lprintf(MSG_ERROR, "Get Dns Failed\n");
        goto failed;
    }
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = addr;

    if (-1 == connect(sockfd, (struct sockaddr *)&server, sizeof(struct sockaddr)))
    {
        lprintf(MSG_ERROR, "connect failed: %m\n");
        goto failed;
    }

    info->sock = sockfd;
    return 0;

failed:
    if(sockfd != -1)
        close(sockfd);
    return -1;
}

/* 发送http请求 */
static int send_request(http_t *info)
{
    int len;

    memset(info->buffer, 0x0, RECV_BUF);
    snprintf(info->buffer, RECV_BUF - 1, "GET %s HTTP/1.1\r\n"
        "Accept: */*\r\n"
        "User-Agent: Mozilla/5.0 (compatible; MSIE 5.01; Windows NT 5.0)\r\n"
        "Host: %s\r\n"
        "Connection: Close\r\n\r\n", info->uri, info->host_name);

    lprintf(MSG_DEBUG, "request:\n%s\n", info->buffer);
    return send(info->sock, info->buffer, strlen(info->buffer), 0);
}

/* 解析http头 */
static int parse_http_header(http_t *info)
{
    char *p = NULL;

    // 解析第一行
    fgets(info->buffer, RECV_BUF, info->in);
    p = strchr(info->buffer, ' ');
    //简单检查http头第一行是否合法
    if(!p || !strcasestr(info->buffer, "HTTP"))
    {
        lprintf(MSG_ERROR, "bad http head\n");
        return -1;
    }
    info->status_code = atoi(p + 1);
    lprintf(MSG_DEBUG, "http status code: %d\n", info->status_code);

    // 循环读取解析http头
    while(fgets(info->buffer, RECV_BUF, info->in))
    {
        // 判断头部是否读完
        if(!strcmp(info->buffer, "\r\n"))
        {
            return 0;   /* 头解析正常 */
        }
        lprintf(MSG_DEBUG, "%s", info->buffer);
        // 解析长度 Content-length: 554
        if((p = strncasestr(info->buffer, "Content-length")))
        {
            p = strchr(p, ':');
            p += 2;     // 跳过冒号和后面的空格
            info->len = atoi(p);
            lprintf(MSG_INFO, "Content-length: %d\n", info->len);
        }
        else if((p = strncasestr(info->buffer, "Transfer-Encoding")))
        {
            if((strncasestr(info->buffer, "chunked")))
            {
                info->chunked_flag = 1;
            }
            else
            {
                /* 不支持其他编码的传送方式 */
                lprintf(MSG_ERROR, "Not support %s", info->buffer);
                return -1;
            }
            lprintf(MSG_INFO, "%s", info->buffer);
        }
        else if((p = strncasestr(info->buffer, "Location")))
        {
            p = strchr(p, ':');
            p += 2;     // 跳过冒号和后面的空格
            strncpy(info->location, p, URI_MAX_LEN - 1);
            lprintf(MSG_INFO, "Location: %s\n", info->location);
        }
    }
    lprintf(MSG_ERROR, "bad http head\n");
    return -1;  /* 头解析出错 */
}

/* 保存服务器响应的内容 */
static int save_data(http_t *info, const char *buf, int len)
{
    int total_len = len;
    int write_len = 0;

    // 文件没有打开则先打开
    if(!info->save_file)
    {
        info->save_file = fopen(info->save_path, "w");
        if(!info->save_file)
        {
            lprintf(MSG_ERROR, "fopen %s error: %m\n", info->save_path);
            return -1;
        }
    }

    while(total_len)
    {
        write_len = fwrite(buf, sizeof(char), len, info->save_file);
        if(write_len < len && errno != EINTR)
        {
            lprintf(MSG_ERROR, "fwrite error: %m\n");
            return -1;
        }
        total_len -= write_len;
    }
    return 0;
}

/* 读数据 */
static int read_data(http_t *info, int len)
{
    int total_len = len;
    int read_len = 0;
    int rtn_len = 0;

    while(total_len)
    {
        read_len = MIN(total_len, RECV_BUF);
        // lprintf(MSG_DEBUG, "need read len: %d\n", read_len);
        rtn_len = fread(info->buffer, sizeof(char), read_len, info->in);
        if(rtn_len < read_len)
        {
            if(ferror(info->in))
            {
                if(errno == EINTR) /* 信号中断了读操作 */
                {
                    ;   /* 不做处理继续往下走 */
                }
                else if(errno == EAGAIN || errno == EWOULDBLOCK) /* 超时 */
                {
                    lprintf(MSG_ERROR, "socket recvice timeout: %dms\n", RCV_SND_TIMEOUT);
                    total_len -= rtn_len;
                    lprintf(MSG_DEBUG, "read len: %d\n", rtn_len);
                    break;
                }
                else    /* 其他错误 */
                {
                    lprintf(MSG_ERROR, "fread error: %m\n");
                    break;
                }
            }
            else    /* 读到文件尾 */
            {
                lprintf(MSG_ERROR, "socket closed by peer\n");
                total_len -= rtn_len;
                lprintf(MSG_DEBUG, "read len: %d\n", rtn_len);
                break;
            }
        }

        // lprintf(MSG_DEBUG, " %s\n", info->buffer);
        total_len -= rtn_len;
        lprintf(MSG_DEBUG, "read len: %d\n", rtn_len);
        if(-1 == save_data(info, info->buffer, rtn_len))
        {
            return -1;
        }
        info->recv_data_len += rtn_len;
    }
    if(total_len != 0)
    {
        lprintf(MSG_ERROR, "we need to read %d bytes, but read %d bytes now\n",
            len, len - total_len);
        return -1;
    }
    return 0;
}

/* 接收服务器发回的chunked数据 */
static int recv_chunked_response(http_t *info)
{
    long part_len;

    //有chunked，content length就没有了
    do{
        // 获取这一个部分的长度
        fgets(info->buffer, RECV_BUF, info->in);
        part_len = strtol(info->buffer, NULL, 16);
        lprintf(MSG_DEBUG, "part len: %ld\n", part_len);
        if(-1 == read_data(info, part_len))
            return -1;

        //读走后面的\r\n两个字符
        if(2 != fread(info->buffer, sizeof(char), 2, info->in))
        {
            lprintf(MSG_ERROR, "fread \\r\\n error : %m\n");
            return -1;
        }
    }while(part_len);
    return 0;
}

/* 计算平均下载速度，单位byte/s */
static float calc_download_speed(http_t *info)
{
    int diff_time = 0;
    float speed = 0.0;

    diff_time = info->end_recv_time - info->start_recv_time;
    /* 最小间隔1s，避免计算浮点数结果为inf */
    if(0 == diff_time)
        diff_time = 1;
    speed = (float)info->recv_data_len / diff_time;

    return  speed;
}

/* 接收服务器的响应数据 */
static int recv_response(http_t *info)
{
    int len = 0, total_len = info->len;

    if(info->chunked_flag)
        return recv_chunked_response(info);

    if(-1 == read_data(info, total_len))
        return -1;

    return 0;
}

/* 清理操作 */
static void clean_up(http_t *info)
{
    if(info->in)
        fclose(info->in);
    if(-1 != info->sock)
        close(info->sock);
    if(info->save_file)
        fclose(info->save_file);
    if(info)
        free(info);
}

/* 下载主函数 */
int jv_http_download(char *url, char *save_path)
{
    http_t *info = NULL;
    char tmp[URI_MAX_LEN] = {0};

    if(!url || !save_path)
        return -1;

    //初始化结构体
    info = malloc(sizeof(http_t));
    if(!info)
    {
        lprintf(MSG_ERROR, "malloc failed\n");
        return -1;
    }
    memset(info, 0x0, sizeof(http_t));
    info->sock = -1;
    info->save_path = save_path;

    // 解析url
    if(-1 == parser_URL(url, info))
        goto failed;

    // 连接到server
    if(-1 == connect_server(info))
        goto failed;

    // 发送http请求报文
    if(-1 == send_request(info))
        goto failed;

    // 接收响应的头信息
    info->in = fdopen(info->sock, "r");
    if(!info->in)
    {
        lprintf(MSG_ERROR, "fdopen error\n");
        goto failed;
    }

    // 解析头部
    if(-1 == parse_http_header(info))
        goto failed;

    switch(info->status_code)
    {
        case HTTP_OK:
            // 接收数据
            lprintf(MSG_DEBUG, "recv data now\n");
            info->start_recv_time = time(0);
            if(-1 == recv_response(info))
                goto failed;

            info->end_recv_time = time(0);
            lprintf(MSG_INFO, "recv %d bytes\n", info->recv_data_len);
            lprintf(MSG_INFO, "Average download speed: %.2fKB/s\n",
                    calc_download_speed(info)/1000);
            break;
        case HTTP_REDIRECT:
            // 重启本函数
            lprintf(MSG_INFO, "redirect: %s\n", info->location);
            strncpy(tmp, info->location, URI_MAX_LEN - 1);
            clean_up(info);
            return jv_http_download(tmp, save_path);

        case HTTP_NOT_FOUND:
            // 退出
            lprintf(MSG_ERROR, "Page not found\n");
            goto failed;
            break;

        default:
            lprintf(MSG_INFO, "Not supported http code %d\n", info->status_code);
            goto failed;
    }

    clean_up(info);
    return 0;
failed:
    clean_up(info);
    return -1;
}


/*
¹ŠÄÜ£ºËÑË÷×Ö·ûŽ®ÓÒ±ßÆðµÄµÚÒ»žöÆ¥Åä×Ö·û
*/
char* JV_Rstrchr(char* s, char x)
{
    int i = strlen(s);
    if(!(*s))
    {
        return 0;
    }
    while(s[i-1])
    {
        if(strchr(s+(i-1), x))
        {
            return (s+(i-1));
        }
        else
        {
            i--;
        }
    }
    return 0;
}
 unsigned int JV_M_Param(char *pParam, int nMaxLen, char *pBuffer)
{
	int nLen	= 0;
	while(pBuffer && *pBuffer && *pBuffer != ',')
	{
		*pParam++ = *pBuffer++;
		nLen++;
	}
	return nLen;
}
 void JV_GetMessage(char* src, mhttp_attr_t *attr )
 {
 	mhttp_attr_t http_attr;
	char acBuff[256]= {0};
	unsigned int nLen = 1;
	char *pItem;
	char *pValue;
	char *mpValue;
 	memset(&http_attr, 0, sizeof(mhttp_attr_t));
	src=src+1;
	if(!(*src))
    {
        return;
    }
	while ((nLen = JV_M_Param(acBuff, sizeof(acBuff), src)) > 0)
	{

		acBuff[nLen]	= 0;
		acBuff[nLen-1]	= '\r';
		src += nLen+1;
		pItem = strtok(acBuff, ":");
		pValue = strtok(NULL, "\r");
		//printf("%s=%s\n", pItem, pValue);

		if (strncmp(pItem, "\"camera_code\"", 12) == 0)
		{
			strncpy(http_attr.camera_code, pValue+1, sizeof(http_attr.camera_code));
		}
		else if(strncmp(pItem, "\"secret_key\"", 10) == 0)
		{
			strncpy(http_attr.secret_key, pValue+1, sizeof(http_attr.secret_key));
		}
		else if(strncmp(pItem, "\"publish_url\"", 10) == 0)
		{
			int Len = 0;
			mpValue= pValue+10;
	        while(mpValue && *mpValue && *mpValue != '\\')
				{
					mpValue++;
					Len++;
				}
			strncpy(http_attr.publish_web, pValue+10,Len);
		}
	}
		//printf("http_attr.camera_code=%s\n",http_attr.camera_code);
		//printf("http_attr.secret_key=%s\n",http_attr.secret_key);
		//printf("http_attr.publish_web=%s\n",http_attr.publish_web);
		memcpy(attr, &http_attr,sizeof(mhttp_attr_t));

 }
/*
¹ŠÄÜ£ºŽÓ×Ö·ûŽ®srcÖÐ·ÖÎö³öÍøÕŸµØÖ·ºÍ¶Ë¿Ú£¬²¢µÃµœÓÃ»§ÒªÏÂÔØµÄÎÄŒþ
*/
void JV_GetHost(char* src, char* web, char* file, int* port)
{
    char* pA;
    char* pB;
   // memset(web, 0, sizeof(web));
   // memset(file, 0, sizeof(file));
    *port = 0;
    if(!(*src))
    {
        return;
    }
    pA = src;
    if(!strncmp(pA, "http://", strlen("http://")))
    {
        pA = src+strlen("http://");
    }
    else if(!strncmp(pA, "https://", strlen( "https://")))
    {
        pA = src+strlen( "https://");
    }
    pB = strchr(pA, '/');
    if(pB)
    {
        memcpy(web, pA, strlen(pA)-strlen(pB));
        if(pB+1)
        {
            memcpy(file, pB+1, strlen(pB)-1);
            file[strlen(pB)-1] = 0;
        }
    }
    else
    {
        memcpy(web, pA, strlen(pA));
    }
    if(pB)
    {
        web[strlen(pA) - strlen(pB)] = 0;
    }
    else
    {
        web[strlen(pA)] = 0;
    }
    pA = strchr(web, ':');
    if(pA)
    {
        *port = atoi(pA + 1);
    }
    else
    {
        *port = 80;
    }
}

/*
*filename:   httpclient.c
*purpose:   HTTPÐ­Òé¿Í»§¶Ë³ÌÐò
*/
int   JV_Http_get_message(char   *argv,mhttp_attr_t *attr)
{
    int sockfd = 0;
    char buffer[1024] = "";
    struct sockaddr_in   server_addr;
    struct hostent   *host;
    int portnumber = 0;
    int nbytes = 0;
    char host_addr[256] = "";
    char host_file[1024] = "";
    //FILE *fp;
    char request[1024] = "";
    int send = 0;
    int totalsend = 0;
    int i = 0;
	int j = 0;
    char *pt;
	mhttp_attr_t m_http_attr;
    printf( "parameter.1 is: %s\n ", argv);
    //ToLowerCase(argv[1]);/*œ«²ÎÊý×ª»»ÎªÈ«Ð¡ÐŽ*/
    //printf( "lowercase   parameter.1   is:   %s\n ",   argv[1]);

    JV_GetHost(argv, host_addr, host_file, &portnumber);/*·ÖÎöÍøÖ·¡¢¶Ë¿Ú¡¢ÎÄŒþÃûµÈ*/
    //printf( "webhost:%s\n ", host_addr);
   // printf( "hostfile:%s\n ", host_file);
    //printf( "portnumber:%d\n\n ", portnumber);

    if((host=gethostbyname(host_addr)) == NULL)/*È¡µÃÖ÷»úIPµØÖ·*/
    {
        printf("Gethostname   error, \n ");
        return -1;
    }

    /*   ¿Í»§³ÌÐò¿ªÊŒœšÁ¢   sockfdÃèÊö·û   */
    if((sockfd=socket(AF_INET,SOCK_STREAM,0)) == -1)/*œšÁ¢SOCKETÁ¬œÓ*/
    {
        printf("Socket   Error\a\n ");
        return -1;
    }

    /*   ¿Í»§³ÌÐòÌî³ä·þÎñ¶ËµÄ×ÊÁÏ   */
    bzero(&server_addr,sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(portnumber);
    server_addr.sin_addr=*((struct in_addr*)host->h_addr);

    /*   ¿Í»§³ÌÐò·¢ÆðÁ¬œÓÇëÇó   */
    if(connect(sockfd, (struct sockaddr*)(&server_addr), sizeof(struct sockaddr)) == -1)/*Á¬œÓÍøÕŸ*/
    {
        printf("Connect   Error\a\n ");
        close(sockfd);
        return -1;
    }

    sprintf(request,   "GET   /%s   HTTP/1.1\r\nAccept:   */*\r\nAccept-Language:   zh-cn\r\n\
User-Agent:   Mozilla/4.0   (compatible;   MSIE   5.01;   Windows   NT   5.0)\r\n\
Host:   %s:%d\r\nConnection:   Close\r\n\r\n ", host_file, host_addr, portnumber);

    //printf( "%s\n", request);/*×Œ±žrequest£¬œ«Òª·¢ËÍžøÖ÷»ú*/

    /*È¡µÃÕæÊµµÄÎÄŒþÃû*/
    if(host_file!=NULL&& *host_file)
    {
        pt = JV_Rstrchr(host_file, '/');
    }
    else
    {
        pt = 0;
    }

    /*·¢ËÍhttpÇëÇórequest*/
    send = 0;
    totalsend = 0;
    nbytes=strlen(request);
    while(totalsend < nbytes)
    {
        send = write(sockfd, request+totalsend, nbytes-totalsend);
        if(send == -1)
        {
            printf( "send error!%s\n ", strerror(errno));
            close(sockfd);
            return -1;
        }
        totalsend += send;
        printf("%d bytes send OK!\n ", totalsend);
    }

    //printf( "The   following   is   the   response   header:\n ");
    /*   Á¬œÓ³É¹ŠÁË£¬œÓÊÕhttpÏìÓŠ£¬response   */
	nbytes=read(sockfd,buffer,sizeof(buffer));
	if(nbytes!=418)
	{
		close(sockfd);
		return -1;
	}
  	printf("response = %d\n", nbytes);
    /*   œáÊøÍšÑ¶   */
    close(sockfd);
	j=0;
	for(i=0;i<nbytes;i++)
	{
		 if(j < 4)
		{
            if(buffer[i] == '\r' || buffer[i] == '\n')
			{
				j++;
			}
            else
			{
				j = 0;
			}
		 }
		 else
		 	break;
	}
	//printf("response = %s\n", &buffer[i]);
	JV_GetMessage(&buffer[i], &m_http_attr)  ;
	memcpy(attr, &m_http_attr,sizeof(mhttp_attr_t));
   	return 0;
}

/**
 *@brief ²éÕÒ×Ö·ûŽ®µÄÖµ
 *
 *@param body ÏûÏ¢Ìå¡£ÀàËÆÕâÑù£ºPLAY RTSP/1.0\r\n CSeq: 3\r\n Scale: 0.5\r\n Range: npt=0-
 *@param key Òª²éÕÒµÄŒüÖµ
 *@param seg ·Öžî·û£¬¿ÉÒÔÊÇ':', '=' µÈ
 *@param data ·µ»Øœá¹û
 *@param maxLen Ìá¹©µÄvalueµÄ×îŽó±£Žæ³€¶È
 */
static char *__get_line_value(const char *body, const char *key, char seg, char *data, int maxLen)
{
	char *p;
	char *dst;
	int len;

	p = (char *)strstr(body, key);
	if (data)
		data[0] = '\0';
	if (!p)
		return NULL;

	p += strlen(key);
	while (*p && *p != seg)
		p++;
	p++;
	dst = p;
	len = 0;
	while (*p && *p != '\r' && *p != '\n')
	{
		p++;
		len++;
	}

	if (*dst == '"')
	{
		dst++;
		len-=2; // " Ò»¶šÊÇ³É¶Ô³öÏÖµÄ
	}

	if (data)
		p = data;
	else
		p = (char *)malloc(len+1);
	memcpy(p, dst, len);
	p[len] = '\0';

	return p;
}

/**
 *@brief Œì²âContent-Length ×Ö¶Î£¬»ñÈ¡ÊýŸÝ×Ü³€¶È
 *@param predata ÒÑÊÕµœµÄÊýŸÝ
 *@return œ«ÒªÊÕµœµÄÊýŸÝµÄ×Ü³€¶È¡£
 *@note ÊýŸÝ×Ü³€¶ÈÎª£ºHTTPÍ·µÄ³€¶È + Content-LengthËùÖž¶šµÄÊýŸÝ³€¶È
 */
static int __get_content_length(const char *predata)
{
	char *tstr;
	//int ret;
	char temp[32];
	int contentLen;
	if (!predata)
	{
		return -1;
	}
	tstr = __get_line_value(predata, "Content-Length", ':', temp, 32);
	if (tstr)
	{
		contentLen = atoi(tstr);
		const char *p = strstr(predata, "\r\n\r\n");
		if (p)
		{
			return contentLen + p - predata + 4;
		}
		return contentLen + strlen(predata);
	}
	else
	{
		return 0;
	}

}

int JV_Http_post_message(const char *url, const char *req, int len, char *resp, int timeout, int bneedreply)
{
	int sockfd = 0;
	int ret = 0;
	int port = 0;
	int contentLen = 0;
	int chunkLen = 0;
	int offset = 0;
	int bChunked = 0;
	struct sockaddr_in servaddr;

	//char sendbuf[BUFSIZE_SEND] = {0};
	char sendbuf[1024*1024] = {0};
	char recvbuf[BUFSIZE_RECV] = {0};
	char domain[128] = {0};
	char ipaddr[16] = {0};

	memset(sendbuf, 0, sizeof(sendbuf));
	memset(ipaddr, 0, sizeof(ipaddr));

	if(url == NULL)
	{
		printf("NULL url\n");
		return -1;
	}
	if(strncmp(url, "http://", 7) != 0)
	{
		printf("invalid url\n");
		return -1;
	}
	printf("url=%s\n", url);

	const char *ptrDomain = strchr(url, ':');
	if(ptrDomain == NULL)
	{
		printf("Invalid url:%s\n", url);
		return -2;
	}
	const char *ptrPort = strchr(ptrDomain+1, ':');
	if(ptrPort == NULL)
	{
		port = 80;
		ptrPort = strchr(ptrDomain+3, '/');
		if(ptrPort == NULL)
		{
			//printf("strlen(url):%d\n", strlen(url));
			ptrPort = url + strlen(url);
		}
	}
	else
	{
		port = atoi(ptrPort+1);
	}
	strncpy(domain, ptrDomain+3, ptrPort-ptrDomain-3);

	struct hostent *host = gethostbyname(domain);
	if(host == NULL)
	{
		printf("gethostbyname failed\n");
		return -4;
	}
	//for(pptr=host->h_addr_list; *pptr!=NULL; pptr++)
	//	printf("address:%s\n", inet_ntop(host->h_addrtype, *pptr, str, sizeof(str)));
	if(inet_ntop(host->h_addrtype, host->h_addr, ipaddr, sizeof(ipaddr)) != NULL)
	{
		//printf("ipaddr:%s\n", ipaddr);
	}


	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("socket error:%s\n", strerror(errno));
		return -5;
	}

	struct timeval t;
	t.tv_sec = timeout;
	t.tv_usec = 0;

	ret = setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t));
	if(ret != 0)
	{
		perror("setsockopt");
		close(sockfd);
		return -6;
	}
	t.tv_sec = timeout;
	t.tv_usec = 0;
	ret = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
	if(ret != 0)
	{
		perror("setsockopt");
		close(sockfd);
		return -7;
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	if(inet_pton(AF_INET, ipaddr, &servaddr.sin_addr) <= 0)
	{
		printf("inet_pton error:%s\n", strerror(errno));
		close(sockfd);
		return -8;
	}

	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
	{
		printf("connect error:%s\n", strerror(errno));
		close(sockfd);
		return -9;
	}

	char *service = strstr(ptrDomain+3, "/");
	if(service == NULL)
	{
		service = "/";
	}

#if 0
	//printf("service=%s, domain=%s, port=%d\n", service, domain, port);
	sprintf(sendbuf, "POST %s HTTP/1.1\r\n"
					"Host: %s:%d\r\n"
					"Content-Type: application/json;charset=utf-8\r\n"
					"Content-Length: %d\r\n"
					"\r\n",
					service, domain, port, len);
#else
	sprintf(sendbuf, "POST %s HTTP/1.1\r\n"
					/*"Host: %s\r\n"
					"Content-Type: application/json\r\n"*/
					"Host: %s\r\n"
					"Content-Type: application/x-www-form-urlencoded\r\n"
					"Content-Length: %d\r\n"
					"Accept: */*\r\n"
					"Cache-Control: no-cache\r\n"
					"\r\n",
					service, domain, /*port,*/ len);
#endif

	strcat(sendbuf, req);
	//printf("http send:\n%s\n",sendbuf);


	ret = send(sockfd, sendbuf, strlen(sendbuf), 0);
	if (ret < 0)
	{
		printf("send error:%s\n", strerror(errno));
		close(sockfd);
		return -10;
	}

	while(bneedreply)
	{
		// ³¬³öBuffer(ÐèÒª°üÀš1×ÖœÚ'\0')
		if (offset + 1 >= sizeof(recvbuf))
		{
			printf("Buffer overflow! offset: %d, Recv:\n%s\n", offset, recvbuf);
			close(sockfd);
			return -11;
		}
		ret = recv(sockfd, recvbuf+offset, sizeof(recvbuf)-offset-1, 0);
		if (ret == -1)
		{
			printf("error: recv failed : %s\n", strerror(errno));
			close(sockfd);
			return -12;
		}
		else if (ret == 0)
		{
			printf("socket is closed\n");
			close(sockfd);
			return -13;
		}
		offset += ret;
		recvbuf[offset] = '\0';
		printf("recv:\n%s\n", recvbuf);
		if(strstr(recvbuf, "Content-Length:") != NULL)
		{
			if (contentLen == 0)
			{
				contentLen = __get_content_length(recvbuf);
			}
			if (contentLen == 0 || contentLen <= offset)
			{
				if(contentLen > 0)
				{
					char *httpBody = strstr(recvbuf, "\r\n\r\n");
					if(httpBody == NULL)
					{
						printf("http resp incorrect\n");
						close(sockfd);
						return -14;
					}
					strcpy(resp, httpBody+4);
				}
				break;
			}
		}
		else if(strstr(recvbuf, "Transfer-Encoding: chunked") != NULL)
		{
			bChunked = 1;
			char *httpBody = strstr(recvbuf, "\r\n\r\n");
			//printf("httpBody:\n%s\n", httpBody);
			offset = 0;
			if(httpBody != NULL)
			{
				char size[8] = {0};
				char *bodySize = httpBody+4;
				char *bodyStart = strstr(bodySize, "\r\n");
				if(bodyStart != NULL)
				{
					//printf("bodySize:\n%s\n", bodySize);
					strncpy(size, bodySize, (int)(bodyStart-bodySize));
					sscanf(size,"%x",&chunkLen);
					//printf("chunkLen:%d\n", chunkLen);

					bodyStart+=2;
					//printf("bodyStart:\n%s\n", bodyStart);
					if (contentLen + chunkLen + 1 >= BUFSIZE_RECV)
					{
						printf("recvbuf:\n%s\n", recvbuf);
						printf("Chunked over flow0! contentLen:%d, chunkLen: %d\n", contentLen, chunkLen);
						close(sockfd);
						return -15;
					}
					strncpy(resp+contentLen, bodyStart, chunkLen);

					contentLen += chunkLen;
					//printf("contentLen:%d\n", contentLen);
					if(strstr(bodyStart, "0\r\n\r\n") != NULL)//recvd all the chunks
					{
						//printf("1resp:\n%s\n", resp);
						break;
					}
				}
				else
				{
					printf("1HTTP resp incorrect!\n");
					close(sockfd);
					return -16;
				}
			}
			else
			{
				printf("2HTTP resp incorrect!\n");
				close(sockfd);
				return -17;
			}
		}
		else
		{
			if(bChunked == 1)
			{
				char *end = strstr(recvbuf, "0\r\n\r\n");
				if(end != NULL)//recvd all the chunks
				{
					if (contentLen + (end - recvbuf) + 1 >= BUFSIZE_RECV)
					{
						printf("recvbuf:\n%s\n", recvbuf);
						printf("Chunked over flow1! contentLen:%d, chunkLen: %d\n", contentLen, end-recvbuf);
						close(sockfd);
						return -18;
					}
					strncpy(resp+contentLen, recvbuf, end-recvbuf);
					//printf("2resp:\n%s\n", resp);
					break;
				}
				else
				{
					if (contentLen + chunkLen + 1 >= BUFSIZE_RECV)
					{
						printf("recvbuf:\n%s\n", recvbuf);
						printf("Chunked over flow2! contentLen:%d, chunkLen: %d\n", contentLen, ret);
						close(sockfd);
						return -19;
					}
					strncpy(resp+contentLen, recvbuf, ret);
				}
			}
			else
			{
				printf("3HTTP resp incorrect!\n");
				close(sockfd);
				return -20;
			}
		}
	}
	close(sockfd);
	return contentLen;
}
int JV_Http_post_json_message(const char *url, const char *req, int len, char *resp, int timeout, int bneedreply)
{
	int sockfd = 0;
	int ret = 0;
	int port = 0;
	int contentLen = 0;
	int chunkLen = 0;
	int offset = 0;
	int bChunked = 0;
	struct sockaddr_in servaddr;

	//char sendbuf[BUFSIZE_SEND] = {0};
	char sendbuf[1024*1024] = {0};
	char recvbuf[BUFSIZE_RECV] = {0};
	char domain[128] = {0};
	char ipaddr[16] = {0};

	memset(sendbuf, 0, sizeof(sendbuf));
	memset(ipaddr, 0, sizeof(ipaddr));

	if(url == NULL)
	{
		printf("NULL url\n");
		return -1;
	}
	if(strncmp(url, "http://", 7) != 0)
	{
		printf("invalid url\n");
		return -1;
	}
	printf("url=%s\n", url);

	const char *ptrDomain = strchr(url, ':');
	if(ptrDomain == NULL)
	{
		printf("Invalid url:%s\n", url);
		return -2;
	}
	const char *ptrPort = strchr(ptrDomain+1, ':');
	if(ptrPort == NULL)
	{
		port = 80;
		ptrPort = strchr(ptrDomain+3, '/');
		if(ptrPort == NULL)
		{
			//printf("strlen(url):%d\n", strlen(url));
			ptrPort = url + strlen(url);
		}
	}
	else
	{
		port = atoi(ptrPort+1);
	}
	strncpy(domain, ptrDomain+3, ptrPort-ptrDomain-3);

	struct hostent *host = gethostbyname(domain);
	if(host == NULL)
	{
		printf("gethostbyname failed\n");
		return -4;
	}
	//for(pptr=host->h_addr_list; *pptr!=NULL; pptr++)
	//	printf("address:%s\n", inet_ntop(host->h_addrtype, *pptr, str, sizeof(str)));
	if(inet_ntop(host->h_addrtype, host->h_addr, ipaddr, sizeof(ipaddr)) != NULL)
	{
		//printf("ipaddr:%s\n", ipaddr);
	}


	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("socket error:%s\n", strerror(errno));
		return -5;
	}

	struct timeval t;
	t.tv_sec = timeout;
	t.tv_usec = 0;

	ret = setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t));
	if(ret != 0)
	{
		perror("setsockopt");
		close(sockfd);
		return -6;
	}
	t.tv_sec = timeout;
	t.tv_usec = 0;
	ret = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
	if(ret != 0)
	{
		perror("setsockopt");
		close(sockfd);
		return -7;
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	if(inet_pton(AF_INET, ipaddr, &servaddr.sin_addr) <= 0)
	{
		printf("inet_pton error:%s\n", strerror(errno));
		close(sockfd);
		return -8;
	}

	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
	{
		printf("connect error:%s\n", strerror(errno));
		close(sockfd);
		return -9;
	}

	char *service = strstr(ptrDomain+3, "/");
	if(service == NULL)
	{
		service = "/";
	}

#if 1
	//printf("service=%s, domain=%s, port=%d\n", service, domain, port);
	sprintf(sendbuf, "POST %s HTTP/1.1\r\n"
					"Host: %s:%d\r\n"
					"Content-Type: application/json;charset=utf-8\r\n"
					"Content-Length: %d\r\n"
					"\r\n",
					service, domain, port, len);
#else
	sprintf(sendbuf, "POST %s HTTP/1.1\r\n"
					/*"Host: %s\r\n"
					"Content-Type: application/json\r\n"*/
					"Host: %s\r\n"
					"Content-Type: application/x-www-form-urlencoded\r\n"
					"Content-Length: %d\r\n"
					"Accept: */*\r\n"
					"Cache-Control: no-cache\r\n"
					"\r\n",
					service, domain, /*port,*/ len);
#endif

	strcat(sendbuf, req);
	//printf("http send:\n%s\n",sendbuf);


	ret = send(sockfd, sendbuf, strlen(sendbuf), 0);
	if (ret < 0)
	{
		printf("send error:%s\n", strerror(errno));
		close(sockfd);
		return -10;
	}

	while(bneedreply)
	{
		// ³¬³öBuffer(ÐèÒª°üÀš1×ÖœÚ'\0')
		if (offset + 1 >= sizeof(recvbuf))
		{
			printf("Buffer overflow! offset: %d, Recv:\n%s\n", offset, recvbuf);
			close(sockfd);
			return -11;
		}
		ret = recv(sockfd, recvbuf+offset, sizeof(recvbuf)-offset-1, 0);
		if (ret == -1)
		{
			printf("error: recv failed : %s\n", strerror(errno));
			close(sockfd);
			return -12;
		}
		else if (ret == 0)
		{
			printf("socket is closed\n");
			close(sockfd);
			return -13;
		}
		offset += ret;
		recvbuf[offset] = '\0';
		printf("recv:\n%s\n", recvbuf);
		if(strstr(recvbuf, "Content-Length:") != NULL)
		{
			if (contentLen == 0)
			{
				contentLen = __get_content_length(recvbuf);
			}
			if (contentLen == 0 || contentLen <= offset)
			{
				if(contentLen > 0)
				{
					char *httpBody = strstr(recvbuf, "\r\n\r\n");
					if(httpBody == NULL)
					{
						printf("http resp incorrect\n");
						close(sockfd);
						return -14;
					}
					strcpy(resp, httpBody+4);
				}
				break;
			}
		}
		else if(strstr(recvbuf, "Transfer-Encoding: chunked") != NULL)
		{
			bChunked = 1;
			char *httpBody = strstr(recvbuf, "\r\n\r\n");
			//printf("httpBody:\n%s\n", httpBody);
			offset = 0;
			if(httpBody != NULL)
			{
				char size[8] = {0};
				char *bodySize = httpBody+4;
				char *bodyStart = strstr(bodySize, "\r\n");
				if(bodyStart != NULL)
				{
					//printf("bodySize:\n%s\n", bodySize);
					strncpy(size, bodySize, (int)(bodyStart-bodySize));
					sscanf(size,"%x",&chunkLen);
					//printf("chunkLen:%d\n", chunkLen);

					bodyStart+=2;
					//printf("bodyStart:\n%s\n", bodyStart);
					if (contentLen + chunkLen + 1 >= BUFSIZE_RECV)
					{
						printf("recvbuf:\n%s\n", recvbuf);
						printf("Chunked over flow0! contentLen:%d, chunkLen: %d\n", contentLen, chunkLen);
						close(sockfd);
						return -15;
					}
					strncpy(resp+contentLen, bodyStart, chunkLen);

					contentLen += chunkLen;
					//printf("contentLen:%d\n", contentLen);
					if(strstr(bodyStart, "0\r\n\r\n") != NULL)//recvd all the chunks
					{
						//printf("1resp:\n%s\n", resp);
						break;
					}
				}
				else
				{
					printf("1HTTP resp incorrect!\n");
					close(sockfd);
					return -16;
				}
			}
			else
			{
				printf("2HTTP resp incorrect!\n");
				close(sockfd);
				return -17;
			}
		}
		else
		{
			if(bChunked == 1)
			{
				char *end = strstr(recvbuf, "0\r\n\r\n");
				if(end != NULL)//recvd all the chunks
				{
					if (contentLen + (end - recvbuf) + 1 >= BUFSIZE_RECV)
					{
						printf("recvbuf:\n%s\n", recvbuf);
						printf("Chunked over flow1! contentLen:%d, chunkLen: %d\n", contentLen, end-recvbuf);
						close(sockfd);
						return -18;
					}
					strncpy(resp+contentLen, recvbuf, end-recvbuf);
					//printf("2resp:\n%s\n", resp);
					break;
				}
				else
				{
					if (contentLen + chunkLen + 1 >= BUFSIZE_RECV)
					{
						printf("recvbuf:\n%s\n", recvbuf);
						printf("Chunked over flow2! contentLen:%d, chunkLen: %d\n", contentLen, ret);
						close(sockfd);
						return -19;
					}
					strncpy(resp+contentLen, recvbuf, ret);
				}
			}
			else
			{
				printf("3HTTP resp incorrect!\n");
				close(sockfd);
				return -20;
			}
		}
	}
	close(sockfd);
	return contentLen;
}

#if 0
int JV_Http_post_json_message(const char *url, const char *req, int len, char *resp, int timeout, int bneedreply)
{
	int sockfd = 0;
	int ret = 0;
	int port = 0;
	int contentLen = 0;
	int chunkLen = 0;
	int offset = 0;
	int bChunked = 0;
	struct sockaddr_in servaddr;

	//char sendbuf[BUFSIZE_SEND] = {0};
	char sendbuf[1024*1024] = {0};
	char recvbuf[BUFSIZE_RECV] = {0};
	char domain[128] = {0};
	char ipaddr[16] = {0};

	memset(sendbuf, 0, sizeof(sendbuf));
	memset(ipaddr, 0, sizeof(ipaddr));

	if(url == NULL)
	{
		printf("NULL url\n");
		return -1;
	}
	if(strncmp(url, "http://", 7) != 0)
	{
		printf("invalid url\n");
		return -1;
	}
	printf("url=%s\n", url);

	const char *ptrDomain = strchr(url, ':');
	if(ptrDomain == NULL)
	{
		printf("Invalid url:%s\n", url);
		return -2;
	}
	const char *ptrPort = strchr(ptrDomain+1, ':');
	if(ptrPort == NULL)
	{
		port = 80;
		ptrPort = strchr(ptrDomain+3, '/');
		if(ptrPort == NULL)
		{
			//printf("strlen(url):%d\n", strlen(url));
			ptrPort = url + strlen(url);
		}
	}
	else
	{
		port = atoi(ptrPort+1);
	}
	strncpy(domain, ptrDomain+3, ptrPort-ptrDomain-3);

	struct hostent *host = gethostbyname(domain);
	if(host == NULL)
	{
		printf("gethostbyname failed\n");
		return -4;
	}
	//for(pptr=host->h_addr_list; *pptr!=NULL; pptr++)
	//	printf("address:%s\n", inet_ntop(host->h_addrtype, *pptr, str, sizeof(str)));
	if(inet_ntop(host->h_addrtype, host->h_addr, ipaddr, sizeof(ipaddr)) != NULL)
	{
		//printf("ipaddr:%s\n", ipaddr);
	}


	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("socket error:%s\n", strerror(errno));
		return -5;
	}

	struct timeval t;
	t.tv_sec = timeout;
	t.tv_usec = 0;

	ret = setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t));
	if(ret != 0)
	{
		perror("setsockopt");
		close(sockfd);
		return -6;
	}
	t.tv_sec = timeout;
	t.tv_usec = 0;
	ret = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
	if(ret != 0)
	{
		perror("setsockopt");
		close(sockfd);
		return -7;
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	if(inet_pton(AF_INET, ipaddr, &servaddr.sin_addr) <= 0)
	{
		printf("inet_pton error:%s\n", strerror(errno));
		close(sockfd);
		return -8;
	}

	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
	{
		printf("connect error:%s\n", strerror(errno));
		close(sockfd);
		return -9;
	}

	char *service = strstr(ptrDomain+3, "/");
	if(service == NULL)
	{
		service = "/";
	}

#if 1
	//printf("service=%s, domain=%s, port=%d\n", service, domain, port);
	#if 1
	sprintf(sendbuf, "POST %s HTTP/1.1\r\n"
					"Host: %s:%d\r\n"
					"Content-Type: application/json;charset=utf-8\r\n"
					"Content-Length: %d\r\n"
					"\r\n",
					service, domain, port, len);
	#else
	sprintf(sendbuf,"Content-Type: application/json;charset=utf-8\r\n");
	#endif
#else
	sprintf(sendbuf, "POST %s HTTP/1.1\r\n"
					/*"Host: %s\r\n"
					"Content-Type: application/json\r\n"*/
					"Host: %s\r\n"
					"Content-Type: application/x-www-form-urlencoded\r\n"
					"Content-Length: %d\r\n"
					"Accept: */*\r\n"
					"Cache-Control: no-cache\r\n"
					"\r\n",
					service, domain, /*port,*/ len);
#endif

	strcat(sendbuf, req);
	printf("http send:[%s]\n",sendbuf);


	ret = send(sockfd, sendbuf, strlen(sendbuf), 0);
	if (ret < 0)
	{
		printf("ERROR:send error:%s\n", strerror(errno));
		close(sockfd);
		return -10;
	}else{
		printf("INFO:send [%s] success!\n", sendbuf);
	}

	while(bneedreply)
	{
	#if 0
		int retry_count = 0;
		int RETRY_LIMIT = 10;
		ret = recv(sockfd, recvbuf, sizeof(recvbuf), 0);
		while((ret < 0) && (retry_count < RETRY_LIMIT)){
			++retry_count;
			printf("INFO:retry for %d times\n", retry_count);
			ret = recv(sockfd, recvbuf, sizeof(recvbuf), 0);
			usleep(1000 * 100);
		}
		if(ret > 0){
			printf("INFO:recvbuf is [%s]\n", recvbuf);
		}
	#endif
	#if 1
		fd_set	rdfds;
		struct timeval tv;
		// ³¬³öBuffer(ÐèÒª°üÀš1×ÖœÚ'\0')
		if (offset + 1 >= sizeof(recvbuf))
		{
			printf("Buffer overflow! offset: %d, Recv:\n%s\n", offset, recvbuf);
			close(sockfd);
			return -11;
		}
		#if 1
		FD_ZERO(&rdfds);
		FD_SET(sockfd, &rdfds);
		tv.tv_sec = 10; //gdcfaceinfo.dcfacehb.hbinterval
		tv.tv_usec = 0;
		ret = select(sockfd + 1, &rdfds, NULL, NULL, &tv);
		//printf("select ret %d\n", ret);
		if(ret < 0){
			perror("select");
			//close(sockfd);
			//sockfd = -1;
		}
		else if(ret == 0){
			printf("%s send_cmd_heartbeat\n", __func__);
			continue;
		}else{
			//printf("INFO:socket is ready to be read!\n");
			if(FD_ISSET(sockfd, &rdfds))
			{
				memset(recvbuf, '\0', sizeof(recvbuf));
				ret = recv(sockfd, recvbuf, sizeof(recvbuf), 0);
				if(ret > 0)
				{
					recvbuf[ret] = '\0';
					printf("INFO:[%s] recv:[%s]\n", __func__, recvbuf);
					//jv_tcp_server_cmd_prase(recvbuf, sockfd);
				}

			}
		}
		#endif
		#if 0
		ret = recv(sockfd, recvbuf+offset, sizeof(recvbuf)-offset-1, 0);
		if (ret == -1)
		{
			printf("error: recv failed : %s\n", strerror(errno));
			close(sockfd);
			//return -12;
		}
		else if (ret == 0)
		{
			printf("socket is closed\n");
			close(sockfd);
			return -13;
		}
		#endif
	#endif
		offset += ret;
		recvbuf[offset] = '\0';
		printf("recv:\n%s\n", recvbuf);
	#if 0
		if(strstr(recvbuf, "Content-Length:") != NULL)
		{
			if (contentLen == 0)
			{
				contentLen = __get_content_length(recvbuf);
			}
			if (contentLen == 0 || contentLen <= offset)
			{
				if(contentLen > 0)
				{
					char *httpBody = strstr(recvbuf, "\r\n\r\n");
					if(httpBody == NULL)
					{
						printf("http resp incorrect\n");
						close(sockfd);
						return -14;
					}
					strcpy(resp, httpBody+4);
				}
				break;
			}
		}
		else if(strstr(recvbuf, "Transfer-Encoding: chunked") != NULL)
		{
			bChunked = 1;
			char *httpBody = strstr(recvbuf, "\r\n\r\n");
			//printf("httpBody:\n%s\n", httpBody);
			offset = 0;
			if(httpBody != NULL)
			{
				char size[8] = {0};
				char *bodySize = httpBody+4;
				char *bodyStart = strstr(bodySize, "\r\n");
				if(bodyStart != NULL)
				{
					//printf("bodySize:\n%s\n", bodySize);
					strncpy(size, bodySize, (int)(bodyStart-bodySize));
					sscanf(size,"%x",&chunkLen);
					//printf("chunkLen:%d\n", chunkLen);

					bodyStart+=2;
					//printf("bodyStart:\n%s\n", bodyStart);
					if (contentLen + chunkLen + 1 >= BUFSIZE_RECV)
					{
						printf("recvbuf:\n%s\n", recvbuf);
						printf("Chunked over flow0! contentLen:%d, chunkLen: %d\n", contentLen, chunkLen);
						close(sockfd);
						return -15;
					}
					strncpy(resp+contentLen, bodyStart, chunkLen);

					contentLen += chunkLen;
					//printf("contentLen:%d\n", contentLen);
					if(strstr(bodyStart, "0\r\n\r\n") != NULL)//recvd all the chunks
					{
						//printf("1resp:\n%s\n", resp);
						break;
					}
				}
				else
				{
					printf("1HTTP resp incorrect!\n");
					close(sockfd);
					return -16;
				}
			}
			else
			{
				printf("2HTTP resp incorrect!\n");
				close(sockfd);
				return -17;
			}
		}
		else
		{
			if(bChunked == 1)
			{
				char *end = strstr(recvbuf, "0\r\n\r\n");
				if(end != NULL)//recvd all the chunks
				{
					if (contentLen + (end - recvbuf) + 1 >= BUFSIZE_RECV)
					{
						printf("recvbuf:\n%s\n", recvbuf);
						printf("Chunked over flow1! contentLen:%d, chunkLen: %d\n", contentLen, end-recvbuf);
						close(sockfd);
						return -18;
					}
					strncpy(resp+contentLen, recvbuf, end-recvbuf);
					//printf("2resp:\n%s\n", resp);
					break;
				}
				else
				{
					if (contentLen + chunkLen + 1 >= BUFSIZE_RECV)
					{
						printf("recvbuf:\n%s\n", recvbuf);
						printf("Chunked over flow2! contentLen:%d, chunkLen: %d\n", contentLen, ret);
						close(sockfd);
						return -19;
					}
					strncpy(resp+contentLen, recvbuf, ret);
				}
			}
			else
			{
				printf("3HTTP resp incorrect!\n");
				close(sockfd);
				return -20;
			}
		}
	}
	close(sockfd);
	return contentLen;
	#endif
}
}
#endif
//////////////////////////////httpclient.c   œáÊø///////////////////////////////////////////


#define HTTP_BUF_MAX_SIZE (4*1024)

static char http_header[] =
	"POST /snappic HTTP/1.1\r\n"
	"Host: %s:%d\r\n"
	"Connection: keep-alive\r\n"
	"Content-Type: multipart/form-data; boundary=%s\r\n"
	"Content-Length: %d\r\n\r\n"
	"%s";

static char disposition_str[] =
	"--%s\r\n"
	"Content-Disposition: form-data; name=\"imgFile\"; filename=\"%s\"\r\n"
	"Content-Type: image/jpeg\r\n"
	"Content-Transfer-Encoding: binary\r\n\r\n";


int JV_http_push_snappic(char *srvIP, int srvPort, char *localfile, char *remotefile)
{
	int sockfd = -1;
	struct sockaddr_in addr;
	char disposition[4096];
	char send_end_data[128] = {0};
	char buf[HTTP_BUF_MAX_SIZE];
	FILE *fp;
	char boundary[64] = {0};
	int ret = -1, content_length = -1;
	long long int timestamp;
	struct timeval tv;
	struct timeval timeout = {3, 0};

	if (srvIP == NULL || srvPort <= 0 )
	{
		return -1;
	}

	if (access(localfile, F_OK) != 0)
	{
		printf("file %s not exsit!\n", localfile);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(srvIP);
	addr.sin_port = htons(srvPort);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == sockfd)
	{
		return -1;
	}

	setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(struct timeval));
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(struct timeval));

	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		printf("conect face server failed\n");
		close(sockfd);
		return -1;
	}

	fp = fopen(localfile, "rb");
	if (fp == NULL)
	{
		printf("open file %s error!\n", localfile);
		close(sockfd);
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	content_length = ftell(fp);
	rewind(fp);

	gettimeofday(&tv, NULL);
	timestamp = (long long int)tv.tv_sec * 1000 + tv.tv_usec;
	snprintf(boundary, sizeof(boundary), "====WebKitFormBoundary%lld", timestamp);

	content_length += snprintf(disposition, sizeof(disposition), disposition_str,
						boundary, remotefile);

	content_length += snprintf(send_end_data, sizeof(send_end_data),"\r\n--%s--\r\n", boundary);

	//printf("disposition=[%s]\n", disposition);
	//printf("send_end=[%s]\n", send_end_data);

	ret = snprintf(buf, HTTP_BUF_MAX_SIZE, http_header, srvIP, srvPort, boundary, content_length, disposition);

	//printf("send_buf=[%s]\n", buf);

	if (send(sockfd, buf, ret, 0) != ret)
	{
		printf("send head error!\n");
		close(sockfd);
		fclose(fp);
		return -1;
	}

	clearerr(fp);

	while (1)
	{
		ret = fread(buf, 1, 4096, fp);
		if(ret != 4096)
		{
			if (!ferror(fp))
			{
				if (send(sockfd, buf, ret, 0) != ret)
				{
					printf("send the end date error!\n");
					close(sockfd);
					fclose(fp);
					return -1;
				}

				fclose(fp);
				break;
			}
			else
			{
				printf("read file error!\n");
				close(sockfd);
				fclose(fp);
				return -1;
			}
		}

		if (send(sockfd, buf, 4096, 0) != 4096)
		{
			printf("send date error\n");
			close(sockfd);
			fclose(fp);
			return -1;
		}
	}

	if (send(sockfd, send_end_data, strlen(send_end_data), 0) != strlen(send_end_data))
	{
		close(sockfd);
		return -1;
	}

	//printf("send to server end date:%s\n", send_end_data);

	memset(buf, 0, HTTP_BUF_MAX_SIZE);
	if (recv(sockfd, buf, HTTP_BUF_MAX_SIZE, 0) < 0)
	{
		printf("recv error!\n");
	}

	//printf("recv:%s\n", buf);
	close(sockfd);
	return 0;
}


