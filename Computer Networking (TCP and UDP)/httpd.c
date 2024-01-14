#include "header.h"

#define DEFAULT_BACKLOG 100
#define MAXLEN 1000


// Global variable used to block the UDP socket when in use
pthread_mutex_t udp_socket_lock = PTHREAD_MUTEX_INITIALIZER;



int main(int argc, char *argv[]) {

  // Set up TCP communication
  int sock_fd, newsock, port = atoi(argv[2]);
  struct sockaddr_in sa, newsockinfo, peerinfo;
  socklen_t len;
  
  char localaddr[INET_ADDRSTRLEN], peeraddr[INET_ADDRSTRLEN], buff[MAXLEN+1];
  
  sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = htonl(INADDR_ANY);
  
  bind(sock_fd, (struct sockaddr *) &sa, sizeof(sa));  
  listen(sock_fd, DEFAULT_BACKLOG);

  
  // Infinite loop to keep receiving requests  
  for (;;) {
    len = sizeof(newsockinfo); 
    newsock = accept(sock_fd, (struct sockaddr *) &peerinfo, &len);
    
    len = sizeof(newsockinfo);
    getsockname(newsock, (struct sockaddr *) &newsockinfo, &len);
    
    inet_ntop(AF_INET, &newsockinfo.sin_addr.s_addr, localaddr, sizeof(localaddr));
    inet_ntop(AF_INET, &peerinfo.sin_addr.s_addr, peeraddr, sizeof(peeraddr));
    
    int mlen = recv(newsock, buff, sizeof(buff), 0);
    if (mlen < 0) {
      report_500(newsock);
      break;
    }
    
    
    // Each request is taken over by individual thread to have main thread always listening
    // Each thread are ditached form the main thread; thread responsible for their own clean-up
    pthread_t tid;

    
    // HEAD REQUEST
    if (strncmp(buff, "HEAD", 4) == 0) {

      // Report error if HEAD request is send for kvstore
      if (buff[6] == 'k' && buff[7] == 'v' && buff[8] == '/') {

	// Seperate funtions are made to reduce redundancies for reporting
	// appropriate error messages to the client
	report_400(newsock);
      }

      // Continue HEAD request for regular file in the database
      else {

	// This sets up a struct which hold necessary information for executing the request
	SockInfo *tsock = make_sockinfo(newsock, buff, 'h');
	if (tsock == NULL) {
	  report_500(newsock);
	}

	else {
	  if (pthread_create(&tid, NULL, curl_operations, (void *) tsock) != 0) {
	    report_500(newsock);
	  }	  
	}
      }      
    }
    
    // GET REQUEST
    else if (strncmp(buff, "GET", 3) == 0) {

      // Curl to get from kvstore()
      if (buff[5] == 'k' && buff[6] == 'v' && buff[7] == '/') {

	// This sets up a struct which hold necessary information for executing the request
	kvInfo *tsock = make_kvinfo(newsock, buff, 'g', argv[1]);
	if (tsock == NULL) {
	  report_500(newsock);
	}

	else {
	  if (pthread_create(&tid, NULL, kv_operations, (void *) tsock) != 0) {
	    report_500(newsock);
	  }
	}
      }

      // Curl to get contents of file in the database
      else {

	SockInfo *tsock = make_sockinfo(newsock, buff, 'g');
	if (tsock == NULL) {
	  report_500(newsock);
	}

	else {
	  if (pthread_create(&tid, NULL, curl_operations, (void *) tsock) != 0) {
	    report_500(newsock);
	  }
	}
      }
    }

    // PUT REQUEST
    else if (strncmp(buff, "PUT", 3) == 0) {

      // PUT request for kvstore
      if (buff[5] == 'k' && buff[6] == 'v' && buff[7] == '/') {

	kvInfo *tsock = make_kvinfo(newsock, buff,  's', argv[1]);
	if (tsock == NULL) {
	  report_500(newsock);
	}

	else {
	  if (pthread_create(&tid, NULL, kv_operations, (void*) tsock) != 0) {
	    report_500(newsock);
	  }
	}
      }

      // Report error if PUT request is for a file in the database
      else {
        report_403(newsock);
      }
    }

    // All other functionalities will report Not Implemented
    else {
      report_501(newsock);
    }
  }  

  
  // Ending ceremony
  pthread_mutex_destroy(&udp_socket_lock);
  
  shutdown(sock_fd, SHUT_RDWR);
  close(sock_fd);
  
  return 0;
}


// NOTE : This program receives some valgrind errors related to pthread_create()
//        None of the errors are realted to memory leaks
