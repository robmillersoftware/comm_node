#ifndef COMMNODE_H
#define COMMNODE_H

#include "NeighborInfo.h"
#include <map>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/algorithm/string.hpp>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <mutex>

/**
 * This class performs the majority of the networking tasks
 * in the CommNode application.
 */
class CommNode {
	public:
		/**
	   * PUBLIC STATICS
		 */
		//Our sockets will deliver messages of exactly 128 bytes
		static const int DGRAM_SIZE = 128;
		//This string will signal nodes that a TCP conversation is over
		static const char* NO_RESPONSE;

		//These two static functions let us use member functions as 
		//POSIX thread callbacks
		static void* handleBroadcast(void* p) {
			return static_cast<CommNode*>(p)->handleBroadcast();
		}

		static void* handleTCP(void* p) {
			return static_cast<CommNode*>(p)->handleTCP();
		}
	
		/**
		 * CONSTRUCTOR
		 */
		CommNode(boost::uuids::uuid id, int port);
	
		~CommNode() {
			delete neighbors;
			delete localNeighbors;
		};
	
		/**
		 * CONTROL FUNCTIONS
		 */
		void start(); //start transmitting and listening 
		void stop(); //stop transmitting and listening
		void update(); //send heartbeat and perform maintenance
		
		/**
		 * Accessor functions
		 */
		boost::uuids::uuid getUUID() { return uuid; };
		bool isRunning() { return running; };
	private:
		/**
		 * Private functions
		 */
		void initBroadcastListener();
		void initBroadcastServer();
		void initTCPListener();
	  void startBroadcastListener();
		void startTCPListener();
		void* handleBroadcast(void);
		void* handleTCP(void);
		void forwardToLocalNeighbors(char* msg, unsigned long int sz, 
			std::string id = "");
		void sendHeartbeat();
		void addNeighbor(std::string id, std::string ip, int port);
		void connectToNeighbor(NeighborInfo* n);
		void printNeighbors();
		void runMetrics();
		std::string createTCPResponse(int sockFD, char* buf, unsigned long int sz);
		void addToMapSync(const NeighborInfo& n);

		/**
		 * Private variables
		 */
		std::mutex mapMutex;
		boost::uuids::uuid uuid;
		bool running;		
		int udpPortNumber;
		int tcpPortNumber;
		char udpDgram[512];
		int udpListenerFD;						//This socket is for listening to broadcasts
		int udpBroadcastFD;						//This socket is for writing broadcasts
		int tcpListenerFD;
		sockaddr_in broadcastAddr;
		std::string broadcastStr;
		std::string listenerStr;
		unsigned int broadcastLen;
		unsigned int listenerLen;
		unsigned int tcpLen;
		std::vector<pollfd> fds;
		pthread_t listenerThread;
		pthread_t tcpThread;
		bool isListening;							//We are listening for UDP broadcasts
		unsigned short tcpPort; 			//This is assigned when the TCP listener is 
																	//initialized
	
		//This map contains all nodes that can be reached on the LAN
		std::map<std::string, NeighborInfo*> *neighbors;
		//This map contains only nodes that exist on the same IP address as the
		//current node
		std::map<std::string, NeighborInfo*> *localNeighbors;
};
#endif
