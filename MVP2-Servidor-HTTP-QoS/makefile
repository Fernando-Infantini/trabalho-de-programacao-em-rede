index:
	gcc message.c -c
	gcc parser.c -c
	gcc main.c message.o parser.o -o servidor -pthread

clear:
	rm -f message.o
	rm -f parser.o
	rm -f servidor