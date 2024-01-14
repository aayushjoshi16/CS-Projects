#ifndef HEADER_H
#define HEADER_H

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <stddef.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>

extern pthread_mutex_t udp_socket_lock;

typedef struct SockInfo {
  int sock_id;
  char *fname;
  char operation;
} SockInfo;

typedef struct kvInfo {
  int sock_id;
  char *key;
  char *value;
  char *server_path;
} kvInfo;


char *get_value(char *buff);


void *curl_operations(void *args);
void *kv_operations(void *args);

SockInfo *make_sockinfo(int sock_id, char *buff, char operation);
kvInfo *make_kvinfo(int sock_id, char *buff, char operation, char *server_path);

void report_400(int sock_id);
void report_403(int sock_id);
void report_404(int sock_id);
void report_500(int sock_id);
void report_501(int sock_id);

#endif
