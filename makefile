libmsocket.a: mysocket.o
	ar rcs libmsocket.a mysocket.o

mysocket.o: mysocket.c
	gcc -c -Wall mysocket.c -lpthread -lm

clean:
	rm -f mysocket.o libmsocket.a server client
