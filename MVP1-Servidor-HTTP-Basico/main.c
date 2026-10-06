#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "parser.h"

#define MYADDR "127.0.0.1"
#define MYPORT 9998
#define BUFFER_SIZE 4096

void *handle_answer(void *clientfd_v);
int read_file(char *path, char **body, size_t *file_size);
int write_file(char *path, char *body, size_t size, int clientfd, size_t total_len);
int test_file(char *path);

int main (void){
	int doorwayfd;
	struct sockaddr_in my_addr;
	int addrlen = sizeof(my_addr);

	doorwayfd = socket(AF_INET, SOCK_STREAM, 0);
	if(doorwayfd == -1){
		perror("Erro ao criar socket");
		return -1;
	}

	int opt = 1;
	setsockopt(doorwayfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(MYPORT);
	my_addr.sin_addr.s_addr = inet_addr(MYADDR);

	if(bind(doorwayfd, (struct sockaddr*)&my_addr, sizeof(my_addr)) == -1){
		perror("Erro no bind");
		return -1;
	}

	if(listen(doorwayfd, 5) == -1){
		perror("Erro no listen");
		return -1;
	}

	printf("Servidor rodando em http://%s:%d\n", MYADDR, MYPORT);

	while(1){
		int clientfd = accept(doorwayfd, (struct sockaddr*)&my_addr, (socklen_t*)&addrlen);
		if(clientfd == -1) continue;

		pthread_t answer_thread;
		if(pthread_create(&answer_thread, NULL, handle_answer, (void*)(intptr_t)clientfd) == 0){
			pthread_detach(answer_thread);
		} else {
			close(clientfd);
		}
	}

	close(doorwayfd);
	return 0;
}

void *handle_answer(void *clientfd_v){
	int clientfd = (int)(intptr_t)clientfd_v;
	char buffer[BUFFER_SIZE];

	ssize_t bytes_read = read(clientfd, buffer, BUFFER_SIZE-1);
	if (bytes_read <= 0){
		close(clientfd);
		pthread_exit(NULL);
	}
	buffer[bytes_read] = '\0';

	struct message *message = parse_message(buffer, bytes_read);
	if(!message || !message->start_line){
		const char *error400 = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
		write(clientfd, error400, strlen(error400));
		free_message(message);
		close(clientfd);
		pthread_exit(NULL);
	}

	size_t index = message_lookup_field(message, "Content-Length");
	size_t clen = index!=-1 ? atoi(message->field_lines[index].value) : -1;

	char *target_path = message->start_line->request.target;
	if (target_path && target_path[0] == '/') {
		target_path++;
	}

	switch(message->start_line->request.method){
	case GET:{
			char *body = NULL;
			size_t body_len = 0;

			switch(read_file(target_path, &body, &body_len)){
				case 0:{
					char header[256];
					snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", body_len);

					write(clientfd, header, strlen(header));
					write(clientfd, body, body_len);

					free(body);
				} break;

				case 1:{
					const char *error404 = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>404 Not Found</h1>";
					write(clientfd, error404, strlen(error404));
				} break;

				default:{
					const char *error500 = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>500 Server Error</h1>";
					write(clientfd, error500, strlen(error500));
				} break;
			}
		} break;

		case HEAD:{
			if(!test_file(target_path)){
				const char *header = "HTTP/1.1 200 OK\r\n\r\n";
				write(clientfd, header, strlen(header));
			}else{
				const char *error404 = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
				write(clientfd, error404, strlen(error404));
			}
		} break;

		case PUT:{
			if(!write_file(target_path, message->body ? message->body : "", message->body_size, clientfd, clen)){
				const char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>Successful write</h1>";
				write(clientfd, header, strlen(header));
			}else{
				const char *error403 = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>Access denied</h1>";
				write(clientfd, error403, strlen(error403));
			}
		} break;

		case OPTIONS:{
			const char *options = "HTTP/1.1 204 No Content\r\nAllow: OPTIONS, GET, HEAD, PUT\r\n\r\n";
			write(clientfd, options, strlen(options));
		} break;

		case TRACE:{
			char response[BUFFER_SIZE + 128];
			snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: message/http\r\n\r\n%s", buffer);
			write(clientfd, response, strlen(response));
		} break;

		case DELETE:
		case CONNECT:
		case POST:
		case QUERY:
		case PATCH:{
			const char *error403 = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>Access denied</h1>";
			write(clientfd, error403, strlen(error403));
		} break;

		default:{
			const char *error500 = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>Server error</h1>";
			write(clientfd, error500, strlen(error500));
		} break;
	}

	free_message(message);

	shutdown(clientfd, SHUT_WR);
	close(clientfd);
	pthread_exit(NULL);
}

int read_file(char *path, char **body, size_t *file_size){
	if(!path || strlen(path) == 0) return 1;
	FILE* file = fopen(path, "rb");
	if(!file) return 1;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);

	if(size < 0) {
		fclose(file);
		return 1;
	}

	*body = (char*)malloc(size + 1);
	if(!*body) {
		fclose(file);
		return 1;
	}

	size_t read_bytes = fread(*body, 1, size, file);
	(*body)[read_bytes] = '\0';
	*file_size = read_bytes;

	fclose(file);
	return 0;
}

int write_file(char *path, char *body, size_t size, int clientfd, size_t total_len){
	if(!path || strlen(path) == 0) return 1;
	FILE* file = fopen(path, "wb");
	if(!file) return 1;
	if(body) fwrite(body, sizeof(char), size, file);
	if(total_len!=-1){
		while(total_len>size){
			char temp_buffer[BUFFER_SIZE];
			size_t tmp_size = read(clientfd, temp_buffer, BUFFER_SIZE);
			fwrite(temp_buffer, sizeof(char), tmp_size, file);
			size += tmp_size;
		}
	}
	fclose(file);
	return 0;
}

int test_file(char *path){
	if(!path || strlen(path) == 0) return 1;
	FILE* file = fopen(path, "r");
	if(!file) return 1;
	fclose(file);
	return 0;
}
