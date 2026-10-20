#ifndef PARSER
#define PARSER
#include "message.h"

struct message *parse_message(char *stream, size_t bytes_read);

union starting_line *parse_starting_line(char *stream);
int parse_request_line(union starting_line *line, char *stream, size_t *n);
int parse_status_line(union starting_line *line, char *stream, size_t *n);

int parse_field_line(struct field_line *fline, char *stream);

enum methods parse_method(char *stream, size_t *n);
char *parse_path(char *stream, size_t *n);
int parse_version(struct version *ver, char *stream, size_t *n);
unsigned int parse_status(char *stream, size_t *n);

#endif
