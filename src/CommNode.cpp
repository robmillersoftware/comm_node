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

//Static variable for stopping conversations
const char* CommNode::NO_RESPONSE = "";

/**
 * Constructor
 */
CommNode::CommNode(boost::uuids::uuid id, int port) {
	neighbors = new map<std::string, NeighborInfo*>();
	localNeighbors = new map<std::string, NeighborInfo*>();

	udpPortNumber = port;
	uuid = id; 
}

/*
 * Sets the state to running and starts the listener threads.
 */
void CommNode::start() {
	running = true;
	
	initBroadcastListener();
	initBroadcastServer();
	initTCPListener();

	//We only want to start the udp listener if we sucessfully bound
	//the listener socket
	if (isListening) {
		startBroadcastListener();
	}

	startTCPListener();
}

/**
 * Stops the node and closes all connections
 */
void CommNode::stop() {
	//setting the running flag to false will stop the loops in both thread
  //handlers
	running = false;

	//Wait for both the UDP and TCP listeners to stop
	pthread_join(tcpThread, NULL);
	pthread_join(udpThread, NULL);

	//Close all sockets
	close(udpListenerFD);
	close(tcpListenerFD);
	
	for (auto it : *neighbors) {
		close(it.second->socketFD);
	}

	//Empty neighbor containers
	neighbors->clear();
	localNeighbors->clear();
}

/**
 * Sends a heartbeat and runs various upkeep code
 */
void CommNode::update() {
	sendHeartbeat();

	//Run metrics on a separate thread and wait for it to finish
	pthread_t metricsThread;
	pthread_create(&metricsThread, NULL, &runMetrics, this);
	pthread_join(metricsThread, NULL);

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
	int ret = pthread_create(&udpThread, NULL, &CommNode::handleBroadcast, 
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

					addNeighborAsync(neighbor, std::string(ip), portNum);
				}
			}
		}
	}
	return NULL;
}

/**
 * Lets us modify the transfer queue from multiple threads without overwriting
 * each other
 */
void CommNode::modifyXferQueueAsync(int fd, std::string msg) {
	std::lock_guard<std::mutex> lock(xferMutex);
	transferQueue[fd] = msg;
}

/**
 * Adds a new neighbor to the map. Mutex prevents sockets from being 
 * opened twice on accident
 */
void CommNode::addNeighborAsync(std::string id, std::string ip, int port,
		int fd) {
	std::lock_guard<std::mutex> lock(mapMutex);
	if (neighbors->count(id) != 0) return;

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

  //If the optional parameter was passed in, then we've already connected 
  //a socket	
	if (fd == -1)
		connectToNeighbor(n);
	else
		n->socketFD = fd;

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
 * This function creates a POSIX thread where the TCP listener waits for
 * neighbors to connect 
 */
void CommNode::startTCPListener() {
	int ret = pthread_create(&tcpThread, NULL, &CommNode::handleTCP, this);	
	if (ret)
		cnLog->exitWithError("Error creating TCP thread");
}

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

	//Save the port number we were bound to
	tcpPortNumber = ntohs(temp.sin_port);
	
	ret = listen(tcpListenerFD, 10);
	if (ret < 0) {
		cnLog->exitWithError("Unable to listen on TCP socket " +
			std::to_string(tcpListenerFD));
	}

	freeaddrinfo(resInfo);
}

/**
 * Creates a new TCP socket and connects it to the neighbor. Also starts 
 * a new IO thread for the socket
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

	pthread_t thread;
	std::pair<int, CommNode*> arg(n->socketFD, this);
	pthread_create(&thread, NULL, &incomingMessageHandler, &arg);
}

/**
 * Polls all of this node's TCP sockets and spawns a new message handler for
 * the new socket
 */
void* CommNode::handleTCP() {
	cnLog->debug("Listening for TCP connections with socket " + 
		std::to_string(tcpListenerFD) + " on port number: " + 
		std::to_string(tcpPortNumber));

	sockaddr_in newNeighbor;
	unsigned int newNeighborLen = sizeof newNeighbor;
	
	while (running) {
		int newSock = accept(tcpListenerFD, (sockaddr*)&newNeighbor, 
		 &newNeighborLen);
		if (newSock < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			}
			cnLog->exitWithError("Unable to accept TCP connection");
		}

		pthread_t newThread;
		std::pair<int, CommNode*> arg(newSock, this);
		pthread_create(&newThread, NULL, &incomingMessageHandler,
			&arg);
	}

	return NULL;
}

/**
 * Formats neighbor information for printing and writes to a file.
 */
void CommNode::printNeighbors() {
	std::stringstream ss;

	ss << " NEIGHBOR UUID | ADDRESS | LATENCY (ms) | BANDWIDTH (kbps)" << endl << 
		"------------------------------------------------------------------------"
		<< endl;

	for (auto it : *neighbors) {
		ss << it.second->uuid << "|" << 
			it.second->ip << ":" << it.second->port << "|" << it.second->latency << 
			"ms |" << it.second->bandwidth << "kbps"<< endl;
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
			int ret = write(it.second->socketFD, msg, sz);
			if (ret < 0) {
				cnLog->exitWithError("Unable to write to socket: " + 
					std::to_string(it.second->socketFD));
			}
		}
	}
}

/**
 * Helper function that handles parsing a TCP recv string, performs necessary
 * logic and returns an appropriate response or NO_RESPONSE if none is
 * required.
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

		//Time is currently in nanoseconds. Divide by 1 million to get millis
		long int milliDur = dur.count() / 1000000;

		//Get time difference in milliseconds and store it in that neighbor's 
		//latency info
		long int start = stol(splits[1], NULL, 0);

		for (auto& it : *neighbors) {
			if (it.second->socketFD == sockFD) {
				it.second->latency = milliDur - start;
	
				float scalar;
				if (it.second->latency == 0) {
					scalar = 0.0f;
				} else {
					scalar = 1.0f / (float)(it.second->latency);
				}
				float bandwidth = (float)DGRAM_SIZE * scalar;
				it.second->bandwidth = bandwidth;
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
		sockaddr_in peer;
		unsigned int peerLen = sizeof peer;
		getpeername(sockFD, (sockaddr*)&peer, &peerLen);
			
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(peer.sin_addr.s_addr), ip, INET_ADDRSTRLEN);
		int port = ntohs(peer.sin_port);

		addNeighborAsync(splits[1], std::string(ip), port, sockFD);
		return NO_RESPONSE;
	} else if (splits[0] == "add") {
		std::string neighbor = splits[1];
		boost::algorithm::trim(neighbor);

		std::string myself = boost::uuids::to_string(uuid);
		
		//Ignore messages originating from this node
		if (myself.compare(neighbor) == 0) {
			return NO_RESPONSE;
		}

		if (neighbors->count(neighbor) == 0) {
			char ip[INET_ADDRSTRLEN];
			sockaddr_in peer;
			unsigned int peerLen = sizeof peer;
			getpeername(sockFD, (sockaddr*)&peer, &peerLen);
			
			inet_ntop(AF_INET, &(peer.sin_addr), ip, INET_ADDRSTRLEN);

			int portNum;
			std::stringstream convert(splits[2]);
			convert >> portNum;

			addNeighborAsync(neighbor, std::string(ip), portNum, sockFD);
		}				
		return NO_RESPONSE;
	}
	cnLog->debug("Invalid TCP request: " + str);
	return NO_RESPONSE;
}


/**
 * Gathers information about neighboring nodes
 */
void* CommNode::runMetrics() {
	using namespace boost::posix_time;

	//Run metrics on each neighbor
	for (auto& it : *neighbors) {
		auto stop = std::chrono::system_clock::now();
		typedef std::chrono::system_clock::period period_t;
		auto dur = stop.time_since_epoch();

		//duration is in nanoseconds. Divide by 1 million to get millis
		long int milliDur = dur.count() / 1000000;	

		//Write a short message to the neighbor's TCP socket and await response
		char msg[128];
		sprintf(msg, "%s %ld", "ping", milliDur);
		modifyXferQueueAsync(it.second->socketFD, std::string(msg));
	}
	return NULL;
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

/**
 * Handle incoming TCP messages
 * @param i an index in the fds array of file descriptors
 */
void* CommNode::incomingMessageHandler(int i) {
	//Write to remote node with the get command specifying uuid
	std::string msg("get uuid");
	int ret = write(i, msg.c_str(), DGRAM_SIZE);
	if (ret < 0) {
		cnLog->exitWithError("Error writing to new socket number " + 
			std::to_string(i));
	}

	while(running) {
		char buffer[DGRAM_SIZE];
		int nbytes = read(i, &buffer, DGRAM_SIZE);
		//Bytes received less than or equal to 0. Either the client hung up
		//or there was an error
		if (nbytes <= 0) {
			if (nbytes == 0) {
				cnLog->debug("Socket hung up: " + std::to_string(i));
			} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			} else {
				cnLog->error("Error reading from socket " + 
					std::to_string(i));
			}
			close(i);
			break;
		} else {
			std::string resp = createTCPResponse(i, buffer, DGRAM_SIZE);
			if (resp.compare(NO_RESPONSE)) {
				nbytes = write(i, resp.c_str(), DGRAM_SIZE);
				if (nbytes < 0) {
					cnLog->error("Error writing to socket ");
				}
			}

			if (transferQueue[i].compare(NO_RESPONSE)) {
				int bytes_available;
				ioctl(i, FIONREAD, &bytes_available);
				nbytes = write(i, transferQueue[i].c_str(), DGRAM_SIZE);
				if (nbytes < 0) {
					cnLog->error("Error writing queued message");
				}
				modifyXferQueueAsync(i, NO_RESPONSE);
			}
		}
	}
	return NULL;
}
