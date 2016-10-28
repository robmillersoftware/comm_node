#ifndef COMMNODE_H
#define COMMNODE_H

#include "NeighborInfo.h"
#include <map>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/**
 * This class performs the majority of the networking tasks
 * in the CommNode application.
 */
class CommNode {
	public:
		//static helper classes to allow member function to act
		//as a POSIX thread callback
		static void* handleBroadcast(void* p) {
			return static_cast<CommNode*>(p)->handleBroadcast();
		}

		static void* handleTCP(void* p) {
			return static_cast<CommNode*>(p)->handleTCP();
		}

		CommNode(boost::uuids::uuid id, int port);
		
		/*
		 * These three functions are the controls for the node
		 */
		void start(); //start transmitting and listening 
		void stop(); //stop transmitting and listening
		void update(); //send heartbeat and perform maintenance
		
		//These are the only two pieces of information 
		//outsiders should have access to.
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
		void stopBroadcastListener();
		void startTCPListener();
		void* handleBroadcast(void);
		void* handleTCP(void);
		void forwardToLocalNeighbors(char* msg, unsigned long int sz, 
			boost::uuids::uuid id = boost::uuids::nil_uuid());
		void sendHeartbeat();
		void addNeighbor(NeighborInfo n);
		void printNeighbors();
		void runMetrics();
		const char* createTCPResponse(int sockFD, char* buf, unsigned long int sz);

		/**
		 * Private variables
		 */
		boost::uuids::uuid uuid;
		bool running;		
		int udpPortNumber;
		int tcpPortNumber;
		char udpDgram[512];
		int udpListenerFD;						//This socket is for listening to broadcasts
		int udpBroadcastFD;						//This socket is for writing broadcasts
		int tcpListenerFD;
		sockaddr_in listenerAddr;
		sockaddr_in broadcastAddr;
		sockaddr_in tcpAddr;
		std::string broadcastStr;
		std::string listenerStr;
		unsigned int broadcastLen;
		unsigned int listenerLen;
		unsigned int tcpLen;
		pthread_t listenerThread;
		pthread_t tcpThread;
		bool isListening;							//We are listening for UDP broadcasts
		unsigned short tcpPort; 			//This is assigned when the TCP listener is 
																	//initialized
	
		//This map contains all nodes that can be reached on the LAN
		std::map<boost::uuids::uuid, NeighborInfo> neighbors;

		//This map contains only nodes that exist on the same IP address as the
		//current node
		std::map<boost::uuids::uuid, NeighborInfo> localNeighbors;
};
#endif
