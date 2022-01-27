#ifndef _EPOLL_SERVER_H
#define _EPOLL_SERVER_H

void epoll_run(int port);
void do_accept(int lfd, int epfd);
void do_read(int cfd, int epfd);
void disconnect(int cfd, int epfd);
int get_line(int sock, char *buf, int size);
int init_listen_fd(int port, int epfd);
void http_request(int cfd, const char* request);
void send_respond_head(int cfd, int no, const char* desc, const char* type, long len);
void send_file(int cfd, const char* filename);
const char *get_file_type(const char *name);
void send_error(int cfd, int status, const char* title, const char* text);
void send_dir(int cfd, const char* dirname);
void encode_str(char* to, int tosize, const char* from);
void decode_str(char *to, char *from);

#endif
