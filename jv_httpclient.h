#ifndef __JV_HTTPCLIENT_H__
#define __JV_HTTPCLIENT_H__

#ifdef PLATFORM_ingenicT20X
#define BUFSIZE_SEND 1024*256
#else
#define BUFSIZE_SEND 4*1024
#endif
#define BUFSIZE_RECV 4*1024

typedef struct __HTTP_T
{
	char camera_code[64];
	char secret_key[64];
	char publish_web[64];
}mhttp_attr_t;

int Http_get_message(char   *argv,mhttp_attr_t *attr) ;

/**
 *@brief 向指定服务地址发送post请求
 *@param url 服务地址
 *@param req 请求数据
 *@param len 请求数据长度
 *@param resp 返回的响应数据，大小应为BUFSIZE大小
 *@param timeout 等待响应超时时间
 *@return 返回的数据的总长度。
 *@note 数据总长度为：HTTP头的长度 + Content-Length所指定的数据长度
 */
int JV_Http_post_message(const char *url, const char *req, int len, char *resp, int timeout, int bneedreply);
int JV_http_push_snappic(char *srvIP, int srvPort, char *localfile, char *remotefile);
int jv_http_download(char *url, char *save_path);
int JV_Http_post_json_message(const char *url, const char *req, int len, char *resp, int timeout, int bneedreply);



#endif

