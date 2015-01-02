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
	list<string> files;

	conn(int connfd, list<string> &filelist) { 
		ctrlfd = connfd;
		datafd = -1;
		status = IDLE;
		files = filelist;
		fp = NULL;
	}
};

class user {
public:
	list<string> files;
	list<conn> conns;

	void broadcast(string filename, list<conn>::iterator excl) {
		for (list<conn>::iterator it = conns.begin(); it != conns.end(); it++) {
			if (it != excl) {
				it->files.push_back(filename);
			}
		}
		files.push_back(filename);
	}
};

int new_tcp_socket()
{
	int listenfd;
	int val, flag;
	struct sockaddr_in servaddr;
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	flag = fcntl(listenfd, F_GETFL, 0);
	fcntl(listenfd, F_SETFL, flag|O_NONBLOCK);

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
	size_t nr;
	int i;
	int flag;
	long sz;
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
	flag = fcntl(listenfd, F_GETFL, 0);
	fcntl(listenfd, F_SETFL, flag|O_NONBLOCK);

	val = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(atoi(argv[1]));

	bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));

	listen(listenfd, 10);

	for(;;) {
		// --------------------------------------------------------------------------------
		// setup select
		// --------------------------------------------------------------------------------
		FD_ZERO(&rset);
		FD_ZERO(&wset);
		FD_SET(listenfd, &rset);
		maxfd = listenfd;

		for(list<int>::const_iterator pend_it = pend.begin(); pend_it != pend.end(); pend_it++) {
			FD_SET(*pend_it, &rset);
			if (*pend_it > maxfd) maxfd = *pend_it;
		}

		for(map<string, user>::iterator users_it = users.begin(); users_it != users.end(); users_it++) {
			const string &cur_name = users_it->first;
			user &cur_user = users_it->second;
			for(list<conn>::iterator conn_it = cur_user.conns.begin(); conn_it != cur_user.conns.end(); conn_it++) {
				FD_SET(conn_it->ctrlfd, &rset);
				if (conn_it->ctrlfd > maxfd) maxfd = conn_it->ctrlfd;

				if (conn_it->status == IDLE && !conn_it->files.empty()) {
					dlistenfd = new_tcp_socket();
					len = sizeof(dservaddr);
					getsockname(dlistenfd, (struct sockaddr *) &dservaddr, &len);

					conn_it->status = WAIT_DOWNLOAD;
					conn_it->datafd = dlistenfd;
					conn_it->filename = conn_it->files.front();
					conn_it->files.pop_front();

					fprintf(stderr, "openning file %s\n", conn_it->filename.c_str());
					if ((conn_it->fp = fopen((cur_name+"_"+conn_it->filename).c_str(), "r")) == NULL) {
						fprintf(stderr, "open input file error\n");
						exit(1);
					} else {
						fprintf(stderr, "open file %s\n", conn_it->filename.c_str());
					}

					fseek(conn_it->fp, 0L, SEEK_END);
					sz = ftell(conn_it->fp);
					fseek(conn_it->fp, 0L, SEEK_SET);

					sprintf(sendbuff, "/put %s %hu %ld\n", conn_it->filename.c_str(), ntohs(dservaddr.sin_port), sz);
					if ( (n = write(conn_it->ctrlfd, sendbuff, strlen(sendbuff))) < 0) {
						if (errno != EWOULDBLOCK) {
							fprintf(stderr, "download request write error\n");
						}
					}

					fprintf(stderr, "bind port: %hu\n", ntohs(dservaddr.sin_port));
				}

				if (conn_it->status == WAIT_UPLOAD || conn_it->status == WAIT_DOWNLOAD || conn_it->status == UPLOAD) {
					FD_SET(conn_it->datafd, &rset);
					if (conn_it->datafd > maxfd) maxfd = conn_it->datafd;
				}

				if (conn_it->status == DOWNLOAD) {
					FD_SET(conn_it->datafd, &wset);
					if (conn_it->datafd > maxfd) maxfd = conn_it->datafd;
				}
			}
		}

		// --------------------------------------------------------------------------------
		// call select
		// --------------------------------------------------------------------------------
		//fprintf(stderr, "call select\n");
		select(maxfd+1, &rset, &wset, NULL, NULL);
		//fprintf(stderr, "exit select\n");

		// --------------------------------------------------------------------------------
		// check listening socket
		// --------------------------------------------------------------------------------
		if (FD_ISSET(listenfd, &rset)) {
			clilen = sizeof(cliaddr);
			if ( (connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &clilen)) < 0) {
				if(errno != EWOULDBLOCK) {
					fprintf(stderr, "accept error\n");
					exit(1);
				}
			} else {
				/* print client info */
				len = sizeof(servaddr);
				getsockname(connfd, (struct sockaddr *) &servaddr, &len);
				inet_ntop(AF_INET, &servaddr.sin_addr, buff, sizeof(buff));
				fprintf(stderr, "%s:%hu\n", buff, ntohs(servaddr.sin_port));
				inet_ntop(AF_INET, &cliaddr.sin_addr, buff, sizeof(buff));
				fprintf(stderr, "%s:%hu\n", buff, ntohs(cliaddr.sin_port));

				pend.push_back(connfd);
			}
		} // listenfd if

		// --------------------------------------------------------------------------------
		// check pending connections
		// --------------------------------------------------------------------------------
		for(list<int>::const_iterator pend_it = pend.begin(); pend_it != pend.end();) {
			if (FD_ISSET(*pend_it, &rset)) {
				sockfd = *pend_it;
				if ( (n = read(sockfd, recvbuff, sizeof(recvbuff))) < 0 ) {
					if (errno != EWOULDBLOCK) {
						fprintf(stderr, "client connection error\n");
						exit(1);
					}
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
						users[username].conns.push_back(conn(sockfd, users[username].files));
						sprintf(sendbuff, "Welcome to the dropbox-like server! : %s\n", username);
						fprintf(stderr, "connection from: %s\n", username);
						if( (n = write(sockfd, sendbuff, strlen(sendbuff))) < 0) {
							if(errno != EWOULDBLOCK) {
								fprintf(stderr, "write welcome message error\n");
								exit(1);
							} else {
								fprintf(stderr, "write welcome message failed\n");
							}
						}
						FD_CLR(sockfd, &rset);
					}
				}
			} else {
				pend_it++;
			}
		} // pend for

		// --------------------------------------------------------------------------------
		// check established connections
		// --------------------------------------------------------------------------------
		for(map<string, user>::iterator users_it = users.begin(); users_it != users.end(); users_it++) {
			const string &cur_name = users_it->first;
			user &cur_user = users_it->second;
			for(list<conn>::iterator conn_it = cur_user.conns.begin(); conn_it != cur_user.conns.end();) {

				if (conn_it->status != IDLE && FD_ISSET(conn_it->datafd, &wset)) {
					if( (nr = fread(buff, 1, sizeof(buff), conn_it->fp)) == 0 ) {
						close(conn_it->datafd);
						fclose(conn_it->fp);
						conn_it->datafd = -1;
						conn_it->status = IDLE;
						conn_it->filename = "";
						fprintf(stderr, "all data sent\n");
					} else {
						if ( (n = write(conn_it->datafd, buff, nr)) < 0 ) {
							if (errno != EWOULDBLOCK) {
								fprintf(stderr, "data send error\n");
								exit(1);
							} else {
								fprintf(stderr, "write would block\n");
							}
						} else if (n == 0) {
							fprintf(stderr, "client closed download connection\n");
							fclose(conn_it->fp);
							conn_it->datafd = -1;
							conn_it->status = IDLE;
						} else if (n < nr) {
							fprintf(stderr, "not all data are sent\n");
						} else {
							//fprintf(stderr, "data sent\n");
						}
					}

				} else if (conn_it->status != IDLE && FD_ISSET(conn_it->datafd, &rset)) {
					switch (conn_it->status) {
						case WAIT_UPLOAD:
							if( (dconnfd = accept(conn_it->datafd, (struct sockaddr *) &cliaddr, &clilen)) < 0) {
								if(errno != EWOULDBLOCK) {
									fprintf(stderr, "accept error\n");
									exit(1);
								}
							} else {
								fprintf(stderr, "upload data connection accepted\n");
								close(conn_it->datafd);
								conn_it->datafd = dconnfd;
								conn_it->status = UPLOAD;

								if ((conn_it->fp = fopen((cur_name+"_"+conn_it->filename).c_str(), "w")) == NULL) {
									fprintf(stderr, "open output file error\n");
									exit(1);
								} else {
									fprintf(stderr, "open file %s\n", conn_it->filename.c_str());
								}
							}
							break;

						case UPLOAD:
							if ( (n = read(conn_it->datafd, buff, sizeof(buff))) < 0 ) {
								if (errno != EWOULDBLOCK) {
									fprintf(stderr, "data read error\n");
									exit(1);
								}
							} else if (n == 0) {
								fclose(conn_it->fp);
								conn_it->datafd = -1;
								conn_it->status = IDLE;
								cur_user.broadcast(conn_it->filename, conn_it);
							} else {
								fwrite(buff, n, 1, conn_it->fp);
							}
							break;

						case WAIT_DOWNLOAD:
							if ( (dconnfd = accept(conn_it->datafd, (struct sockaddr *) &cliaddr, &clilen)) < 0) {
								if(errno != EWOULDBLOCK) {
									fprintf(stderr, "accept error\n");
									exit(1);
								}
							} else {
								fprintf(stderr, "download data connection accepted\n");
								close(conn_it->datafd);
								conn_it->datafd = dconnfd;
								conn_it->status = DOWNLOAD;
							}
							break;
					}
					conn_it++;
				} else if (FD_ISSET(conn_it->ctrlfd, &rset)) {
					connfd = conn_it->ctrlfd;
					if( (n = read(connfd, buff, sizeof(buff))) < 0 ) {
						if (errno != EWOULDBLOCK) {
							fprintf(stderr, "connection failed\n");
							conn_it = cur_user.conns.erase(conn_it);
							exit(1);
						}
						conn_it++;
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
							if ( (n = write(connfd, sendbuff, strlen(sendbuff))) < 0 ) {
								if(errno != EWOULDBLOCK) {
									fprintf(stderr, "send /pasv error");
									exit(1);
								}
							}
							fprintf(stderr, "bind port: %hu\n", ntohs(dservaddr.sin_port));

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

	} // select for

	return 0;
}
