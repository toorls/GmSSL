﻿
/*
 *  Copyright 2014-2023 The GmSSL Project. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the License); you may
 *  not use this file except in compliance with the License.
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 */


#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <gmssl/socket.h>
#include <gmssl/http.h>
#include <gmssl/error.h>


int http_parse_uri(const char *uri, char host[128], int *port, char path[256])
{
	if (!uri || !host || !port || !path) {
		error_print();
		return -1;
	}

	*host = 0;
	*port = 80;
	*path++ = '/';
	*path = 0;

	if (sscanf(uri, "http://%127[^:]:%i/%254[^\n]", host, port, path) == 3);
	else if (sscanf(uri, "http://%127[^/]/%254[^\n]", host, path) == 2);
	else if (sscanf(uri, "http://%127[^:]:%i[^/][^\n]", host, port) == 2);
	else if (sscanf(uri, "http://%127[^/][^\n]", host) == 1);
	else {
		error_print();
		return -1;
	}
	if (!host[0] || strchr(host, '/') || strchr(host, ':')) {
		error_print();
		return -1;
	}
	if (*port <= 0) {
		error_print();
		return -1;
	}

	return 1;
}

static int socket_recv_all(int sock, uint8_t *buf, size_t len)
{
	size_t n;
	while (len) {
		if ((n = recv(sock, buf, len, 0)) <= 0) {
			error_print();
			return -1;
		}
		buf += n;
		len -= n;
	}
	return 1;
}

int http_parse_response(uint8_t *buf, size_t buflen, uint8_t **content, size_t *contentlen, size_t *left)
{
	char *ok = "HTTP/1.1 200 OK\r\n";
	char *p;
	size_t headerlen;

	if (buflen < strlen(ok) || memcmp(buf, ok, strlen(ok)) != 0) {
		error_print();
		return -1;
	}
	if (!(p = strnstr((char *)buf, "\r\n\r\n", buflen))) {
		error_print();
		return -1;
	}
	*content = (uint8_t *)(p + 4);
	headerlen = *content - buf;

	if (!(p = strnstr((char *)buf, "\r\nContent-Length: ", headerlen))) {
		error_print();
		return -1;
	}
	p += strlen("\r\nContent-Length: ");
	*contentlen = atoi(p);
	if (*contentlen <= 0 || *contentlen > INT_MAX) {
		error_print();
		return -1;
	}

	buflen -= headerlen;
	if (buflen < *contentlen)
		*left = *contentlen - buflen;
	else	*left = 0;

	return 1;
}


#define HTTP_GET_TEMPLATE "GET %s HTTP/1.1\r\n" "Host: %s\r\n" "\r\n\r\n"

int http_get(const char *uri, uint8_t *buf, size_t buflen,
	uint8_t **content, size_t *contentlen)
{
	char *uribuf = NULL;
	char host[128];
	int port;
	char path[256];
	struct hostent *hp;
	struct sockaddr_in server;
	tls_socket_t sock;
	char get[sizeof(HTTP_GET_TEMPLATE) + sizeof(host) + sizeof(path)];
	int getlen;
	size_t len;
	size_t left;

	// parse uri
	if (http_parse_uri(uri, host, &port, path) != 1) {
		error_print();
		return -1;
	}

	// connect
	if (!(hp = gethostbyname(host))) {
		error_print();
		return -1;
	}
	server.sin_addr = *((struct in_addr *)hp->h_addr_list[0]);
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		error_print();
		return -1;
	}
	if (connect(sock, (struct sockaddr *)&server , sizeof(server)) < 0) {
		error_print();
		return -1;
	}

	// request
	if ((getlen = snprintf(get, sizeof(get),
		"GET %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"\r\n\r\n", path, host)) <= 0) {
		error_print();
		return -1;
	}
	if (send(sock, get, strlen(get), 0) != getlen) {
		error_print();
		return -1;
	}

	// response
	if ((len = recv(sock, buf, buflen, 0)) <= 0) {
		error_print();
		return -1;
	}
	if (http_parse_response(buf, len, content, contentlen, &left) != 1) {
		error_print();
		return -1;
	}
	if (left) {
		if (len + left > buflen) {
			error_print(); // buf is not enough
			return -1;
		}
		if (socket_recv_all(sock, buf + len, left) != 1) {
			error_print();
			return -1;
		}
	}

	return 1;
}

