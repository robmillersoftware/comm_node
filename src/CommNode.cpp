#include "CommNode.h"
#include <boost/uuid/uuid_generators.hpp>
#include "CommNodeLog.h"

extern CommNodeLog* cnLog;
extern int mkaddr(void* addr, int* addrlen,
	char* str_addr, char* protocol);

CommNode::CommNode() {
	uuid = boost::uuids::random_generator()();
}

CommNode::CommNode(int port) {
	portNumber = port;
	uuid = boost::uuids::random_generator()();
	bc_addr = "127.255.255.2:" + std::to_string(portNumber);
	setUpUDPSocket();
}

void CommNode::start() {
	running = true;
	startListeningUDP();
}

void CommNode::stop() {
	running = false;
}

void CommNode::startListeningUDP() {
	int ret = pthread_create(&udpThread, NULL, &CommNode::handleUDP, this);	

	if (ret) {
		char msg[512];
		sprintf(msg, "Error creating POSIX thread: %d", ret);
		cnLog->writeMessage(CommNodeLog::severities::CN_ERROR,
			msg);
	} else {
		cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, "Successfully created UDP server thread");
	}
}

void* CommNode::handleUDP() {
	socklen_t fromLen = sizeof adr;
	cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, "inside handleUDP");
	while (running) {
		cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, "inside while loop");
		int ret = recvfrom(udpSock, udpDgram, sizeof udpDgram, 0, (struct sockaddr*)&adr, &fromLen);
		cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, "got message on socket");
		if (ret < 0) {
			char msg[256];
			sprintf(msg, "Error receiving UDP packet: %s", std::strerror(errno));
			cnLog->writeMessage(CommNodeLog::severities::CN_ERROR, msg);
			return NULL;
		}

		cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, "Recieved a message: ");
	}	
}

void CommNode::sendHeartbeat() {
}

void CommNode::update() {
	cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, "Message sent!");
}

void CommNode::setUpUDPSocket() {
	udpSock = socket(AF_INET, SOCK_DGRAM, 0);
	if (udpSock == -1) {
		char errMsg[256];
		sprintf(errMsg, "Error opening UDP socket: %s", std::strerror(errno));
		cnLog->writeMessage(CommNodeLog::severities::CN_ERROR, errMsg);
	}
	len_inet = sizeof adr;

	int ret = mkaddr(&adr, &len_inet, const_cast<char*>(bc_addr.c_str()), const_cast<char*>("udp"));
	
	if (ret == -1) {
		char errMsg[256];
		sprintf(errMsg, "Error setting broadcast address: %s", std::strerror(errno));
		cnLog->writeMessage(CommNodeLog::severities::CN_ERROR, errMsg);
	}

	ret = setsockopt(udpSock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr));

	if (ret == -1) {
		char errMsg[256];
		sprintf(errMsg, "Error setting socket options: %s", std::strerror(errno));
		cnLog->writeMessage(CommNodeLog::severities::CN_ERROR, errMsg);
	}

	ret = bind(udpSock, (struct sockaddr*)&adr, len_inet);

	if (ret == -1) {
		char errMsg[256];
		sprintf(errMsg, "Error binding socket to broadcast address: %s", std::strerror(errno));
		cnLog->writeMessage(CommNodeLog::severities::CN_ERROR, errMsg);
	}
}
