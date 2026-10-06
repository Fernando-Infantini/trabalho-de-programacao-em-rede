#include <stdlib.h>
#include <string.h>
#include "message.h"
void free_message(struct message *message){
	if(!message) return;

	if(message->start_line){
		if(message->start_line->request.target){
			free(message->start_line->request.target);
		}
		free(message->start_line);
	}

	if(message->field_lines){
		for(int i = 0; i < 8; i++){
			if(message->field_lines[i].name) free(message->field_lines[i].name);
			if(message->field_lines[i].value) free(message->field_lines[i].value);
		}
		free(message->field_lines);
	}

	if(message->body){
		free(message->body);
	}

	free(message);
	return;
}

size_t message_lookup_field(struct message *message, const char *field){
	size_t index=0;
	for(;index<8 && strcmp(message->field_lines[index].name, field);index++);
	if(index==8) index = -1;
	return index;
}
