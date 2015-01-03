#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<errno.h>

int read_cnt = 0;
char read_buff[1024];
char *read_ptr;

ssize_t my_read(int fd, char *ptr)
{
	if (read_cnt <= 0) {
		again:
		if ( (read_cnt = read(fd, read_buff, sizeof(read_buff))) < 0) {
			if (errno == EINTR)
				goto again;
			return -1;
		} else if (read_cnt == 0) {
			return 0;
		}
		read_ptr = read_buff;
	}
	
	read_cnt--;
	*ptr = *read_ptr++;
	return 1;
}

ssize_t readline(int fd, void *vptr, size_t maxlen)
{
	ssize_t n, rc;
	char c, *ptr;

	ptr = vptr;
	for (n = 1; n < maxlen; n++) {
		if ( (rc = my_read(fd, &c)) == 1) {
			*ptr++ = c;
			if (c == '\n')
				break;
		} else if (rc == 0) {
			*ptr = 0;
			return (n-1);
		} else
			return -1;
	}

	*ptr = 0;
	return n;
}

void print_status(int max, int cur)
{
	int i;
	fprintf(stderr, "Progress : [");
	for (i = 0; i < cur; i++) {
		fprintf(stderr, "#");
	}
	for (; i < max; i++) {
		fprintf(stderr, " ");
	}
	fprintf(stderr, "]\r");
}

int main(int argc, char **argv)
{
	int sockfd, datafd;
	unsigned int sec;
	fd_set allset, rset;
	int maxfdp1;
	ssize_t n;
	long nread;
	int oldbar, newbar;
	long sz;
	char buff[1024];
	unsigned short dport;
	char filename[1024];
	struct sockaddr_in servaddr;
	FILE *fp;
	int downloading = 0;

	if (argc != 4) {
		fprintf(stderr, "usage: %s <ip> <port> <username>\n", argv[0]);
		exit(1);
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(atoi(argv[2]));

	if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
		fprintf(stderr, "ip format error\n");
		exit(1);
	}

	if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		fprintf(stderr, "connect error\n");
		exit(1);
	} 

	sprintf(buff, "/name %s\n", argv[3]);
	write(sockfd, buff, strlen(buff));

	FD_ZERO(&allset);
	FD_SET(fileno(stdin), &allset);
	FD_SET(sockfd, &allset);
	maxfdp1 = (fileno(stdin) > sockfd ? fileno(stdin) : sockfd) + 1;

	for(;;) {
		rset = allset;
		select(maxfdp1, &rset, NULL, NULL, NULL);
		
		if (FD_ISSET(fileno(stdin), &rset)) {
			if (fgets(buff, sizeof(buff), stdin) == NULL) {
				close(sockfd);
				exit(0);
			}

			if (strcmp(buff, "/exit\n") == 0) {
				close(sockfd);
				exit(0);
			} else if (sscanf(buff, "/sleep %u", &sec) == 1) {
				downloading = 0;
				fprintf(stderr, "Client starts to sleep\n");
				int i;
				for (i = 0; i < sec; i++) {
					fprintf(stderr, "Sleep %d\n", i+1);
					sleep(1);
					
					FD_ZERO(&rset);
					FD_SET(sockfd, &rset);
					struct timeval tv;
					tv.tv_sec = 0;
					tv.tv_usec = 0;
					if (select(sockfd+1, &rset, NULL, NULL, &tv) > 0) {
						if( (n = readline(sockfd, buff, sizeof(buff))) < 0 ) {
							fprintf(stderr, "connection error\n");
							exit(1);
						} else if (n == 0) {
							fprintf(stderr, "connection closed by server\n");
							exit(1);
						}
						buff[n] = 0;
						if(!downloading && sscanf(buff, "/put %s %hu %ld", filename, &dport, &sz) == 3) {
							datafd = socket(AF_INET, SOCK_STREAM, 0);
							servaddr.sin_port = htons(dport);
							if (connect(datafd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
								fprintf(stderr, "download connect error\n");
								exit(1);
							} 
							downloading = 1;
						}
					}
				}
				fprintf(stderr, "Client wakes up\n");

				// duplicate download place

				if (downloading) {
					if ( (fp = fopen(filename, "w")) == NULL) {
						fprintf(stderr, "file open error\n");
					} else {
						fprintf(stderr, "Downloading file : %s\n", filename);
						print_status(22, 0);
						nread = 0;
						oldbar = newbar = 0;
						while ( (n = read(datafd, buff, sizeof(buff))) > 0 ) {
							nread += n;
							fwrite(buff, n, 1, fp);
							oldbar = newbar;
							newbar = nread*22/sz;
							if(oldbar != newbar)
								print_status(22, newbar);
						}
						if (n == 0) {
							fclose(fp);
							close(datafd);
						} else if (n < 0) {
							fprintf(stderr, "download connection failed\n");
							exit(1);
						}
						fprintf(stderr, "\nDownload %s complete!\n", filename);
					}
					downloading = 0;
				}

			} else if (sscanf(buff, "/put %s", filename) == 1) {

				if( (fp = fopen(filename, "r")) == NULL) {
					fprintf(stderr, "file open error\n");
				} else {
					write(sockfd, buff, strlen(buff));
					if( (n = readline(sockfd, buff, sizeof(buff))) < 0 ) {
						fprintf(stderr, "data connection error\n");
						exit(1);
					} else if (n == 0) {
						fprintf(stderr, "data connection closed by server\n");
						exit(1);
					}

					buff[n] = 0;
					sscanf(buff, "/pasv %hu", &dport);
					fprintf(stderr, "remote port: %hu\n", dport);

					datafd = socket(AF_INET, SOCK_STREAM, 0);
					servaddr.sin_port = htons(dport);
					if (connect(datafd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
						fprintf(stderr, "upload connect error\n");
						exit(1);
					} 

					fprintf(stderr, "Uploading file : %s\n", filename);
					fseek(fp, 0L, SEEK_END);
					sz = ftell(fp);
					fseek(fp, 0L, SEEK_SET);
					print_status(22, 0);
					nread = 0;
					oldbar = newbar = 0;
					while( (n = fread(buff, 1, sizeof(buff), fp)) > 0 ) {
						nread += n;
						write(datafd, buff, n);
						oldbar = newbar;
						newbar = nread*22/sz;
						if(oldbar != newbar)
							print_status(22, newbar);
					}
					fprintf(stderr, "\nUpload %s complete!\n", filename);
					fclose(fp);
					close(datafd);
				}
			}
		}

		if(FD_ISSET(sockfd, &rset)) {
			if( (n = readline(sockfd, buff, sizeof(buff))) < 0 ) {
				fprintf(stderr, "connection error\n");
				exit(1);
			} else if (n == 0) {
				fprintf(stderr, "connection closed by server\n");
				exit(1);
			}

			buff[n] = 0;
			if(sscanf(buff, "/put %s %hu %ld", filename, &dport, &sz) == 3) {
				datafd = socket(AF_INET, SOCK_STREAM, 0);
				servaddr.sin_port = htons(dport);
				if (connect(datafd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
					fprintf(stderr, "download connect error\n");
					exit(1);
				} 

				if ( (fp = fopen(filename, "w")) == NULL) {
					fprintf(stderr, "file open error\n");
				} else {
					fprintf(stderr, "Downloading file : %s\n", filename);
					print_status(22, 0);
					nread = 0;
					oldbar = newbar = 0;
					while ( (n = read(datafd, buff, sizeof(buff))) > 0 ) {
						nread += n;
						fwrite(buff, n, 1, fp);
						oldbar = newbar;
						newbar = nread*22/sz;
						if(oldbar != newbar)
							print_status(22, newbar);
					}
					if (n == 0) {
						fclose(fp);
						close(datafd);
					} else if (n < 0) {
						fprintf(stderr, "download connection failed\n");
						exit(1);
					}
					fprintf(stderr, "\nDownload %s complete!\n", filename);
				}
			} else {
				fprintf(stderr, "%s", buff);
			}
		}

	}

	return 0;
}
