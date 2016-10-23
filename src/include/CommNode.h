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

class CommNode {
	public:
		static void* handleUDP(void* p) {
			return static_cast<CommNode*>(p)->handleUDP();
		}

		CommNode();
		CommNode(int port);
		void start();
		void stop();
		void update();
		boost::uuids::uuid getUUID() { return uuid; };
		bool isRunning() { return running; };
	private:
		boost::uuids::uuid uuid;	
		bool running;
		int portNumber;
		sockaddr_in adr;
		int len_inet;
		int udpSock;
		char udpDgram[512];
		std::string bc_addr;
		void setUpUDPSocket();
	  void startListeningUDP();
		void stopListeningUDP();
		unsigned int so_reuseaddr = 1;
		pthread_t udpThread;
		void* handleUDP(void);
		void sendHeartbeat();
};

#endif
