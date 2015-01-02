#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <list>
#include <map>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

#define IDLE 0
#define UPLOAD 1
#define DOWNLOAD 2
#define WAIT_UPLOAD 3
#define WAIT_DOWNLOAD 4

using namespace std;

class conn {
public:
	int ctrlfd;
	int datafd;
	int status;
	string filename;
	FILE *fp;

	conn() {};
	conn(int connfd) { ctrlfd = connfd; datafd = -1; status = IDLE; }
};

class user {
public:
	list<string> files;
	list<conn> conns;
};

int new_tcp_socket()
{
	int listenfd;
	int val, flag;
	struct sockaddr_in servaddr;
	listenfd = socket(AF_INET, SOCK_STREAM, 0);

	//flag = fcntl(listenfd, F_GETFL, 0);
	//fcntl(listenfd, F_SETFL, flag|O_NONBLOCK);

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

	list<int> pend;
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

		for(list<int>::const_iterator pend_it = pend.begin(); pend_it != pend.end(); pend_it++) {
			FD_SET(*pend_it, &rset);
			if (*pend_it > maxfd) maxfd = *pend_it;
		}

		for(map<string, user>::const_iterator users_it = users.begin(); users_it != users.end(); users_it++) {
			const string &cur_name = users_it->first;
			const user &cur_user = users_it->second;
			for(list<conn>::const_iterator conn_it = cur_user.conns.begin(); conn_it != cur_user.conns.end(); conn_it++) {
				FD_SET(conn_it->ctrlfd, &rset);
				if (conn_it->ctrlfd > maxfd) maxfd = conn_it->ctrlfd;

				if (conn_it->status != IDLE) {
					FD_SET(conn_it->datafd, &rset);
					if (conn_it->datafd > maxfd) maxfd = conn_it->datafd;
				}
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

			pend.push_back(connfd);
		} // listenfd if

		for(list<int>::const_iterator pend_it = pend.begin(); pend_it != pend.end();) {
			if (FD_ISSET(*pend_it, &rset)) {
				sockfd = *pend_it;
				if ( (n = read(sockfd, recvbuff, sizeof(recvbuff))) < 0 ) {
					fprintf(stderr, "client connection error\n");
					pend_it = pend.erase(pend_it);
				} else if (n == 0) {
					fprintf(stderr, "client closed connection\n");
					pend_it = pend.erase(pend_it);
				} else {
					if( sscanf(recvbuff, "/name %s", username) != 1 ) {
						pend_it++;
						fprintf(stderr, "name error\n");
					} else {
						pend_it = pend.erase(pend_it);
						users[username].conns.push_back(conn(sockfd));
						sprintf(sendbuff, "Welcome to the dropbox-like server! : %s\n", username);
						fprintf(stderr, "connection from: %s\n", username);
						write(sockfd, sendbuff, strlen(sendbuff));
					}
				}
			} else {
				pend_it++;
			}
		} // pend for

		for(map<string, user>::iterator users_it = users.begin(); users_it != users.end(); users_it++) {
			const string &cur_name = users_it->first;
			user &cur_user = users_it->second;
			for(list<conn>::iterator conn_it = cur_user.conns.begin(); conn_it != cur_user.conns.end();) {

				if (FD_ISSET(conn_it->datafd, &rset)) {
					switch (conn_it->status) {
						case WAIT_UPLOAD:
							dconnfd = accept(conn_it->datafd, (struct sockaddr *) &cliaddr, &clilen);
							printf("data connection accepted\n");
							close(conn_it->datafd);
							conn_it->datafd = dconnfd;
							conn_it->status = UPLOAD;

							if ((conn_it->fp = fopen(conn_it->filename.c_str(), "w")) == NULL) {
								fprintf(stderr, "open output file error\n");
								exit(1);
							} else {
								fprintf(stderr, "open file %s\n", conn_it->filename.c_str());
							}
							break;

						case UPLOAD:
							if ( (n = read(conn_it->datafd, buff, sizeof(buff))) < 0 ) {
								fprintf(stderr, "data read error\n");
								exit(1);
							} else if (n == 0) {
								fclose(conn_it->fp);
								conn_it->datafd = -1;
								conn_it->status = IDLE;
							} else {
								fwrite(buff, n, 1, conn_it->fp);
							}
							break;
					}
					conn_it++;
				} else if (FD_ISSET(conn_it->ctrlfd, &rset)) {
					connfd = conn_it->ctrlfd;
					if( (n = read(connfd, buff, sizeof(buff))) < 0 ) {
						fprintf(stderr, "connection failed\n");
						exit(1);
						conn_it = cur_user.conns.erase(conn_it);
					} else if (n == 0) {
						fprintf(stderr, "connection closed by client\n");
						conn_it = cur_user.conns.erase(conn_it);
					} else {
						buff[n] = 0;
						fputs(buff, stdout);

						if (sscanf(buff, "/put %s", filename) == 1) {
							dlistenfd = new_tcp_socket();
							len = sizeof(dservaddr);
							getsockname(dlistenfd, (struct sockaddr *) &dservaddr, &len);

							sprintf(sendbuff, "/pasv %hu", ntohs(dservaddr.sin_port));
							write(connfd, sendbuff, strlen(sendbuff));
							printf("bind port: %hu\n", ntohs(dservaddr.sin_port));

							conn_it->datafd = dlistenfd;
							conn_it->status = WAIT_UPLOAD;
							conn_it->filename = filename;
						}
						conn_it++;
					} // else read
				} else {
					conn_it++;
				} // else FD_ISSET
			} // conns for
		} // users for
	}

	return 0;
}
