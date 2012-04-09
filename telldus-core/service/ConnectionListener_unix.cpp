#include "ConnectionListener.h"
#include "Socket.h"
#include "Log.h"

#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#include <stdio.h>
#include <unistd.h>
#include <errno.h>

class ConnectionListener::PrivateData {
public:
	TelldusCore::EventRef waitEvent;
	std::string name;
	bool running;
	bool isTcp;
	SOCKET_T shutpipe[2];
};

ConnectionListener::ConnectionListener(const std::wstring &name, TelldusCore::EventRef waitEvent)
{
	d = new PrivateData;
	d->waitEvent = waitEvent;

	d->name = std::string(name.begin(), name.end());
	d->running = true;

	pipe(d->shutpipe);

	this->start();
}

ConnectionListener::~ConnectionListener(void) {
	d->running = false;

	write(d->shutpipe[1], "", 1);

	this->wait();

	if(!d->isTcp)
		unlink(d->name.c_str());

	close(d->shutpipe[0]);
	close(d->shutpipe[1]);
	delete d;
}

void ConnectionListener::run(){
	struct timeval tv = { 0, 0 };

	//Timeout for select
	SOCKET_T serverSocket;

	/* d->name is either a filesystem path, or a tcp socket.
	 * If a TCP socket, it should have format tcp://<ip>:<port>
	 */
	d->isTcp = 0;

	if(d->name.find("tcp://") == 0) {
		d->isTcp = 1;
		// Extract IP + port from name
		std::string sockspec(d->name, 6);
		size_t colon = sockspec.find(':');
		if(colon == std::string::npos) {
			Log::error("Invalid socket '%s', missing :", d->name.c_str());
			return;
		}

		std::string hostspec(sockspec, 0, colon);
		std::string portspec(sockspec, colon + 1);
		int port = (int)strtol(portspec.c_str(), NULL, 10);
		if(port == 0) {
			Log::error("Invalid socket '%s', cannot interpret port number", d->name.c_str());
			return;
		}
		if(hostspec.length() == 0) {
			Log::error("Invalid socket '%s', missing bind IP", d->name.c_str());
			return;
		}

		struct sockaddr_in addr;
		serverSocket = socket(AF_INET, SOCK_STREAM, 0);
		if (serverSocket < 0) {
			Log::error("Failed to create TCP socket, error %d (%s)", errno, strerror(errno));
			return;
		}

		int optval = 1;
		setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = inet_addr(hostspec.c_str());

		if(bind(serverSocket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			Log::error("Failed to bind socket '%s', error %d: %s", d->name.c_str(), errno, strerror(errno));
			close(serverSocket);
			return;
		}
	}else{
		// Use name as plain filename
		struct sockaddr_un name;
		serverSocket = socket(PF_LOCAL, SOCK_STREAM, 0);
		if (serverSocket < 0) {
			return;
		}
		name.sun_family = AF_LOCAL;
		memset(name.sun_path, '\0', sizeof(name.sun_path));
		strncpy(name.sun_path, d->name.c_str(), sizeof(name.sun_path));
		unlink(name.sun_path);
		int size = SUN_LEN(&name);
		bind(serverSocket, (struct sockaddr *)&name, size);
	}

	listen(serverSocket, 5);

	if(!d->isTcp) {
		//Change permissions to allow everyone
		chmod(d->name.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
	}

	fd_set infds;
	FD_ZERO(&infds);

	while(d->running) {
		tv.tv_sec = 5;

		FD_SET(serverSocket, &infds);
		FD_SET(d->shutpipe[0], &infds);
		int response = select(std::max(serverSocket, d->shutpipe[0]) + 1, &infds, NULL, NULL, &tv);
		if (response == 0) {
			continue;
		} else if (response < 0 ) {
			continue;
		}
		if (FD_ISSET(d->shutpipe[0], &infds)) {
			// Shutdown
			break;
		}
		//Make sure it is a new connection
		if (!FD_ISSET(serverSocket, &infds)) {
			continue;
		}
		SOCKET_T clientSocket = accept(serverSocket, NULL, NULL);

		ConnectionListenerEventData *data = new ConnectionListenerEventData();
		data->socket = new TelldusCore::Socket(clientSocket);
		d->waitEvent->signal(data);

	}
	close(serverSocket);
}

