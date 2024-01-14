#include "header.h"


char *get_fname(char *buff);
char *get_key(char *buff);
char *parse_path(char *path);
char *get_value(char *buff);

void curl_terminate(SockInfo *tsock);
void kv_terminate(int socket_fd, char *client_sock_name, kvInfo *tsock);


// Objective : The thread will handle curl requests : HEAD and GET

void *curl_operations(void *args) {

  // SockInfo contains all the necessary information
  SockInfo *tsock = (SockInfo *)args;
  char message[1025] = "";
  int mlen;
  
  // If file does not exist
  if (access(tsock -> fname, F_OK) != 0) {

    // Function used to send 404 error message to client
    report_404(tsock -> sock_id);

    // Performs all the tasks required for error-free exiting
    curl_terminate(tsock);
  }
  
  
  // Get file size
  struct stat st;
  int size = 0;

  // Store size to display in header message
  if (stat(tsock -> fname, &st) == 0) {
    size = st.st_size;
  }
  else {
    report_500(tsock -> sock_id);
    curl_terminate(tsock);
  }
  
  // Send file header
  snprintf(message, sizeof(message), "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n", size);

  // Sending header
  mlen = send(tsock -> sock_id, message, strlen(message), 0);
  if (mlen < 0) {
    report_500(tsock -> sock_id);
    curl_terminate(tsock);
  }

  // Programs continues into opening and sending file content if GET is requested
  if (tsock -> operation == 'g') {

    // Open file for reading
    FILE *fp = fopen(tsock -> fname, "r");
    char c[1] = "";

    // Formatting output to write content
    c[0] = '\r';
    mlen = send(tsock -> sock_id, c, sizeof(char), 0);
    c[0] = '\n';
    mlen = send(tsock -> sock_id, c, sizeof(char), 0);

    // Reading and sending
    while ((c[0] = fgetc(fp)) != EOF) {
      mlen = send(tsock -> sock_id, c, sizeof(char), 0);

      // Report error if unable to send
      if (mlen < 0) {
	report_500(tsock -> sock_id);
	curl_terminate(tsock);
      }
    }

    fclose(fp);
  }

  // Closing tasks
  curl_terminate(tsock);

  // Kept here to avoid "reached non-void function" warning
  pthread_exit(NULL);
}


// Terminates the thread after freeing all the memory and closing other

void curl_terminate(SockInfo *tsock) {
  pthread_detach(pthread_self());
  
  shutdown(tsock -> sock_id, SHUT_RDWR);
  close(tsock -> sock_id);
  free(tsock -> fname);
  free(tsock);
  pthread_exit(NULL);
}


// Objective : Responsible for handling all kv_ requests : GET and PUT

void *kv_operations(void *args) {

  // Locking socket for other threads until the current one exits
  pthread_mutex_lock(&udp_socket_lock);
  
  kvInfo *tsock = (kvInfo *)args;
  char message[1025] = "";
  
  // Set up UDP connection
  int socket_fd;
  struct sockaddr_un server_address;
  struct sockaddr_un client_address;
  size_t bytes_received, bytes_sent;
  socklen_t address_length = sizeof(struct sockaddr_un);
  
  // Used to read value returned by kvstore.c in case get is requested
  char buffer[1025];

  // Server of kvstore.c
  char *server_sock_name = tsock -> server_path;

  // Parsing through and keeping the client socket in the same directory as the
  // server socket
  char *client_sock_name = parse_path(tsock -> server_path);
  
  if (client_sock_name == NULL) {
    // Reporting internal error
    report_500(tsock -> sock_id);

    // Performs all necessary tasks for error-free exi
    kv_terminate(socket_fd, client_sock_name, tsock);
  }

  // Set up socket
  if ((socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
    report_500(tsock -> sock_id);
    kv_terminate(socket_fd, client_sock_name, tsock);
  }
  
  memset(&client_address, 0, sizeof(struct sockaddr_un));
  client_address.sun_family = AF_UNIX;
  strcpy(client_address.sun_path, client_sock_name);
  
  
  // Bind client socket to name (server will discover this name via recvfrom)
  unlink(client_sock_name);
  if (bind(socket_fd, (const struct sockaddr *) &client_address, sizeof(struct sockaddr_un)) < 0) {
    report_500(tsock -> sock_id);
    kv_terminate(socket_fd, client_sock_name, tsock);
  }
  
  memset(&server_address, 0, sizeof(struct sockaddr_un));
  server_address.sun_family = AF_UNIX;
  strcpy(server_address.sun_path, server_sock_name);

  
  // OPERATION
  char operation[4] = "get";

  // tsock -> value set to NULL is get is requested
  if (tsock -> value) {
    operation[0] = 's';
  }

  // Send operation
  bytes_sent = sendto(socket_fd, (char *) &operation, strlen(operation), 0,
		      (struct sockaddr *) &(server_address),
		      sizeof(struct sockaddr_un));

  // Error if unable to send
  if (bytes_sent <= 0) {
    report_500(tsock -> sock_id);
    kv_terminate(socket_fd, client_sock_name, tsock);
  }

  
  // KEY
  bytes_sent = sendto(socket_fd, (char *) tsock -> key, strlen(tsock -> key), 0,
		      (struct sockaddr *) &(server_address),
		      sizeof(struct sockaddr_un));
  
  if (bytes_sent <= 0) {
    report_500(tsock -> sock_id);
    kv_terminate(socket_fd, client_sock_name, tsock);
  } 
  

  // PUT
  if (tsock -> value) {

    // Send value
    bytes_sent = sendto(socket_fd, (char *) tsock -> value, strlen(tsock -> value), 0,
			(struct sockaddr *) &(server_address),
			sizeof(struct sockaddr_un));
    
    if (bytes_sent <= 0) {
      report_500(tsock -> sock_id);
      kv_terminate(socket_fd, client_sock_name, tsock);
    }

    // Send OK message and exit
    const char *val_message = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    int mlen = send(tsock -> sock_id, val_message, strlen(val_message), 0);

    if (mlen < 0) {
      report_500(tsock -> sock_id);
      kv_terminate(socket_fd, client_sock_name, tsock);
    }    
  }
  
  // GET
  else {
    
    // Clearing buffer to read value from server
    memset(buffer, '\0', sizeof(buffer));

    // Receiving from server
    bytes_received = recvfrom(socket_fd, (void *) buffer, sizeof(buffer), 0,
			      (struct sockaddr *) &(server_address),
			      &address_length);

    // Report error if unable to receive
    if (bytes_received <= 0) {
      report_500(tsock -> sock_id);
      kv_terminate(socket_fd, client_sock_name, tsock);
    }

    else {

      // If key not found, then send 404 error
      char *err_check = (char *)malloc(60 * sizeof(char));
      sprintf(err_check, "Key %s does not exist.\n", tsock -> key);
      
      if (strcmp(err_check, buffer) == 0) {
        snprintf(message, sizeof(message), "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", strlen(buffer));
      }

      else {      
	snprintf(message, sizeof(message), "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", strlen(buffer));
      }

      // Send header
      int mlen = send(tsock -> sock_id, message, strlen(message), 0);
      if (mlen < 0) {
	report_500(tsock -> sock_id);
	kv_terminate(socket_fd, client_sock_name, tsock);
      }      

      // Send value corresponding to the key
      mlen = send(tsock -> sock_id, buffer, strlen(buffer), 0);
      if (mlen < 0) {
	report_500(tsock -> sock_id);
	kv_terminate(socket_fd, client_sock_name, tsock);
      }

      free(err_check);
    }    
  }

  // Closing tasks for error-free exit
  kv_terminate(socket_fd, client_sock_name, tsock);

  // Kept here to avoid "reached non-voif function" warning
  pthread_exit(NULL);
}


// Terminates the kv_operations() thread after freeing and closing all requisites

void kv_terminate(int socket_fd, char *client_sock_name, kvInfo *tsock) {
  pthread_detach(pthread_self());
  
  close(socket_fd);
  unlink(client_sock_name);
  
  if (client_sock_name) {
    free(client_sock_name);
  }
  
  free(tsock -> key);
  
  if (tsock -> value) {
    free(tsock -> value);
  }

  shutdown(tsock -> sock_id, SHUT_RDWR);
  close(tsock -> sock_id);

  free(tsock);
  pthread_mutex_unlock(&udp_socket_lock);
  pthread_exit(NULL);
}


// Objective : This function creates SockInfo struct for curl_operations

SockInfo *make_sockinfo(int sock_id, char *buff, char operation) {

  SockInfo *tsock = (SockInfo *) malloc(sizeof(SockInfo));
  if (tsock == NULL) {
    return NULL;
  }  

  // Stores sock_id, the file name and the operation (HEAD or GET) to be performed
  tsock -> sock_id = sock_id;
  tsock -> fname = get_fname(buff);
  if (tsock -> fname == NULL) {
    free(tsock);
    return NULL;
  }
  
  tsock -> operation = operation;
  return tsock;
}


// Objective : This function creates kvInfo struct for kv_operations

kvInfo *make_kvinfo(int sock_id, char *buff, char operation, char *server_path) {

  kvInfo *tsock = (kvInfo *) malloc(sizeof(kvInfo));
  if (tsock == NULL) {
    return NULL;
  }

  // Get key from the buffer
  tsock -> sock_id = sock_id;
  tsock -> key = get_key(buff);
  if (tsock -> key == NULL) {
    free(tsock);
    return NULL;
  }

  // Check and read value form the buffer
  if (operation == 's') {
    tsock -> value = get_value(buff);
    if (tsock -> value == NULL) {
      free(tsock -> key);
      free(tsock);
      return NULL;
    }
  }

  else if (operation == 'g') {
    tsock -> value = NULL;
  }

  else {
    perror("Error : Invalid operation send to make_kvinfo()\n");
    report_500(sock_id);
  }
  
  tsock -> server_path = server_path;
  return tsock;
}


// Objective : This function reads the input (curl) buffer for the file to GET or HEAD

char *get_fname(char *buff) {

  // Allocating enough space for file name
  char *fname = (char *) calloc(1025, sizeof(char));
  if (fname == NULL) {
    return NULL;
  }
  
  int i = 0;

  // Reading position depends on request received
  if (strncmp(buff, "HEAD", 4) == 0) {
    i = 6;
  }
  else if (strncmp(buff, "GET", 3) == 0) {
    i = 5;
  }
  else {
    perror("Error : Invalid command entered\n");
    return NULL;
  }

  // Write to fname and return
  int j = 0;
  while (buff[i] != ' ') {
    fname[j] = buff[i];
    j++;
    i++;
  }

  return fname;
}


// Objective : This function reads the input (curl) buffer and returns the key

char *get_key(char *buff) {

  char *key = (char *) calloc(33, sizeof(char));
  if (key == NULL) {
    return NULL;
  }
  
  int i = 8;
  int j = 0;

  while (buff[i] != ' ') {
    key[j] = buff[i];
    i++;
    j++;
  }

  return key;
}


// Objective : This function parses through the UDP socket path and adds a 'tmp' at the
//             end for client socket name (they must be in the same directory to communicate)

char *parse_path(char *path) {
  
  char *modified_path = malloc(strlen(path) + 1);
  if (modified_path == NULL) {
    return NULL;
  }
  memset(modified_path, 0, strlen(path) + 1);
  
  // Check if memory allocation was successful
  if (!modified_path) {
    return NULL;
  }
  
  // Copy the original path to the new path
  strcpy(modified_path, path);
  
  // Find the last occurrence of '/'
  char *filename = strrchr(modified_path, '/');
  
  // Check if filename is found
  if (filename) {
    // Change the filename to "tmp"
    strcpy(filename + 1, "tmp");
  }
  
  // Return the modified path
  return modified_path;
}


// Objective : This function reads the buffer for the value to be set in kvstore's database

char *get_value(char *buff) {

  // Find Content-Length
  const char *strCmp = "Content-Length: ";
  const char *find_strCmp = strstr(buff, strCmp);
  int value_length = atoi(find_strCmp + strlen(strCmp));
  
  // Find the start of the last line
  const char *value_start = strrchr(buff, '\n');
  if (value_start == NULL) {
    return NULL;
  }
  
  // Allocate memory for the value
  char *value = (char *) malloc((value_length + 1) * sizeof(char));
  if (value == NULL) {
    return NULL;
  }
  
  // Copy the value from the last line
  strncpy(value, value_start + 1, value_length);
  value[value_length] = '\0';
  
  return value;
}



// These function are used to send error messages to the client

void report_400(int sock_id) {
  const char *message = "HTTP/1.1 400 Bad Request\r\n\r\n";
  int mlen = send(sock_id, message, strlen(message), 0);
  
  if (mlen < 0) {
    perror("Error : Unable to send 404 error\n");
  }
}


void report_403(int sock_id) {
  const char *message = "HTTP/1.1 403 Permission Denied\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";
  int mlen = send(sock_id, message, strlen(message), 0);
  
  if (mlen < 0) {
    perror("Error : Unable to send 404 error\n");
  }
}


void report_404(int sock_id) {
  const char *message = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";
  int mlen = send(sock_id, message, strlen(message), 0);
  
  if (mlen < 0) {
    perror("Error : Unable to send 404 error\n");
  }
}


void report_500(int sock_id) {
  const char *message = "HTTP/1.1 500 Internal Error\r\n\r\n";
  int mlen = send(sock_id, message, strlen(message), 0);
  
  if (mlen < 0) {
    perror("Error : Unable to send 404 error\n");
  }
}


void report_501(int sock_id) {
  const char *message = "HTTP/1.1 501 Not Implemented\r\n\r\n";
  int mlen = send(sock_id, message, strlen(message), 0);
  
  if (mlen < 0) {
    perror("Error : Unable to send 404 error\n");
  }
}
