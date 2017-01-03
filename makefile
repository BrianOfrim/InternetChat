all: chat379 server379

chat379 : client.c
	gcc -o chat379 client.c

server379 : server.c
	gcc -o server379 server.c

clean :
	rm chat379 server379