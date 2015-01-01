#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <list>
#include <map>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

using namespace std;

class conn {
public:
	int ctrlfd;
	int datafd;

	conn() {};
	conn(int connfd) { ctrlfd = connfd; datafd = -1; }
};

class user {
public:
	list<string> files;
	list<conn> conns;
};

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
	int i;
	socklen_t clilen;
	socklen_t len;
	int listenfd, connfd, sockfd, dlistenfd, dconnfd;
	int val;
	fd_set rset, wset;
	int maxfd;
	char buff[1024];
	char username[100];
	char sendbuff[1024];
	char recvbuff[1024];
	char filename[1024];

	set<int> pend;
	map<string, user> users;
	
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

	for(;;) {
		FD_ZERO(&rset);
		//FD_ZERO(&wset);
		FD_SET(listenfd, &rset);
		maxfd = listenfd;

		for(set<int>::const_iterator pend_it = pend.begin(); pend_it != pend.end(); pend_it++) {
			FD_SET(*pend_it, &rset);
			if (*pend_it > maxfd) maxfd = *pend_it;
		}

		for(map<string, user>::const_iterator users_it = users.begin(); users_it != users.end(); users_it++) {
			const string &cur_name = users_it->first;
			const user &cur_user = users_it->second;
			for(list<conn>::const_iterator conn_it = cur_user.conns.begin(); conn_it != cur_user.conns.end(); conn_it++) {
				FD_SET(conn_it->ctrlfd, &rset);
				if (conn_it->ctrlfd) maxfd = conn_it->ctrlfd;
			}
		}

		select(maxfd+1, &rset, NULL, NULL, NULL);

		if (FD_ISSET(listenfd, &rset)) {
			clilen = sizeof(cliaddr);
			connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &clilen);

			/* print client info */
			len = sizeof(servaddr);
			getsockname(connfd, (struct sockaddr *) &servaddr, &len);
			inet_ntop(AF_INET, &servaddr.sin_addr, buff, sizeof(buff));
			fprintf(stderr, "%s:%hu\n", buff, ntohs(servaddr.sin_port));
			inet_ntop(AF_INET, &cliaddr.sin_addr, buff, sizeof(buff));
			fprintf(stderr, "%s:%hu\n", buff, ntohs(cliaddr.sin_port));

			pend.insert(connfd);
		} // listenfd if

		set<int> pend_copy = pend;
		for(set<int>::const_iterator pend_it = pend_copy.begin(); pend_it != pend_copy.end(); ++pend_it) {
			if (FD_ISSET(*pend_it, &rset)) {
				sockfd = *pend_it;
				if ( (n = read(sockfd, recvbuff, sizeof(recvbuff))) < 0 ) {
					fprintf(stderr, "client connection error\n");
					pend.erase(sockfd);
				} else if (n == 0) {
					fprintf(stderr, "client closed connection\n");
					pend.erase(sockfd);
				} else {
					if( sscanf(recvbuff, "/name %s", username) != 1 ) {
						fprintf(stderr, "name error\n");
					} else {
						pend.erase(sockfd);
						users[username].conns.push_back(conn(sockfd));
						sprintf(sendbuff, "Welcome to the dropbox-like server! : %s\n", username);
						fprintf(stderr, "connection from: %s\n", username);
						write(sockfd, sendbuff, strlen(sendbuff));
					}
				}
			}
		} // pend for

		for(map<string, user>::const_iterator users_it = users.begin(); users_it != users.end(); users_it++) {
			const string &cur_name = users_it->first;
			const user &cur_user = users_it->second;
			for(list<conn>::const_iterator conn_it = cur_user.conns.begin(); conn_it != cur_user.conns.end(); conn_it++) {
				if(FD_ISSET(conn_it->ctrlfd, &rset)) {
					connfd = conn_it->ctrlfd;
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
			} // conns for
		} // users for
	}

	return 0;
}
