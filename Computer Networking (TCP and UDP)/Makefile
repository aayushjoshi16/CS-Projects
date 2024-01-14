CC = gcc
CFLAGS = -pthread -Wall -pedantic -std=c99

httpd: httpd.c helper.c header.h
	$(CC) $(CFLAGS) -o httpd httpd.c helper.c

clean :
	rm -f httpd
