#include <stdlib.h>
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
