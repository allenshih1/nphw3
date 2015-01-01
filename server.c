#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<netinet/in.h>

int new_tcp_socket()
{
	int listenfd;
	int val;
	struct sockaddr_in servaddr;
	listenfd = socket(AF_INET, SOCK_STREAM, 0);

	val = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(0);

	bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));

	listen(listenfd, 1);
	return listenfd;
}

int main(int argc, char **argv)
{
	ssize_t n;
	socklen_t clilen;
	socklen_t len;
	int listenfd, connfd, dlistenfd, dconnfd;
	int val;
	char buff[1024];
	char sendbuff[1024];
	char recvbuff[1024];
	char filename[1024];
	struct sockaddr_in servaddr, cliaddr;
	struct sockaddr_in dservaddr, dcliaddr;
	FILE *frecv;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	listenfd = socket(AF_INET, SOCK_STREAM, 0);

	val = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(atoi(argv[1]));

	bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));

	listen(listenfd, 10);

	clilen = sizeof(cliaddr);
	connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &clilen);

	len = sizeof(servaddr);
	getsockname(connfd, (struct sockaddr *) &servaddr, &len);
	inet_ntop(AF_INET, &servaddr.sin_addr, buff, sizeof(buff));
	fprintf(stderr, "%s:%hu\n", buff, ntohs(servaddr.sin_port));
	inet_ntop(AF_INET, &cliaddr.sin_addr, buff, sizeof(buff));
	fprintf(stderr, "%s:%hu\n", buff, ntohs(cliaddr.sin_port));

	while (1) {
		if( (n = read(connfd, buff, sizeof(buff))) < 0 ) {
			fprintf(stderr, "connection failed\n");
			exit(1);
		} else if (n == 0) {
			fprintf(stderr, "connection closed by client\n");
			exit(0);
		}

		buff[n] = 0;
		fputs(buff, stdout);

		if (sscanf(buff, "/put %s", filename) == 1) {
			dlistenfd = new_tcp_socket();
			len = sizeof(dservaddr);
			getsockname(dlistenfd, (struct sockaddr *) &dservaddr, &len);

			sprintf(sendbuff, "/pasv %hu", ntohs(dservaddr.sin_port));
			printf("bind port: %hu\n", ntohs(dservaddr.sin_port));

			write(connfd, sendbuff, strlen(sendbuff));
			dconnfd = accept(dlistenfd, (struct sockaddr *) &cliaddr, &clilen);
			printf("data connection accepted\n");

			if ((frecv = fopen("y", "w")) == NULL) {
				fprintf(stderr, "open output file error\n");
				exit(1);
			}

			while (1) {
				if ( (n = read(dconnfd, buff, sizeof(buff))) < 0 ) {
					fprintf(stderr, "data read error\n");
					exit(1);
				} else if (n == 0) {
					fclose(frecv);
					break;
				}
				fwrite(buff, n, 1, frecv);
			}
		}
	}

	return 0;
}
