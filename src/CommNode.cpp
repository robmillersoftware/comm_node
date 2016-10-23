#include "CommNode.h"
#include <boost/uuid/uuid_generators.hpp>
#include "CommNodeLog.h"

//This external variable holds the instance to the CommNodeLog singleton
extern CommNodeLog* cnLog;

//The mkaddr function creates a socket address from a protocol and string 
//representation
extern int mkaddr(void* addr, int* addrlen,
	char* str_addr, char* protocol);

/*
 * This is the only constructor used by this application
 */
CommNode::CommNode(int port) {
	portNumber = port;
	uuid = boost::uuids::random_generator()();
	
//By default, everything is going to go through the loopback
	listenerStr = "127.255.255.2:" + std::to_string(portNumber);
	broadcastStr = "127.0.0.*:" + std::to_string(portNumber);

	initBroadcastListener();
	initBroadcastServer();
}

/*
 * Sets the state to running and engages the UDP server thread.
 */
void CommNode::start() {
	running = true;
	startBroadcastListener();
}

/*
 * Turns off the running state and performs basic cleanup. The server thread uses 
 * this state change to stop receiving messages.
 */
void CommNode::stop() {
	running = false;
}

/**
 * This function creates a new POSIX thread where the UDP server will be listening
 * for heartbeat messages from other nodes.
 */
void CommNode::startBroadcastListener() {
	int ret = pthread_create(&listenerThread, NULL, &CommNode::handleBroadcast, this);	

	//If anything besides 0 is returned from pthread_create, then there was an error
	if (ret) {
		char msg[512];
		sprintf(msg, "Error creating POSIX thread: %d", ret);
		cnLog->error(msg);
	}
}

/**
 * This function is called by pthread_create when the UDP server is started
 */
void* CommNode::handleBroadcast() {
	socklen_t fromLen = sizeof broadcastAddr;

	while (running) {
		int ret = recvfrom(udpListenerFD, udpDgram, sizeof udpDgram, 0, (struct sockaddr*)&broadcastAddr, &fromLen);
		if (ret < 0) {
			char msg[256];
			sprintf(msg, "Error receiving UDP packet: %s", std::strerror(errno));
			cnLog->error(msg);
			running = false;
		}

		char rcvd[1024];
		sprintf(rcvd, "Received a message: %s", udpDgram);
		cnLog->debug(rcvd);
	}	
}

void CommNode::sendHeartbeat() {
	int enable = 1;
	setsockopt(udpBroadcastFD, SOL_SOCKET, SO_BROADCAST, &enable, sizeof enable);

	char buff[512];
	sprintf(buff, "hello?");
	sendto(udpBroadcastFD, buff, strlen(buff), 0, (struct sockaddr*)&listenerAddr, listenerLen);
}

void CommNode::update() {
	sendHeartbeat();
	cnLog->debug("Message sent!");
}

void CommNode::initBroadcastListener() {
	udpListenerFD = socket(AF_INET, SOCK_DGRAM, 0);

	if (udpListenerFD == -1) {
		cnLog->exitWithErrorMessage("Unable to create UDP socket file descriptor");
	}

	listenerLen = sizeof listenerAddr;

	int ret = mkaddr(&listenerAddr, &listenerLen, const_cast<char*>(listenerStr.c_str()), const_cast<char*>("udp"));
	
	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error getting socket address");
	}

	//For the socket options that take an enable/disable int value
	int enable = 1;

	ret = setsockopt(udpListenerFD, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof enable);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error setting socket reuseaddr option");
	}

	ret = bind(udpListenerFD, (struct sockaddr*)&listenerAddr, listenerLen);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error binding socket to listener address");
	}
}

void CommNode::initBroadcastServer() {
	udpBroadcastFD = socket(AF_INET, SOCK_DGRAM, 0);

	if (udpBroadcastFD == -1) {
		cnLog->exitWithErrorMessage("Unable to create UDP socket file descriptor");
	}

	broadcastLen = sizeof broadcastAddr;

	int ret = mkaddr(&broadcastAddr, &broadcastLen, const_cast<char*>(broadcastStr.c_str()), const_cast<char*>("udp"));
	
	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error getting socket address");
	}

	//For the socket options that take an enable/disable int value
	int enable = 1;

	ret = setsockopt(udpBroadcastFD, SOL_SOCKET, SO_BROADCAST, &enable, sizeof enable);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error setting socket reuseaddr option");
	}

	ret = bind(udpBroadcastFD, (struct sockaddr*)&broadcastAddr, broadcastLen);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error binding socket to broadcast address");
	}	
}
