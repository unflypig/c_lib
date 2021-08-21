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
 *@brief ��ָ�������ַ����post����
 *@param url �����ַ
 *@param req ��������
 *@param len �������ݳ���
 *@param resp ���ص���Ӧ���ݣ���СӦΪBUFSIZE��С
 *@param timeout �ȴ���Ӧ��ʱʱ��
 *@return ���ص����ݵ��ܳ��ȡ�
 *@note �����ܳ���Ϊ��HTTPͷ�ĳ��� + Content-Length��ָ�������ݳ���
 */
int JV_Http_post_message(const char *url, const char *req, int len, char *resp, int timeout, int bneedreply);
int JV_http_push_snappic(char *srvIP, int srvPort, char *localfile, char *remotefile);
int jv_http_download(char *url, char *save_path);
int JV_Http_post_json_message(const char *url, const char *req, int len, char *resp, int timeout, int bneedreply);



#endif

