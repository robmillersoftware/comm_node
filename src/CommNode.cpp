#include "CommNode.h"
#include <boost/uuid/uuid_io.hpp>
#include <boost/timer/timer.hpp>
#include "CommNodeLog.h"
#include <ifaddrs.h>
#include <boost/algorithm/string.hpp>
#include <sys/poll.h>

//This external variable holds the instance to the logger used by all files
extern CommNodeLog* cnLog;

/** 
 * Local helper function for getting the broadcast IP address based on local 
 * IP and subnet mask
 */
static in_addr_t getBroadcastIp() {
	ifaddrs* allAddrs = NULL;
	getifaddrs(&allAddrs);

	for (ifaddrs* it = allAddrs; it != NULL; it = it->ifa_next) {
		if (it->ifa_ifu.ifu_broadaddr == NULL) continue;

	  char rtn[INET_ADDRSTRLEN];
		memset(rtn, 0, INET_ADDRSTRLEN);
 		const void* rtnPtr = inet_ntop(AF_INET, 
			&(((sockaddr_in*)it->ifa_addr)->sin_addr), rtn, INET_ADDRSTRLEN);

		if (rtnPtr == NULL)
			cnLog->exitWithError("Unable retrieve IP string from ifaddrs");
		
		return ((sockaddr_in*)it->ifa_addr)->sin_addr.s_addr;
	}
	if (allAddrs != NULL)
		freeifaddrs(allAddrs);
	return 0;
}

/*
 * This is the only constructor used by this application
 */
CommNode::CommNode(boost::uuids::uuid id, int port) {
	udpPortNumber = port;
	uuid = id; 
	
	initBroadcastListener();
	initBroadcastServer();
	initTCPListener();
}

/**
 * Sets up a socket that handles incoming UDP messages on the specified port.
 * These messages will be coming from the LAN broadcast IP
 */
void CommNode::initBroadcastListener() {
	udpListenerFD = socket(AF_INET, SOCK_DGRAM, 0);

	if (udpListenerFD < 0) {
		cnLog->exitWithError("Unable to create UDP socket file descriptor");
	}

	listenerLen = sizeof listenerAddr;
	
	memset(&listenerAddr, 0, listenerLen);
	listenerAddr.sin_family = AF_INET;
	listenerAddr.sin_addr.s_addr = INADDR_ANY;
	listenerAddr.sin_port = htons(udpPortNumber);

	int ret = bind(udpListenerFD, (sockaddr*)&listenerAddr, listenerLen);
	if (ret < 0) {
		if (errno == EADDRINUSE) {
			//Ignore this error. It most likely means that another CN is already 
			//listening for broadcasts. It will forward messages to this node. 
			isListening = false;
		} else {
			cnLog->exitWithError("Error binding to local port " + 
				std::to_string(udpPortNumber));
		}	
	} else {
		cnLog->debug("Listening for UDP messages on port " + 
			std::to_string(udpPortNumber));
		isListening = true;
	}
}

/**
 * Initializes the broadcast socket FD and creates the broadcast address object.
 * On a fixed interval, this server will broadcast a UDP packet on a specified
 * port number.
 */
void CommNode::initBroadcastServer() {
	udpBroadcastFD = socket(AF_INET, SOCK_DGRAM, 0);
	if (udpBroadcastFD < 0) {
		cnLog->exitWithError("Unable to create UDP socket file descriptor");
	}

	int enable = 1;
	int ret = setsockopt(udpBroadcastFD, SOL_SOCKET, SO_BROADCAST, 
		&enable, sizeof enable);
	if (ret < 0) {
		cnLog->exitWithError("Error setting options for broadcast socket");
	}

	broadcastLen = sizeof broadcastAddr;
	memset(&broadcastAddr, 0, broadcastLen);
	broadcastAddr.sin_family = AF_INET;
	broadcastAddr.sin_addr.s_addr = getBroadcastIp();

	char ipstr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(broadcastAddr.sin_addr), ipstr, INET_ADDRSTRLEN);
	cnLog->debug("SENDING BROADCAST TO " + std::string(ipstr));
	if (broadcastAddr.sin_addr.s_addr == 0) {
		cnLog->exitWithError("Unable to find broadcast IP address");
	}

	broadcastAddr.sin_port = htons(udpPortNumber);
}

/**
 * Initializes a TCP socket and binds it to a random port. This socket will
 * listen for connect() attempts and accept them
 */
void CommNode::initTCPListener() {
	tcpListenerFD = socket(AF_INET, SOCK_STREAM, 0);
	if (tcpListenerFD < 0) {
		cnLog->exitWithError("Unable to create TCP socket file descriptor");
	}

	tcpLen = sizeof tcpAddr;
	memset(&tcpAddr, 0, tcpLen);
	tcpAddr.sin_family = AF_INET;
	tcpAddr.sin_addr.s_addr = INADDR_ANY;
	tcpAddr.sin_port = 0;

	int enable = 1;
	int ret = setsockopt(tcpListenerFD, SOL_SOCKET, SO_REUSEADDR, 
		&enable, sizeof enable);
	if (ret < 0) {
		cnLog->exitWithError(
			"Error setting socket options for TCP listener");
	}

	ret = bind(tcpListenerFD, (sockaddr*)&tcpAddr, tcpLen);
	if (ret < 0) {
		cnLog->exitWithError("Error binding socket to listener address");
	}

	ret = getsockname(tcpListenerFD, (sockaddr*)&tcpAddr, &tcpLen);
	if (ret < 0) {
		cnLog->exitWithError("Error getting socket address");
	}

	ret = listen(tcpListenerFD, 10);
	if (ret < 0) {
		cnLog->exitWithError("Unable to listen on TCP socket " +
			std::to_string(tcpListenerFD));
	}

	cnLog->debug("Listening for TCP connections on port number: " + 
		std::to_string(ntohs(tcpAddr.sin_port)));
}

/*
 * Sets the state to running and starts the listener threads.
 */
void CommNode::start() {
	running = true;

	//We only want to start the broadcast listening if we sucessfully bound
	//the listener socket
	if (isListening) {
		startBroadcastListener();
	}

	startTCPListener();
}

/**
 * This function creates a new POSIX thread where the UDP server will be 
 * listening for heartbeat messages from other nodes.
 */
void CommNode::startBroadcastListener() {
	int ret = pthread_create(&listenerThread, NULL, &CommNode::handleBroadcast, this);	
	if (ret) 
		cnLog->exitWithError("Error creating broadcast thread");
}

/*
 * Turns off the running state which causes the application to close
 */
void CommNode::stop() {
	running = false;
}

/**
 * This launches the TCP listener on another thread. Neighbor nodes will connect
 * to this socket
 */
void CommNode::startTCPListener() {
	int ret = pthread_create(&tcpThread, NULL, &CommNode::handleTCP, this);	
	if (ret)
		cnLog->exitWithError("Error creating TCP thread");
}


/**
 * This function is called when the TCP listener is started. It handles reading
 * from all TCP sockets
 */
void* CommNode::handleTCP() {
	sockaddr_in neighborAddr;
	unsigned int neighborLen = sizeof neighborAddr;
	vector<pollfd> fds;
	
	pollfd tcp = { tcpListenerFD, POLLIN, 0 };
 	fds.push_back(tcp);

	while(running) {
		if (poll(&fds[0], fds.size(), -1))
			cnLog->exitWithError("Error selecting on fds");

		for (unsigned int i = 0; i < fds.size(); ++i) {
			if (fds[i].revents == 0) continue;
			if (fds[i].revents != POLLIN) 
				cnLog->exitWithError("Received unexpected poll event");

			cnLog->debug("Got a TCP message on socket: " + std::to_string(i));
			if (i == (unsigned int)tcpListenerFD) {
				int newSock = accept(tcpListenerFD, (sockaddr*)&neighborAddr, 
					&neighborLen);
				if (newSock < 0) {
						cnLog->error("Unable to accept TCP connection");
				}
				
				//Write to remote node with the get command specifying uuid
				std::string msg("get uuid");
				int ret = write(newSock, msg.c_str(), sizeof msg.c_str());
				if (ret < 0) {
					cnLog->debug("Error writing to socket number " + 
						std::to_string(newSock));
				}
					
				//The neighbor responds with their UUID
				char reply[boost::uuids::uuid::static_size()];
				ret = read(newSock, &reply, boost::uuids::uuid::static_size());
				if (ret < 0) {
					cnLog->debug("Error reading from socket number " + 
						std::to_string(newSock));
				}
					
				std::string replyStr(reply);
				boost::algorithm::trim(replyStr);
					
				boost::uuids::uuid neighborId = 
					boost::lexical_cast<boost::uuids::uuid>(replyStr);

				if (neighbors.count(neighborId) != 0) {
					neighbors[neighborId].socketFD = newSock;
					cnLog->debug("TCP Socket connected to neighbor:" + replyStr);
				}
			} else {
				char buffer[256];
				int nbytes = recv(i, &buffer, sizeof buffer, 0);

				//Bytes received less than or equal to 0. Either the client hung up
				//or there was an error
				if (nbytes <= 0) {
					if (nbytes == 0) {
						cnLog->debug("Socket hung up: " + std::to_string(i));
					} else {
						cnLog->error("Error reading from socket " + std::to_string(i));
					}
					close(i);
				} else {
					const char* resp = createTCPResponse(i, buffer, sizeof buffer);
					write(i, resp, strlen(resp));
				}
			}
		}
	}

	return NULL;
}

/**
 * Helper function that handles parsing a TCP recv string
 */
const char* CommNode::createTCPResponse(int sockFD, char* buf, 
		long unsigned int sz) {
	std::string str(buf, sz);
	std::vector<std::string> splits;

	boost::split(splits, str, boost::is_any_of("\t "));

	if (splits[0] == "ping") {
		cnLog->debug("Ping request received");
		return "pong";
	} else if (splits[0] == "get") {
		if (splits[1] == "uuid") {
			cnLog->debug("Received request for UUID");
			return boost::uuids::to_string(uuid).c_str();
		}
	} else if (splits[0] == "add") {
		sockaddr_in peer;
		unsigned int peerLen = sizeof peer;
		getpeername(sockFD, (sockaddr*)&peer, &peerLen);

		std::string neighbor = splits[1];
		boost::algorithm::trim(neighbor);

		std::string myself = boost::uuids::to_string(uuid);

		//The received packet should have the UUID of the originating node. 
		//Make sure it isn't coming from this one
		if (myself.compare(neighbor)) {
			char ip[INET_ADDRSTRLEN];

			inet_ntop(AF_INET, &(peer.sin_addr), ip, INET_ADDRSTRLEN);

			NeighborInfo n;
			n.uuid = boost::lexical_cast<boost::uuids::uuid>(neighbor);
			n.ip = std::string(ip);
			n.port = atoi(splits[2].c_str());
							
			addNeighbor(n);
		}
		return "added";
	}
	cnLog->debug("Invalid TCP request: " + str);
	return NULL;
}

/**
 * This function is called by pthread_create when the UDP listener is started
 * It starts a loop that receives UDP data
 */
void* CommNode::handleBroadcast() {
	while (running) {
		//Reset the message object
		memset(udpDgram, 0, sizeof udpDgram);
		
		//TODO: The dgrams being sent are a fixed size. To be safe, however, we 
		//could use ioctl to check for leftover data after calling recv just to 
		//make sure our messages maintain the proper format
		int ret = recvfrom(udpListenerFD, udpDgram, sizeof udpDgram, 0, 
			(sockaddr*)&broadcastAddr, &broadcastLen);
		if (ret < 0)
			cnLog->exitWithError("Error receiving UDP packet");

		cnLog->debug("GETTING SOMETHING: " + std::string(udpDgram));
		//Before doing any processing, forward the message
		forwardToLocalNeighbors(udpDgram, sizeof udpDgram);

		//The format for broadcast dgrams is "command args1 arg2 .. argn"
		std::string broadcastMsg(udpDgram);
		std::vector<std::string> splitStrs;
		boost::split(splitStrs, broadcastMsg, boost::is_any_of("\t "));

		if (splitStrs.size() < 2) {
			cnLog->error("Malformed broadcast message, too few arguments");
		} else {	
			if (splitStrs[0] == "add" && splitStrs.size() >= 3) {
				std::string neighbor = splitStrs[1];
				boost::algorithm::trim(neighbor);

				std::string myself = boost::uuids::to_string(uuid);

				//The received packet should have the UUID of the originating node. 
				//Make sure it isn't coming from this one
				if (myself.compare(neighbor)) {
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
	return NULL;
}

/**
 * This method forwards the given string to all local neighbors by default. 
 * If an id is passed, then the message is only forwarded to that CN
 */
void CommNode::forwardToLocalNeighbors(char* msg, unsigned long int sz, 
		boost::uuids::uuid id) {
	if (!id.is_nil()) {
		write(localNeighbors[id].socketFD, msg, sz);
	} else {
		for (auto it : localNeighbors) {
			cnLog->debug("Forwarding received message: " + std::string(msg) +
				" to local neighbor: " + boost::uuids::to_string(it.second.uuid));
			int ret = write(it.second.socketFD, msg, sz);
			if (ret < 0) {
				cnLog->exitWithError("Unable to write to socket: " + 
					std::to_string(it.second.socketFD));
			}
		}
	}
}

bool fromLocalMachine(std::string ip) {
	ifaddrs* allAddrs = NULL;
	getifaddrs(&allAddrs);

	for (ifaddrs* it = allAddrs; it != NULL; it = it->ifa_next) {
		if (!it->ifa_addr) {
			continue;
		}

		if (it->ifa_addr->sa_family == AF_INET) {
			in_addr_t ipNum = ((sockaddr_in*)it->ifa_addr)->sin_addr.s_addr;
			char ipStr[INET_ADDRSTRLEN];

			inet_ntop(AF_INET, &ipNum, ipStr, INET_ADDRSTRLEN);
			if (!ip.compare(string(ipStr))) {
				return true;

			}
		}
		
	}

	return false;
}

void CommNode::addNeighbor(NeighborInfo n) {
	//We only want to do anything if the neighbor isn't already in the list
	if (neighbors.count(n.uuid) == 0) {
		neighbors[n.uuid] = n;

		n.socketFD = socket(AF_INET, SOCK_STREAM, 0);
	
		if (n.socketFD < 0) {
			cnLog->debug("Unable to open socket");
		}

		sockaddr_in neighborAddr;
		int neighborLen = sizeof neighborAddr;
	
		memset(&neighborAddr, 0, neighborLen);
		neighborAddr.sin_family = AF_INET;
  
		inet_pton(AF_INET, n.ip.c_str(), &(neighborAddr.sin_addr));
		neighborAddr.sin_port = htons(n.port);
	
		int res = connect(n.socketFD, (const sockaddr*)&neighborAddr, neighborLen);
		if (res < 0) {
			cnLog->error("Error connecting to TCP socket: ");
		}
	
		cnLog->debug("Connected to neighbor at address: " + n.ip + 
			std::to_string(n.port));

		// See if this neighbor is running on our local machine, if so add them 
		// to the localNeighbors map
		if (fromLocalMachine(n.ip) && localNeighbors.count(n.uuid) == 0) {
			localNeighbors[n.uuid] = n;
		}
	}
}

void CommNode::sendHeartbeat() {

	char buff[512];
	sprintf(buff, "add %s %d", boost::uuids::to_string(uuid).c_str(), 
		ntohs(tcpAddr.sin_port));
	sendto(udpBroadcastFD, buff, strlen(buff), 0, (sockaddr*)&broadcastAddr, 
		broadcastLen);
}

/**
 * Gathers information about neighboring nodes
 */
void CommNode::runMetrics() {
	using namespace boost::posix_time;
	ptime start, stop;

	//Run metrics on each neighbor
	for (auto it : neighbors) {
		start = second_clock::local_time();
		
		//Write a short message to the neighbor's TCP socket and await response
		const char* msg = "ping";
		write(it.second.socketFD, msg, strlen(msg));
		
		char reply[strlen(msg)];
		read(it.second.socketFD, &reply, strlen(msg));
		
		stop = second_clock::local_time();

		//Get time difference in milliseconds and store it in that neighbor's 
		//latency info
		boost::posix_time::time_duration diff = stop - start;
		it.second.latency = diff.total_milliseconds();

		//reset and start boost timer
		//send payload
		//receive payload
		//stop timer
		//payload.sizeinbits / 1000 / timerseconds
		//save time to it->bandwidth
	}
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

/**
 * Formats neighbor information for printing and writes to a file.
 */
void CommNode::printNeighbors() {
	std::stringstream ss;

	ss << "          NEIGHBOR UUID          " << "|" << 
		"       ADDRESS       " << "|" << 
		"LATENCY" << "|" << 
		"BANDWIDTH" << endl << 
		"------------------------------------------------------------------------"
		<< endl;

	for (auto it : neighbors) {
		ss << boost::uuids::to_string(it.second.uuid) << "|" << 
			it.second.ip << ":" << it.second.port << "|" << it.second.latency << 
			"|" << it.second.bandwidth << endl;
	}

	std::string filename = std::string(getenv("INSTALL_DIRECTORY")) + 
		"/nodestatus_" + boost::uuids::to_string(uuid) + ".txt";
	std::ofstream out;

	out.open(filename, std::ofstream::out | std::ofstream::trunc);
	out << ss.rdbuf();
	out.close();
}
