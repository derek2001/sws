PROG = sws
OBJS = main.o sws.o sws_assist.o req_process.o response.o req_parse.o cgi_process.o file_operation.o
CC = gcc

CFLAGS = -Wall
LDFLAGS = -lmagic

LDFLAGS += -lbsd

all: ${PROG}

${PROG}	: ${OBJS}
	${CC} ${CFLAGS} -o ${PROG} ${OBJS} ${LDFLAGS}

main.o	: src/main.c src/sws.h src/req_process.h src/sws_assist.h
	${CC} ${CFLAGS} -c src/main.c
sws.o	: src/sws.c src/sws.h
	${CC} ${CFLAGS} -c src/sws.c
sws_assist.o	: src/sws_assist.c src/sws_assist.h
	${CC} ${CFLAGS} -c src/sws_assist.c
req_process.o	: src/req_process.c src/req_process.h
	${CC} ${CFLAGS} -c src/req_process.c
response.o	: src/response.c src/response.h
	${CC} ${CFLAGS} -c src/response.c
req_parse.o	: src/req_parse.c src/req_info.h
	${CC} ${CFLAGS} -c src/req_parse.c
cgi_process.o: src/cgi_process.h src/cgi_process.c
	${CC} ${CFLAGS} -c src/cgi_process.c
file_operation.o: src/file_operation.c src/file_operation.h
	${CC} ${CFLAGS} -c src/file_operation.c

clean	:
	rm -f ${PROG} *.o  src/*~
