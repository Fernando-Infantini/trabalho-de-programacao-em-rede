#ifndef MESSAGE
#define MESSAGE

enum methods {UNKNOWN_METHOD,
	GET,
	QUERY,
	HEAD,
	POST,
	PUT,
	DELETE,
	CONNECT,
	OPTIONS,
	TRACE,
	PATCH
};

struct version{
	char all[2];
};

struct request_line{
	enum methods method;
	char *target;
	struct version ver;
};

struct status_line{
	unsigned int status;
	char *reason_phrase;
	struct version ver;
};

union starting_line{
	struct request_line request;
	struct status_line status;
};

struct field_line{
	char *name;
	char *value;
};

struct message{
	union starting_line *start_line;
	struct field_line *field_lines;
	char *body;
};

void free_message(struct message *message);
#endif
