#include "CommNode.h"
#include <boost/uuid/uuid_io.hpp>
#include "CommNodeLog.h"
#include <chrono>
#include <ctime>

//This external variable holds the instance to the logger used by all files
extern CommNodeLog* cnLog;

//Helper functions. Implementation at bottom of file
static in_addr_t getBroadcastIp();
static bool fromLocalMachine(std::string ip);

const char* CommNode::NO_RESPONSE = "done";

/*
 * Set member variables and initialize the three main sockets we'll be using
 */
CommNode::CommNode(boost::uuids::uuid id, int port) {
	neighbors = new map<std::string, NeighborInfo*>();
	localNeighbors = new map<std::string, NeighborInfo*>();

	udpPortNumber = port;
	uuid = id; 
	
	initBroadcastListener();
	initBroadcastServer();
	initTCPListener();
}

/*
 * Sets the state to running and starts the listener threads.
 */
void CommNode::start() {
	running = true;

	//We only want to start the udp listener if we sucessfully bound
	//the listener socket
	if (isListening) {
		startBroadcastListener();
	}

	startTCPListener();
}

/*
 * Turns off the running state which causes the application to close
 */
void CommNode::stop() {
	running = false;
}

/**
 * Sends a heartbeat and runs various upkeep code
 */
void CommNode::update() {
	sendHeartbeat();
	//runMetrics();
	printNeighbors();
	cnLog->debug("Still alive..." + std::to_string(neighbors->size()) + " " + 
		std::to_string(localNeighbors->size()));
}

/**
 * Sets up a socket that handles incoming UDP messages on the specified port.
 * These messages will be coming from the LAN broadcast IP
 */
void CommNode::initBroadcastListener() {
	addrinfo hints, *resInfo;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = IPPROTO_UDP;

	char port[5];
	sprintf(port, "%d", udpPortNumber);
	
	int res = getaddrinfo(NULL, port, &hints, &resInfo);
	if (res != 0)
		cnLog->exitWithError("Error getting UDP addr info: " +
			std::string(gai_strerror(res)));

	udpListenerFD = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);
	if (udpListenerFD < 0)
		cnLog->exitWithError("Unable to create UDP socket file descriptor");

	int ret = bind(udpListenerFD, resInfo->ai_addr, resInfo->ai_addrlen);
	if (ret < 0) {
		if (errno == EADDRINUSE) {
			//Ignore this error. It most likely means that another CN is already 
			//listening for broadcasts. It will forward messages to this node. 
			cnLog->debug("Unable to bind to local port, waiting for master");
			isListening = false;
		} else {
			cnLog->exitWithError("Error binding to local port " + 
				std::to_string(udpPortNumber));
		}	
	} else {
		//Bind was successful, set flag to show we are listening
		isListening = true;
	}
	freeaddrinfo(resInfo);
}

/**
 * Initializes the broadcast socket FD and creates the broadcast address object.
 * On a fixed interval, this server will broadcast a UDP packet on a specified
 * port number.
 */
void CommNode::initBroadcastServer() {
	addrinfo hints, *resInfo;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	
	char ipStr[INET_ADDRSTRLEN];
	in_addr_t brd = getBroadcastIp();
	inet_ntop(hints.ai_family, &brd, ipStr, INET_ADDRSTRLEN);
	
	int res = getaddrinfo(ipStr, std::to_string(udpPortNumber).c_str(), 
		&hints, &resInfo);
	if (res != 0)
		cnLog->exitWithError("Error getting UDP addr info: " +
			std::string(gai_strerror(res)));
	
	udpBroadcastFD = socket(resInfo->ai_family, resInfo->ai_socktype, 
		resInfo->ai_protocol);
	if (udpBroadcastFD < 0)
		cnLog->exitWithError("Unable to create UDP socket file descriptor");

	int enable = 1;
	int ret = setsockopt(udpBroadcastFD, SOL_SOCKET, SO_BROADCAST, 
		&enable, sizeof enable);
	if (ret < 0)
		cnLog->exitWithError("Error setting options for broadcast socket");

	//Saving this sockaddr for later so we don't have to look it up again
	broadcastLen = sizeof broadcastAddr;
	memset(&broadcastAddr, 0, broadcastLen);
	broadcastAddr.sin_family = resInfo->ai_family;
	broadcastAddr.sin_addr = ((sockaddr_in*)resInfo->ai_addr)->
		sin_addr;
	broadcastAddr.sin_port = ((sockaddr_in*)resInfo->ai_addr)->sin_port;
	freeaddrinfo(resInfo);
}

/**
 * This function creates a new POSIX thread where this CN will be listening
 * for heartbeat messages from other nodes.
 */
void CommNode::startBroadcastListener() {
	int ret = pthread_create(&listenerThread, NULL, &CommNode::handleBroadcast, 
		this);	
	if (ret) 
		cnLog->exitWithError("Error creating broadcast thread");
}

/**
 * This function is called by pthread_create when the UDP listener is started
 * It starts a loop that receives UDP data
 */
void* CommNode::handleBroadcast() {
	cnLog->debug("Listening for UDP messages on port " + 
		std::to_string(udpPortNumber));
	
	while (running) {
		memset(udpDgram, 0, DGRAM_SIZE);
		
		sockaddr_in origin;
		unsigned int originSize = sizeof origin;

		int ret = recvfrom(udpListenerFD, udpDgram, DGRAM_SIZE, 0, 
			(sockaddr*)&origin, &originSize);
		if (ret < 0)
			cnLog->exitWithError("Error receiving UDP packet");

		//Before doing any processing, forward the message
		forwardToLocalNeighbors(udpDgram, DGRAM_SIZE);

		//The format for broadcast dgrams is "command args1 arg2 .. argn"
		std::string broadcastMsg(udpDgram);
		std::vector<std::string> splitStrs;
		boost::split(splitStrs, broadcastMsg, boost::is_any_of("\t "));

		if (splitStrs.size() < 2) {
			cnLog->error("Malformed broadcast message, too few arguments");
		} else {	
			//Message format should be "add uuid tcpport"
			if (splitStrs[0] == "add" && splitStrs.size() >= 3) {
				std::string neighbor = splitStrs[1];
				boost::algorithm::trim(neighbor);

				std::string myself = boost::uuids::to_string(uuid);
		
				//Ignore messages originating from this node
				if (myself.compare(neighbor) == 0) {
					continue;
				}

				if (neighbors->count(neighbor) == 0) {
					char ip[INET_ADDRSTRLEN];
					inet_ntop(AF_INET, &(origin.sin_addr), ip, INET_ADDRSTRLEN);

					int portNum;
					std::stringstream convert(splitStrs[2]);
					convert >> portNum;

					addNeighbor(neighbor, std::string(ip), portNum);
				}
			}
		}
	}
	return NULL;
}

void CommNode::addNeighbor(std::string id, std::string ip, int port) {
	NeighborInfo *n = new NeighborInfo();
	n->uuid = id;
	n->ip = ip;
	n->port = port;
	auto res = neighbors->insert(
		std::pair<std::string, NeighborInfo*>(id, n));
	if (!res.second) {
		cnLog->exitWithError("Error inserting into map");
	}
	
	// See if this neighbor is running on our local machine, if 
	// so add them to the localNeighbors map
	bool fromLocal = fromLocalMachine(n->ip);
	int cnt = localNeighbors->count(id);

	if (fromLocal && cnt == 0) {
		res = localNeighbors->insert(
			std::pair<std::string, NeighborInfo*>(id, n));
		if (!res.second) {
			cnLog->exitWithError("Unable to add to localNeighbors");
		}
	}
	
	cnLog->debug("Added neighbor " + n->uuid + " at address " + 
		n->ip + ":" + std::to_string(n->port));
	
	if (n->socketFD == -1)
		connectToNeighbor(n);
	
	n = NULL;
}

/**
 * Sends a UDP packet to the broadcast address
 */
void CommNode::sendHeartbeat() {
	char buff[DGRAM_SIZE];
	memset(buff, 0, DGRAM_SIZE);
	sprintf(buff, "add %s %d", boost::uuids::to_string(uuid).c_str(), 
		tcpPortNumber);
	
	int ret = sendto(udpBroadcastFD, buff, DGRAM_SIZE, 0, 
		(sockaddr*)&broadcastAddr, broadcastLen);
	if (ret < 0) {
		cnLog->exitWithError("Error sending to broadcast socket");
	}
}

/**
 * This function creates a POSIX thread where the TCP 
 */
void CommNode::startTCPListener() {
	int ret = pthread_create(&tcpThread, NULL, &CommNode::handleTCP, this);	
	if (ret)
		cnLog->exitWithError("Error creating TCP thread");
}

//I trust everything above this line
//I'm kind of meh about everything below this line

/**
 * Initializes a TCP socket and binds it to a random port. This socket will
 * listen for connect() attempts and accept them
 */
void CommNode::initTCPListener() {
	addrinfo hints, *resInfo;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = IPPROTO_TCP;
	
	int ret = getaddrinfo(NULL, "0", &hints, &resInfo);
	if (ret != 0)
		cnLog->exitWithError("Error getting TCP addr info: " +
			std::string(gai_strerror(ret)));

	tcpListenerFD = socket(resInfo->ai_family, resInfo->ai_socktype, 
		resInfo->ai_protocol);
	if (tcpListenerFD < 0) {
		cnLog->exitWithError("Unable to create TCP socket file descriptor");
	}

	int enable = 1;
	ret = setsockopt(tcpListenerFD, SOL_SOCKET, SO_REUSEADDR, 
		&enable, sizeof enable);
	if (ret < 0) {
		cnLog->exitWithError(
			"Error setting socket options for TCP listener");
	}

	ret = ioctl(tcpListenerFD, FIONBIO, (char*)&enable);
	if (ret < 0) {
		cnLog->exitWithError("Error making TCP socket non-blocking");
	}
	
	ret = bind(tcpListenerFD, resInfo->ai_addr, resInfo->ai_addrlen);
	if (ret < 0) {
		cnLog->exitWithError("Error binding socket to listener address");
	}

	sockaddr_in temp;
  unsigned int l = sizeof temp;
	if (getsockname(tcpListenerFD, (sockaddr*)&temp, &l) == -1) {
		cnLog->exitWithError("Error getting socket details");
	}

	((sockaddr_in*)resInfo->ai_addr)->sin_port = temp.sin_port;

	ret = listen(tcpListenerFD, 10);

	if (ret < 0) {
		cnLog->exitWithError("Unable to listen on TCP socket " +
			std::to_string(tcpListenerFD));
	}

	tcpPortNumber = ntohs(((sockaddr_in*)resInfo->ai_addr)->sin_port);
	freeaddrinfo(resInfo);
}

/**
 * Adds other nodes to the neighbors map. Adds nodes running on the 
 * local machine to the local neighbors map. Also sets up TCP sockets to
 * each neighbor
 */
void CommNode::connectToNeighbor(NeighborInfo *n) {
	addrinfo hints, *resInfo;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = 0;
	
	int res = getaddrinfo(n->ip.c_str(), std::to_string(n->port).c_str(), 
		&hints, &resInfo);
	if (res != 0)
		cnLog->exitWithError("Error getting TCP addr info: " +
			std::string(gai_strerror(res)));
	
	n->socketFD = socket(resInfo->ai_family, resInfo->ai_socktype, 
		resInfo->ai_protocol);
	if (n->socketFD < 0) {
		cnLog->error("Unable to open socket");
	}
	
	int enable = 1;
	res = ioctl(n->socketFD, FIONBIO, (char*)&enable);
	if (res < 0) {
		cnLog->exitWithError("Error making TCP socket non-blocking");
	}

	res = connect(n->socketFD, resInfo->ai_addr, resInfo->ai_addrlen);
	if (res < 0) {
		if (errno != EINPROGRESS)
			cnLog->error("Error connecting to TCP socket: ");
	}

	pollfd poll = { n->socketFD, POLLIN | POLLPRI, 0 };
	fds.push_back(poll);
}

/**
 * Polls all of this node's TCP sockets and handles incoming messages
 */
void* CommNode::handleTCP() {
	pollfd newPoll = { tcpListenerFD, POLLIN | POLLPRI, 0 };
	fds.push_back(newPoll);
	
	cnLog->debug("Listening for TCP connections with socket " + 
		std::to_string(tcpListenerFD) + " on port number: " + 
		std::to_string(tcpPortNumber));

	while(running) {
		int ret = poll(&fds[0], fds.size(), 1000);
		if (ret < 0) {
			cnLog->exitWithError("Error while polling");
		} else if (ret == 0) {
			//poll timed out, try again
			continue;
		} else {
			for (unsigned int i = 0; i < fds.size(); ++i) {
				if (fds[i].revents == 0 || errno == EINPROGRESS) continue;
				if (fds[i].revents != POLLIN && 
						fds[i].revents != POLLPRI)
					cnLog->exitWithError("Received unexpected poll event");

				if (fds[i].fd == (unsigned int)tcpListenerFD) {
					cnLog->debug("GOT REQUEST FOR NEW TCP CONNECTION");
					sockaddr_in newNeighbor;
					unsigned int newNeighborLen = sizeof newNeighbor;
					int newSock = accept(tcpListenerFD, (sockaddr*)&newNeighbor, 
					 &newNeighborLen);
					if (newSock < 0) {
						if (errno == EAGAIN || errno == EWOULDBLOCK) {
							continue;
						}
						cnLog->exitWithError("Unable to accept TCP connection");
					}
				
					pollfd newFD = { newSock, POLLIN | POLLPRI, 0 };
					fds.push_back(newFD);
					
					//Write to remote node with the get command specifying uuid
					std::string msg("get uuid");
					cnLog->debug("WRITING GET UUID REQUEST");
					ret = write(newSock, msg.c_str(), DGRAM_SIZE);
					if (ret < 0) {
						cnLog->debug("Error writing to socket number " + 
							std::to_string(newSock));
					}
					cnLog->debug("WROTE " + std::to_string(ret) + " BYTES");
  			} else {
					char buffer[DGRAM_SIZE];
					int nbytes = read(fds[i].fd, &buffer, DGRAM_SIZE);
					cnLog->debug("READ SOMETHING: " + std::string(buffer));
					//Bytes received less than or equal to 0. Either the client hung up
					//or there was an error
					if (nbytes <= 0) {
						if (nbytes == 0) {
							cnLog->debug("Socket hung up: " + std::to_string(fds[i].fd));
						} else {
							cnLog->error("Error reading from socket " + 
								std::to_string(fds[i].fd));
						}
						close(fds[i].fd);
						fds.erase(fds.begin() + i);
					} else {
						std::string resp = createTCPResponse(fds[i].fd, buffer, DGRAM_SIZE);
						if (resp.compare(std::string(NO_RESPONSE))) {
							int ret = write(fds[i].fd, resp.c_str(), DGRAM_SIZE);
							if (ret < 0)
								cnLog->exitWithError("Error writing TCP response");
						}
					}
				}
			}
		}
	}

	return NULL;
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

	for (auto it : *neighbors) {
		ss << it.second->uuid << "|" << 
			it.second->ip << ":" << it.second->port << "|" << it.second->latency << 
			"|" << it.second->bandwidth << endl;
	}

	std::string filename = std::string(getenv("INSTALL_DIRECTORY")) + 
		"/nodestatus_" + boost::uuids::to_string(uuid) + ".txt";
	std::ofstream out;

	out.open(filename, std::ofstream::out | std::ofstream::trunc);
	out << ss.rdbuf();
	out.close();
}

/**
 * This method forwards the given string to all local neighbors by default. 
 * If an id is passed, then the message is only forwarded to that CN
 */
void CommNode::forwardToLocalNeighbors(char* msg, unsigned long int sz, 
		std::string id) {
	if (id.length() != 0) {
		write((*localNeighbors)[id]->socketFD, msg, sz);
	} else {
		for (auto it : *localNeighbors) {
			cnLog->debug("FORWARDING MESSAGE");
			int ret = write(it.second->socketFD, msg, sz);
			if (ret < 0) {
				cnLog->exitWithError("Unable to write to socket: " + 
					std::to_string(it.second->socketFD));
			}
		}
	}
}

/**
 * Helper function that handles parsing a TCP recv string
 */
std::string CommNode::createTCPResponse(int sockFD, char* buf, 
		long unsigned int sz) {
	std::string str(buf);
	std::vector<std::string> splits;

	boost::split(splits, str, boost::is_any_of("\t "));

	if (splits[0] == "ping") {
		return "pong " + splits[1];
	} else if (splits[0] == "pong") {
		auto stop = std::chrono::system_clock::now();
		typedef std::chrono::system_clock::period period_t;
		auto dur = stop.time_since_epoch();

		//Get time difference in milliseconds and store it in that neighbor's 
		//latency info
		long long int start = atoi(splits[1].c_str());

		for (auto& it : *neighbors) {
			if (it.second->socketFD == sockFD) {
				it.second->latency = dur.count() - start;
	
				float scalar;
				if (it.second->latency == 0) {
					scalar = 0.0f;
				} else {
					scalar = 1.0f / (float)(it.second->latency);
				}
				float bandwidth = (float)DGRAM_SIZE * scalar;
				it.second->bandwidth = bandwidth;
				cnLog->debug("SUCCESS " + std::to_string(it.second->latency) + " " +
					std::to_string(bandwidth));
				return NO_RESPONSE;
			}
		}
		cnLog->debug("Unable to find neighbor with socketFD = " + 
			std::to_string(sockFD));
		return NO_RESPONSE;
	} else if (splits[0] == "get") {
		if (splits[1] == "uuid") {
			return "uuid " + boost::uuids::to_string(uuid);
		}
	} else if (splits[0] == "uuid") {
		auto it = neighbors->find(splits[1]);
		if (it != neighbors->end()) {
			it->second->socketFD = sockFD;
		} else {
			sockaddr_in peer;
			unsigned int peerLen = sizeof peer;
			getpeername(sockFD, (sockaddr*)&peer, &peerLen);
			
			char ip[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &(peer.sin_addr.s_addr), ip, INET_ADDRSTRLEN);
			int port = ntohs(peer.sin_port);

			addNeighbor(splits[1], std::string(ip), port);
		}
		
		return NO_RESPONSE;
	} else if (splits[0] == "add") {
		cnLog->debug("Received request from master to add neighbor: " + 
			splits[1]);

		sockaddr_in peer;
		unsigned int peerLen = sizeof peer;
		getpeername(sockFD, (sockaddr*)&peer, &peerLen);
		
		std::string neighbor = splits[1];
		boost::algorithm::trim(neighbor);

		std::string myself = boost::uuids::to_string(uuid);

		//The received packet should have the UUID of the originating node. 
		//Make sure it isn't coming from this one
		if (myself.compare(neighbor) && neighbors->count(neighbor) == 0) {
			char ip[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &(peer.sin_addr), ip, INET_ADDRSTRLEN);
							
			addNeighbor(neighbor, std::string(ip), atoi(splits[2].c_str()));
		}
		return NO_RESPONSE;
	}
	cnLog->debug("Invalid TCP request: " + str);
	return NO_RESPONSE;
}


/**
 * Gathers information about neighboring nodes
 */
void CommNode::runMetrics() {
	using namespace boost::posix_time;

	//Run metrics on each neighbor
	for (auto& it : *neighbors) {
		auto stop = std::chrono::system_clock::now();
		typedef std::chrono::system_clock::period period_t;
		auto dur = stop.time_since_epoch();
	
		//Write a short message to the neighbor's TCP socket and await response
		char msg[128];
		sprintf(msg, "%s %ld", "ping", dur.count());
		cnLog->debug("WRITING PING: " + std::string(msg));
		int bytes = write(it.second->socketFD, msg, DGRAM_SIZE);
		if (bytes <= 0)
			cnLog->exitWithError("Unable to write to socket for metrics");
	}
}

/** 
 * Gets LAN broadcast IP from ifaddrs
 */
static in_addr_t getBroadcastIp() {
	ifaddrs* allAddrs = NULL;
	getifaddrs(&allAddrs);

	for (ifaddrs* it = allAddrs; it != NULL; it = it->ifa_next) {
		//If the ifaddr isn't IPv4 or if 
		//it is the loopback interface then continue
		if (it->ifa_addr->sa_family != AF_INET ||
				strcmp(it->ifa_name, "lo") == 0)
			continue;

		sockaddr_in* brd = (sockaddr_in*)(it->ifa_ifu.ifu_broadaddr);
		freeifaddrs(allAddrs);
		
		return brd->sin_addr.s_addr;
	}
	
	if (allAddrs != NULL)
		freeifaddrs(allAddrs);
	return 0;
}

static bool fromLocalMachine(std::string ip) {
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
