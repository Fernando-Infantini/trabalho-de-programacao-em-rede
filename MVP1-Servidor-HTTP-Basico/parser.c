#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

int lstrcmp(const char *a, const char *b, size_t size);

// Converte a string bruta da requisição HTTP em uma struct estruturada
struct message *parse_message(char *stream){
	struct message* message = (struct message*)malloc(sizeof(struct message));
	if(!message) return NULL;
	message->field_lines = (struct field_line*)calloc(8, sizeof(struct field_line));
	if(!message->field_lines){
		free(message);
		return NULL;
	}

	size_t m, n=0;
	int field_status = -1;

	// Isola a primeira linha (Start-Line / Request-Line)
	for(;stream[n]!='\r' && stream[n]!='\n' && stream[n]!='\0'; n++);
	
	char *starting_line = (char*)calloc(n+1, sizeof(char));
	memcpy(starting_line, stream, n*sizeof(char));
	starting_line[n] = '\0';

	if(stream[n]=='\r') n++;
	n++;

	message->start_line = parse_starting_line(starting_line);
	free(starting_line);

	// Processa até 8 cabeçalhos (Field Lines)
	for(int i=0; i<8; i++){
		for(m=n; stream[m]!='\r' && stream[m]!='\n' && stream[m]!='\0'; m++);
		if(stream[m]=='\0') break;

		char *field_line = (char*)calloc(m-n+1, sizeof(char));
		memcpy(field_line, stream+n, m-n);
		field_line[m-n] = '\0';

		if(stream[m]=='\r') m++;
		m++;
		n = m;

		field_status = parse_field_line(&message->field_lines[i], field_line);
		free(field_line);
		if(field_status == 1) break; // Encontrou a linha em branco que separa headers do body
	}

	// Copia o corpo da requisição (payload), se houver
	if(stream[n]!='\0'){
		size_t body_len = strlen(stream + n);
		message->body = (char*)calloc(body_len + 1, sizeof(char));
		if(message->body){
			strcpy(message->body, stream + n);
		}
	} else {
		message->body = NULL;
	}

	return message;
}

// Desaloca com segurança todas as alocações dinâmicas da struct message

union starting_line *parse_starting_line(char *stream){
	union starting_line *line = (union starting_line*)malloc(sizeof(union starting_line));
	if(!line) return NULL;
	size_t n = 0;
	char err = 0;
	if(stream[1]!='T')
		err = parse_request_line(line, stream, &n);
	else
		err = parse_status_line(line, stream, &n);
	if(err){
		free(line);
		return NULL;
	}
	return line;
}

int parse_request_line(union starting_line *line, char *stream, size_t *n){
	struct request_line *result = &line->request;

	result->method = parse_method(stream, n);
	if(result->method == UNKNOWN_METHOD || stream[*n]!=' '){
		return -1;
	} (*n)++;

	result->target = parse_path(stream, n);
	if(!result->target || stream[*n]!=' '){
		return -1;
	} (*n)++;

	if(parse_version(&result->ver, stream, n)){
		return -1;
	}
	return 0;
}

int parse_status_line(union starting_line *line, char *stream, size_t *n){
	struct status_line *result = &line->status;

	if(parse_version(&result->ver, stream, n) || stream[*n]!=' '){
		return -1;
	} (*n)++;

	result->status = parse_status(stream, n);
	if(result->status == 0 || stream[*n]!=' '){
		return -1;
	}

	return 0;
}

int parse_field_line(struct field_line *fline, char *stream){
	if(stream[0] == '\0' || !strcmp(stream, "\r\n") || !strcmp(stream, "\n")){
		return 1;
	}
	size_t tmp = 0;
	for(; stream[tmp]!=':' && stream[tmp]!='\0' && !isspace(stream[tmp]); tmp++);
	if(stream[tmp] != ':'){
		return -1;
	}

	fline->name = (char*)calloc(tmp+1, sizeof(char));
	memcpy(fline->name, stream, tmp);
	fline->name[tmp] = '\0';

	tmp++;
	for(; isspace(stream[tmp]); tmp++);

	size_t val_start = tmp;
	for(; stream[tmp]!='\r' && stream[tmp]!='\n' && stream[tmp]!='\0'; tmp++);
	fline->value = (char*)calloc(tmp - val_start + 1, sizeof(char));
	memcpy(fline->value, stream + val_start, tmp - val_start);
	fline->value[tmp - val_start] = '\0';

	return 0;
}

enum methods parse_method(char *stream, size_t *n){
	switch(stream[*n]){
		case 'G':
			if(!lstrcmp(stream+*n, "GET", 3)) { *n+=3; return GET; }
			break;
		case 'P':
			if(!lstrcmp(stream+*n, "PUT", 3)) { *n+=3; return PUT; }
			if(!lstrcmp(stream+*n, "POST", 4)) { *n+=4; return POST; }
			if(!lstrcmp(stream+*n, "PATCH", 5)) { *n+=5; return PATCH; }
			break;
		case 'H':
			if(!lstrcmp(stream+*n, "HEAD", 4)) { *n+=4; return HEAD; }
			break;
		case 'Q':
			if(!lstrcmp(stream+*n, "QUERY", 5)) { *n+=5; return QUERY; }
			break;
		case 'C':
			if(!lstrcmp(stream+*n, "CONNECT", 7)) { *n+=7; return CONNECT; }
			break;
		case 'D':
			if(!lstrcmp(stream+*n, "DELETE", 6)) { *n+=6; return DELETE; }
			break;
		case 'O':
			if(!lstrcmp(stream+*n, "OPTIONS", 7)) { *n+=7; return OPTIONS; }
			break;
		case 'T':
			if(!lstrcmp(stream+*n, "TRACE", 5)) { *n+=5; return TRACE; }
			break;
	}
	return UNKNOWN_METHOD;
}

char *parse_path(char *stream, size_t *n){
	int tmp = *n;
	for(; stream[tmp]!=' ' && stream[tmp]!='\0'; tmp++);
	char *result = (char*)calloc(tmp - *n + 1, sizeof(char));
	if(!result) return NULL;
	memcpy(result, stream + *n, tmp - *n);
	result[tmp - *n] = '\0';
	*n = tmp;
	return result;
}

int parse_version(struct version *ver, char *stream, size_t *n){
	if(lstrcmp(stream+*n, "HTTP/", 5)) return -1;
	*n += 5;
	if(!isdigit(stream[*n]) || stream[*n+1]!='.' || !isdigit(stream[*n+2])) return -1;
	ver->all[0] = stream[*n];
	ver->all[1] = stream[*n+2];
	*n += 3;
	return 0;
}

unsigned int parse_status(char *stream, size_t *n){
	for(int i=0; i<3; i++) if(!isdigit(stream[*n+i])) return 0;
	if(stream[*n+3]!=' ') return 0;
	unsigned int val = atoi(&(stream[*n]));
	*n += 3;
	return val;
}

int lstrcmp(const char *a, const char *b, size_t size){
	for(size_t i=0; i<size; i++)
		if(a[i]!=b[i]) return -1;
	return 0;
}
