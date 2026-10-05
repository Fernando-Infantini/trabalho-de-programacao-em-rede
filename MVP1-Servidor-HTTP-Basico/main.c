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

// Assinaturas das funções do servidor
void *handle_answer(void *clientfd_v);
int read_file(char *path, char **body, size_t *file_size);
int write_file(char *path, char *body);
int test_file(char *path);

int main (void){
	int doorwayfd;
	struct sockaddr_in my_addr;
	int addrlen = sizeof(my_addr);

	// Criação do socket TCP (IPv4, orientada à conexão)
	doorwayfd = socket(AF_INET, SOCK_STREAM, 0);
	if(doorwayfd == -1){
		perror("Erro ao criar socket");
		return -1;
	}

	// Permite reutilizar a porta imediatamente após o término do processo (evita erro de 'Address already in use')
	int opt = 1;
	setsockopt(doorwayfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	// Definindo endereço e porta de escuta
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(MYPORT); // Conversão para Big Endian (Network Byte Order)
	my_addr.sin_addr.s_addr = inet_addr(MYADDR);

	// Associa o socket ao endereço da interface local
	if(bind(doorwayfd, (struct sockaddr*)&my_addr, sizeof(my_addr)) == -1){
		perror("Erro no bind");
		return -1;
	}

	// Coloca o socket em escuta passiva com fila de conexões pendentes de tamanho 5
	if(listen(doorwayfd, 5) == -1){
		perror("Erro no listen");
		return -1;
	}

	printf("Servidor rodando em http://%s:%d\n", MYADDR, MYPORT);

	// Loop principal de aceitação de clientes
	while(1){
		int clientfd = accept(doorwayfd, (struct sockaddr*)&my_addr, (socklen_t*)&addrlen);
		if(clientfd == -1) continue;

		pthread_t answer_thread;
		// Despacha o tratamento do cliente para uma nova thread POSIX (servidor concorrente)
		if(pthread_create(&answer_thread, NULL, handle_answer, (void*)(intptr_t)clientfd) == 0){
			pthread_detach(answer_thread); // Desanexa a thread para liberar recursos automaticamente ao finalizar
		} else {
			close(clientfd);
		}
	}

	close(doorwayfd);
	return 0;
}

// Handler responsável pelo ciclo de Vida da Requisição/Resposta HTTP
void *handle_answer(void *clientfd_v){
	int clientfd = (int)(intptr_t)clientfd_v;
	char buffer[BUFFER_SIZE];

	// Leitura da requisição do socket
	ssize_t bytes_read = read(clientfd, buffer, BUFFER_SIZE-1);
	if (bytes_read <= 0){
		close(clientfd);
		pthread_exit(NULL);
	}
	buffer[bytes_read] = '\0';

	// Executa o parse do protocolo HTTP sobre a string recebida
	struct message *message = parse_message(buffer);
	if(!message || !message->start_line){
		const char *error400 = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
		write(clientfd, error400, strlen(error400));
		free_message(message);
		close(clientfd);
		pthread_exit(NULL);
	}

	// Sanitização básica do caminho relativo
	char *target_path = message->start_line->request.target;
	if (target_path && target_path[0] == '/') {
		target_path++;
	}

	// Trata os métodos HTTP suportados
	switch(message->start_line->request.method){
	case GET:{
			char *body = NULL;
			size_t body_len = 0;

			switch(read_file(target_path, &body, &body_len)){
				case 0:{
					// Monta o cabeçalho separado do corpo para evitar estourar o buffer de resposta
					char header[256];
					snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", body_len);
					
					// Envia primeiro os cabeçalhos e depois o corpo do arquivo
					write(clientfd, header, strlen(header));
					write(clientfd, body, body_len);

					// Libera o buffer do arquivo alocado na read_file
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
			if(!write_file(target_path, message->body ? message->body : "")){
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

	// Libera a memória heap alocada pela estrutura da mensagem HTTP
	free_message(message);

	// Encerra o canal de escrita e fecha o socket
	shutdown(clientfd, SHUT_WR);
	close(clientfd);
	pthread_exit(NULL);
}

// Lê arquivos de qualquer tamanho alocando memória dinamicamente no Heap
int read_file(char *path, char **body, size_t *file_size){
	if(!path || strlen(path) == 0) return 1;
	FILE* file = fopen(path, "rb");
	if(!file) return 1;

	// Descobre o tamanho total do arquivo
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);

	if(size < 0) {
		fclose(file);
		return 1;
	}

	// Aloca memória suficiente para o arquivo + terminador nulo
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

// Escreve o conteúdo recebido no arquivo indicado
int write_file(char *path, char *body){
	if(!path || strlen(path) == 0) return 1;
	FILE* file = fopen(path, "w");
	if(!file) return 1;
	if(body) {
		fputs(body, file);
	}
	fclose(file);
	return 0;
}

// Verifica se o arquivo existe e pode ser lido
int test_file(char *path){
	if(!path || strlen(path) == 0) return 1;
	FILE* file = fopen(path, "r");
	if(!file) return 1;
	fclose(file);
	return 0;
}