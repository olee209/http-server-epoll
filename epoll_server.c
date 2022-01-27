#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include "epoll_server.h"

#define MAXSIZE 2000

void epoll_run(int port)
{
	int i = 0;

	// 创建一个epoll树的根节点
	int epfd = epoll_create(MAXSIZE);
	if (epfd == -1) { 
		perror("epoll_create error");
		exit(1);
	}

	// 创建lfd，并添加至监听树
	int lfd = init_listen_fd(port, epfd);

	struct epoll_event all_events[MAXSIZE];
	while (1) {
		// 监听节点对应事件
		int ret = epoll_wait(epfd, all_events, MAXSIZE, -1);
		if (ret == -1) {      
			perror("epoll_wait error");
			exit(1);
		}

		for (i=0; i<ret; ++i) {

			// 只处理读事件, 其他事件默认不处理
			struct epoll_event *pev = &all_events[i];

			// 不是读事件
			if (!(pev->events & EPOLLIN)) {                     
				continue;
			}
			if (pev->data.fd == lfd) {   	// 接受连接请求   

				do_accept(lfd, epfd);

			} else {			// 读数据
				do_read(pev->data.fd, epfd);
			}
		}
	}
}


// 读数据
void do_read(int cfd, int epfd)
{
	// 读取一行http协议， 拆分， 获取 get 文件名 协议号
	char line[1024] = {0};
	int len = get_line(cfd, line, sizeof(line));	// 读请求行
	if(len==-1){
		perror("get_line error");
		exit(1);
	} else if (len==0) {
		printf("客户端断开了连接...\n");
		disconnect(cfd, epfd);
	} else {
		printf("============= 请求头 ============\n");   
		printf("请求行数据: %s", line);
		// 还有数据没读完,继续读走
		while(1) {
			char buf[1024] = {0};
			len = get_line(cfd, buf, sizeof(buf));
			if (buf[0] == '\n') {
				break;	
			} else if (len == -1)
				break;
			//printf("----------%s\n", buf);
			//sleep(1);
		}	

		if(strncasecmp("GET", line, 3)==0){
			http_request(cfd, line);
			disconnect(cfd, epfd);
		} 
	}

}


void http_request(int cfd, const char* request) {
	char method[12], path[256], protocol[12];
	sscanf(request, "%[^ ] %[^ ] %[^ ]", method, path, protocol);
	//printf("method=%s, path=%s, protocl=%s\n", method, path, protocol);

    	decode_str(path, path);

	char* file = path+1;

	if (strcmp(path, "/") == 0) {
		file = "./";
	}

	struct stat sbuf;
	int ret = stat(file, &sbuf);
	if (ret!=0) {
		//perror("stat error");
		send_error(cfd, 404, "Not Found", "NO such file or direntry");   
		return;
	}

	if (S_ISDIR(sbuf.st_mode)) {
		send_respond_head(cfd, 200, "OK", get_file_type(".html"), -1);
		send_dir(cfd, file);
	}else if (S_ISREG(sbuf.st_mode)) {
		send_respond_head(cfd, 200, "OK", get_file_type(file), sbuf.st_size);
		//send_respond_head(cfd, 200, "OK", "text/plain; charset=utf-8", sbuf.st_size);
		//send_respond_head(cfd, 200, "OK", "audio/mpeg", -1);
		send_file(cfd, file);
	}

}

void send_dir(int cfd, const char* dirname) {
	char buf[4096] = {0};

	sprintf(buf,"<html><head><title>目录名: %s</title></head>", dirname);
	sprintf(buf+strlen(buf), "<body><h1>当前目录: %s</h1><table>", dirname);

	char enstr[1024] = {0};
	char path[1024] = {0};

	struct dirent** ptr; //
	int num = scandir(dirname, &ptr, NULL, alphasort);

	for (int i = 0; i < num; i++) {
		char* name = ptr[i]->d_name;

		sprintf(path, "%s%s", dirname, name);
		printf("path = %s -------\n", path);

		struct stat sbuf;
		stat(path, &sbuf);

		encode_str(enstr, sizeof(enstr), name);

		// 如果是文件
		if(S_ISREG(sbuf.st_mode)) {       
			sprintf(buf+strlen(buf), 
					"<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
					enstr, name, (long)sbuf.st_size);
		} else if(S_ISDIR(sbuf.st_mode)) {		// 如果是目录       
			sprintf(buf+strlen(buf), 
					"<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>",
					enstr, name, (long)sbuf.st_size);
		}
		int ret = send(cfd, buf, strlen(buf), 0);
		if (ret == -1) {
			if (errno == EAGAIN) {
				perror("send error:");
				continue;
			} else if (errno == EINTR) {
				perror("send error:");
				continue;
			} else if (errno == ECONNRESET) {
				perror("send error:");
				continue;
			} else {
				perror("send error:");
				exit(1);
			}
		}
		memset(buf, 0, sizeof(buf));
		// 字符串拼接
	}

	sprintf(buf+strlen(buf), "</table></body></html>");
	send(cfd, buf, strlen(buf), 0);

	printf("dir message send OK!!!!\n");
#if 0
	// 打开目录
	DIR* dir = opendir(dirname);
	if(dir == NULL)
	{
		perror("opendir error");
		exit(1);
	}

	// 读目录
	struct dirent* ptr = NULL;
	while( (ptr = readdir(dir)) != NULL )
	{
		char* name = ptr->d_name;
	}
	closedir(dir);
#endif
}

void send_error(int cfd, int status, const char* title, const char* text) {
	char buf[4096] = {0};

	sprintf(buf, "%s %d %s\r\n", "HTTP/1.1", status, title);
	sprintf(buf+strlen(buf), "Content-Type:%s\r\n", "text/html");
	sprintf(buf+strlen(buf), "Content-Length:%d\r\n", -1);
	sprintf(buf+strlen(buf), "Connection: close\r\n");
	send(cfd, buf, strlen(buf), 0);
	send(cfd, "\r\n", 2, 0);

	memset(buf, 0, sizeof(buf));

	sprintf(buf, "<html><head><title>%d %s</title></head>\n", status, title);
	sprintf(buf+strlen(buf), "<body><div align=center><h1>%d %s</h1></div>\n", status, title);
	sprintf(buf+strlen(buf), "%s\n", text);
	sprintf(buf+strlen(buf), "<hr>\n</body>\n</html>\n");
	send(cfd, buf, strlen(buf), 0);

	return;
}

void send_file(int cfd, const char* filename) {
	int fd = open(filename, O_RDONLY);
	if(fd==-1){
		//perror("open error");
		send_error(cfd, 404, "Not Found", "NO such file or direntry");   
		exit(1); //
	}

	char buf[4096] = {0};
	int n = 0;
	while((n = read(fd, buf, sizeof(buf)))>0){
		int ret = send(cfd, buf, n, 0);
		if(ret==-1) {
			//printf("-----------: %d", errno);
			if (errno == EAGAIN) { 
				//perror("send error:");
				continue;
			} else if (errno == EINTR) {
				//perror("send error:");
				continue;
			} else {
				perror("send error:");
				exit(1);
			}
		}
	}
	if(n==-1) {
		perror("read error");
		exit(1);
	}

	close(fd);
}

void send_respond_head(int cfd, int no, const char* desc, const char* type, long len) {
	char buf[1024] = {0};
	sprintf(buf, "HTTP/1.1 %d %s\r\n", no, desc);
	sprintf(buf+strlen(buf), "Content-Type: %s\r\n", type);
	sprintf(buf+strlen(buf), "Content-Length: %ld\r\n", len);
	send(cfd, buf, strlen(buf), 0);

	send(cfd, "\r\n", 2, 0);
}

// 断开连接的函数
void disconnect(int cfd, int epfd)
{
	int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
	if(ret == -1) {   
		perror("epoll_ctl del cfd error");
		exit(1);
	}
	close(cfd);
}


// 解析http请求消息的每一行内容
int get_line(int sock, char *buf, int size)
{
	int i = 0;
	char c = '\0';
	int n;
	while ((i < size - 1) && (c != '\n')) {    
		n = recv(sock, &c, 1, 0);
		if (n > 0) {        
			if (c == '\r') {            
				n = recv(sock, &c, 1, MSG_PEEK);
				if ((n > 0) && (c == '\n')) {               
					recv(sock, &c, 1, 0);
				} else {                            
					c = '\n';
				}
			}
			buf[i] = c;
			i++;
		} else {       
			c = '\n';
		}
	}
	buf[i] = '\0';

	return i;
}

// 接受新连接处理
void do_accept(int lfd, int epfd) {

	struct sockaddr_in clt_addr;
	socklen_t clt_addr_len = sizeof(clt_addr);

	int cfd = accept(lfd, (struct sockaddr*)&clt_addr, &clt_addr_len);
	if (cfd == -1) {   
		perror("accept error");
		exit(1);
	}

	// 打印客户端IP+port
	char client_ip[64] = {0};
	printf("New Client IP: %s, Port: %d, cfd = %d\n",
			inet_ntop(AF_INET, &clt_addr.sin_addr.s_addr, client_ip, sizeof(client_ip)),
			ntohs(clt_addr.sin_port), cfd);

	// 设置 cfd 非阻塞
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);

	// 将新节点cfd 挂到 epoll 监听树上
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLET;  //ET模式
	ev.data.fd = cfd;

	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
	if (ret == -1)  {
		perror("epoll_ctl add cfd error");
		exit(1);
	}
}

int init_listen_fd(int port, int epfd) {

	//　创建监听的套接字 lfd
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == -1) {    
		perror("socket error");
		exit(1);
	}
	// 创建服务器地址结构 IP+port
	struct sockaddr_in srv_addr;
	bzero(&srv_addr, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(port);
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	// 端口复用
	int opt = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	// 给 lfd 绑定地址结构
	int ret = bind(lfd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
	if (ret == -1) {   
		perror("bind error");
		exit(1);
	}
	// 设置监听上限
	ret = listen(lfd, 128);
	if (ret == -1) { 
		perror("listen error");
		exit(1);
	}

	// 把lfd挂上树
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = lfd;

	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
	if (ret == -1) { 
		perror("epoll_ctl add lfd error");
		exit(1);
	}

	return lfd;
}


// 通过文件名获取文件的类型
const char *get_file_type(const char *name)
{
	char* dot;

	// 自右向左查找‘.’字符, 如不存在返回NULL
	dot = strrchr(name, '.');   
	if (dot == NULL)
		return "text/plain; charset=utf-8";
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".css") == 0)
		return "text/css";
	if (strcmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcmp( dot, ".wav" ) == 0)
		return "audio/wav";
	if (strcmp(dot, ".avi") == 0)
		return "video/x-msvideo";
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcmp(dot, ".ogg") == 0)
		return "application/ogg";
	if (strcmp(dot, ".pac") == 0)
		return "application/x-ns-proxy-autoconfig";

	return "text/plain; charset=utf-8";
}

int hexit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}

void encode_str(char* to, int tosize, const char* from)
{
	int tolen;

	for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) {    
		if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) {      
			*to = *from;
			++to;
			++tolen;
		} else {
			sprintf(to, "%%%02x", (int) *from & 0xff);
			to += 3;
			tolen += 3;
		}
	}
	*to = '\0';
}

void decode_str(char *to, char *from)
{
	for ( ; *from != '\0'; ++to, ++from  ) {     
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {       
			*to = hexit(from[1])*16 + hexit(from[2]);
			from += 2;                      
		} else {
			*to = *from;
		}
	}
	*to = '\0';
}

