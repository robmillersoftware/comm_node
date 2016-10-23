#ifndef COMMNODE_H
#define COMMNODE_H

#include <boost/uuid/uuid.hpp>
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
		//static helper class to allow member function to act
		//as a POSIX thread callback
		static void* handleBroadcast(void* p) {
			return static_cast<CommNode*>(p)->handleBroadcast();
		}

		CommNode(int port);
		
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
	  void startBroadcastListener();
		void stopBroadcastListener();
		void* handleBroadcast(void);
		void sendHeartbeat();

		/**
		 * Private variables
		 */
		boost::uuids::uuid uuid;
		bool running;		
		int portNumber;
		char udpDgram[512];
		int udpListenerFD;						//This socket is for listening to broadcasts
		int udpBroadcastFD;						//This socket is for writing broadcasts
		sockaddr_in listenerAddr;
		sockaddr_in broadcastAddr;
		std::string broadcastStr;
		std::string listenerStr;
		int broadcastLen;
		int listenerLen;
		pthread_t listenerThread;
};

#endif
