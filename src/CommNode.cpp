#include "CommNode.h"
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "CommNodeLog.h"
#include <ifaddrs.h>
#include <boost/algorithm/string.hpp>

//This external variable holds the instance to the CommNodeLog singleton
extern CommNodeLog* cnLog;

/*
 * This is the only constructor used by this application
 */
CommNode::CommNode(int port) {
	portNumber = port;
	uuid = boost::uuids::random_generator()();
	
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
	sockaddr_in from;

	while (running) {
		memset(udpDgram, 0, sizeof udpDgram);
		int ret = recvfrom(udpListenerFD, udpDgram, sizeof udpDgram, 0, (struct sockaddr*)&broadcastAddr, &fromLen);
		if (ret < 0) {
			char msg[256];
			sprintf(msg, "Error receiving UDP packet: %s", std::strerror(errno));
			cnLog->error(msg);
			running = false;
		}

		std::string neighbor(udpDgram);
		boost::algorithm::trim(neighbor);

		std::string myself = boost::uuids::to_string(uuid);

		//The received packet should have the UUID of the originating node. Make sure
		//it isn't coming from this one
		if (myself.compare(neighbor)) {
			char rcvd[1024];
			char ip[INET_ADDRSTRLEN];

			inet_ntop(AF_INET, &(broadcastAddr.sin_addr), ip, INET_ADDRSTRLEN);

			sprintf(rcvd, "Received a message from %s with ip=\"%s\"", 
				neighbor.c_str(), ip);
			cnLog->debug(rcvd);
		}
	}	
}

void CommNode::sendHeartbeat() {
	int enable = 1;
	setsockopt(udpBroadcastFD, SOL_SOCKET, SO_BROADCAST, &enable, sizeof enable);

	char buff[512];
	sprintf(buff, "%s", boost::uuids::to_string(uuid).c_str());
	sendto(udpBroadcastFD, buff, strlen(buff), 0, (struct sockaddr*)&broadcastAddr, broadcastLen);
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
	
	memset(&listenerAddr, 0, listenerLen);
	listenerAddr.sin_family = AF_INET;
	listenerAddr.sin_addr.s_addr = INADDR_ANY;
	listenerAddr.sin_port = htons(portNumber);

	//For the socket options that take an enable/disable int value
	int enable = 1;

	int ret = setsockopt(udpListenerFD, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof enable);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error setting socket reuseaddr option");
	}

	ret = bind(udpListenerFD, (struct sockaddr*)&listenerAddr, listenerLen);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error binding socket to listener address");
	}
}

//local helper function for getting the broadcast IP address based on local IP and
//subnet mask
static in_addr_t getBroadcastIp() {
	in_addr_t retVal;

	struct ifaddrs* allAddrs = NULL;
	getifaddrs(&allAddrs);

	for (struct ifaddrs* it = allAddrs; it != NULL; it = it->ifa_next) {
		if (!it->ifa_addr) {
			continue;
		}

		if (it->ifa_addr->sa_family == AF_INET) {
			retVal = ((struct sockaddr_in*)it->ifa_addr)->sin_addr.s_addr;
		}
	}

	if (allAddrs != NULL)
		freeifaddrs(allAddrs);

	cnLog->debug(std::to_string(retVal));

	return retVal;
}

void CommNode::initBroadcastServer() {
	udpBroadcastFD = socket(AF_INET, SOCK_DGRAM, 0);

	if (udpBroadcastFD == -1) {
		cnLog->exitWithErrorMessage("Unable to create UDP socket file descriptor");
	}

	broadcastLen = sizeof broadcastAddr;
	
	memset(&broadcastAddr, 0, broadcastLen);
	broadcastAddr.sin_family = AF_INET;
	broadcastAddr.sin_addr.s_addr = getBroadcastIp();
	broadcastAddr.sin_port = htons(portNumber);

	//For the socket options that take an enable/disable int value
	int enable = 1;

	int ret = setsockopt(udpBroadcastFD, SOL_SOCKET, SO_BROADCAST, &enable, sizeof enable);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error setting socket reuseaddr option");
	}

	ret = connect(udpBroadcastFD, (struct sockaddr*)&broadcastAddr, broadcastLen);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error connecting socket to broadcast address");
	}	
}
