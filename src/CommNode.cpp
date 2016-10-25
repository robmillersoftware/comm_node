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
	udpPortNumber = port;
	uuid = boost::uuids::random_generator()();
	
	initBroadcastListener();
	initBroadcastServer();
	initTCPListener();
}

/*
 * Sets the state to running and engages the UDP server thread.
 */
void CommNode::start() {
	running = true;
	startBroadcastListener();
	startTCPListener();
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
		sprintf(msg, "Error creating broadcast thread: %d", ret);
		cnLog->error(msg);
	}
}

/**
 * This launches the TCP listener on another thread. Neighbor nodes will connect
 * to this socket
 */
void CommNode::startTCPListener() {
	int ret = pthread_create(&tcpThread, NULL, &CommNode::handleTCP, this);	

	//If anything besides 0 is returned from pthread_create, then there was an error
	if (ret) {
		char msg[512];
		sprintf(msg, "Error creating TCP thread: %d", ret);
		cnLog->error(msg);
	}
}

/**
 * Formats neighbor information for printing and writes it to a file.
 */
void CommNode::printNeighbors() {
	std::stringstream ss;

	ss << "          NEIGHBOR UUID          " << "|" << 
		"       ADDRESS       " << "|" << "LATENCY" << "|" << "BANDWIDTH" << endl;
	ss << "------------------------------------------------------------------------"
		<< endl;

	for (auto it : neighbors) {
		ss << boost::uuids::to_string(it.second.uuid) << "|" << 
			it.second.ip << ":" << it.second.port << "|" << endl;
	}

	std::string filename = string(PARENT_DIRECTORY) + "/nodestatus_" + boost::uuids::to_string(uuid) + ".txt";
	std::ofstream out;
	out.open(filename, std::ofstream::out | std::ofstream::trunc);

	out << ss.rdbuf();

	out.close();
}

/**
 * This function is called when the TCP listener is started
 */
void* CommNode::handleTCP() {
	sockaddr_in neighborAddr;
	unsigned int neighborLen = sizeof neighborAddr;

	listen(tcpListenerFD, 10);

	while(running) {
		int newSock = accept(tcpListenerFD, (sockaddr*)&neighborAddr, &neighborLen);
		if (newSock < 0) {
			cnLog->error("Unable to accept TCP connection");
		}

		//Write to remote node with the get command specifying uuid
		std::string msg("get uuid");
		write(newSock, msg.c_str(), sizeof msg.c_str());

		//The neighbor responds with their UUID
		char reply[boost::uuids::uuid::static_size()];
		read(newSock, &reply, boost::uuids::uuid::static_size());

		std::string replyStr(reply);
		boost::algorithm::trim(replyStr);
		
		boost::uuids::uuid neighborId = boost::lexical_cast<boost::uuids::uuid>(replyStr);

	  if (neighbors.count(neighborId) != 0) {
			neighbors[neighborId].socketFD = newSock;
			cnLog->debug("TCP Socket connected to neighbor:" + replyStr);
		}
	}
}

/**
 * This function is called by pthread_create when the UDP listener is started
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

		std::string broadcastMsg(udpDgram);
		std::vector<std::string> splitStrs;
		boost::split(splitStrs, broadcastMsg, boost::is_any_of("\t "));

		if (splitStrs.size() < 3) {
			cnLog->error("Malformed broadcast message, too few arguments");
		} else {	
			if (splitStrs[0] == "add") {
				std::string neighbor = splitStrs[1];
				boost::algorithm::trim(neighbor);

				std::string myself = boost::uuids::to_string(uuid);

				//The received packet should have the UUID of the originating node. Make sure
				//it isn't coming from this one
				if (myself.compare(neighbor)) {
					char rcvd[1024];
					char ip[INET_ADDRSTRLEN];

					inet_ntop(AF_INET, &(broadcastAddr.sin_addr), ip, INET_ADDRSTRLEN);

					NeighborInfo n;
					n.uuid = boost::lexical_cast<boost::uuids::uuid>(neighbor);
					n.ip = std::string(ip);
					n.port = atoi(splitStrs[2].c_str());
							
					addNeighbor(n);
				}
  		}
		}
	}	
}

void CommNode::addNeighbor(NeighborInfo n) {
	//We only want to do anything if the neighbor isn't already in the list
	if (neighbors.count(n.uuid) == 0) {
		neighbors[n.uuid] = n;

		cnLog->debug("Added neighbor " + boost::uuids::to_string(n.uuid));

		n.socketFD = socket(AF_INET, SOCK_STREAM, 0);
	
		if (n.socketFD < 0) {
			cnLog->debug("Unable to open socket");
		}

		sockaddr_in neighborAddr;
		int neighborLen = sizeof neighborAddr;
	
		memset(&neighborAddr, 0, neighborLen);
		neighborAddr.sin_family = AF_INET;
  
		inet_pton(AF_INET, n.ip.c_str(), &(neighborAddr.sin_addr));
		neighborAddr.sin_port = n.port;
	
		int res = connect(n.socketFD, (const sockaddr*)&neighborAddr, neighborLen);
		if (res < 0) {
			cnLog->debug(n.ip + ":" + std::to_string(n.port));
			cnLog->error("Error connecting to TCP socket: " + std::string(strerror(errno)));
		} else {
			cnLog->debug("Connected to remote host at " + n.ip + ":" + 
				std::to_string(n.port));
		}
	}
}

void CommNode::sendHeartbeat() {

	char buff[512];
	sprintf(buff, "add %s %d", boost::uuids::to_string(uuid).c_str(), tcpAddr.sin_port);
	sendto(udpBroadcastFD, buff, strlen(buff), 0, (struct sockaddr*)&broadcastAddr, broadcastLen);
}

/**
 * Gathers information about neighboring nodes
 */
void CommNode::runMetrics() {
}

/**
 * Sends a heartbeat and runs various upkeep code
 */
void CommNode::update() {
	sendHeartbeat();
	cnLog->debug("Still alive...");
	runMetrics();
	printNeighbors();
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
	listenerAddr.sin_port = htons(udpPortNumber);

	int ret = bind(udpListenerFD, (struct sockaddr*)&listenerAddr, listenerLen);

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
	broadcastAddr.sin_port = htons(udpPortNumber);

	//For the socket options that take an enable/disable int value
	int enable = 1;

	int ret = setsockopt(udpBroadcastFD, SOL_SOCKET, SO_BROADCAST, &enable, sizeof enable);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error setting socket reuseaddr option");
	}

/*	ret = connect(udpBroadcastFD, (struct sockaddr*)&broadcastAddr, broadcastLen);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error connecting socket to broadcast address");
	}
*/	
}

void CommNode::initTCPListener() {
	tcpListenerFD = socket(AF_INET, SOCK_STREAM, 0);

	if (tcpListenerFD == -1) {
		cnLog->exitWithErrorMessage("Unable to create TCP socket file descriptor");
	}

	tcpLen = sizeof tcpAddr;
	
	memset(&tcpAddr, 0, tcpLen);
	tcpAddr.sin_family = AF_INET;
	tcpAddr.sin_addr.s_addr = INADDR_ANY;
	listenerAddr.sin_port = 0;

	int ret = bind(tcpListenerFD, (struct sockaddr*)&tcpAddr, tcpLen);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error binding socket to listener address");
	}

	ret = getsockname(tcpListenerFD, (struct sockaddr*)&tcpAddr, &tcpLen);

	if (ret == -1) {
		cnLog->exitWithErrorMessage("Error getting socket address");
	}
}
