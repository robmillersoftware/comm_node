#ifndef NEIGHBORINFO_H
#define NEIGHBORINFO_H

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

class NeighborInfo {
	public:
		std::string uuid;							//Unique ID
		std::string ip;								//Neighbor's IP Address in string format
		unsigned short port;					//Neighbor's TCP port number
		int socketFD = -1;						//TCP socket file descriptor
		long latency;									//latency in milliseconds
		float bandwidth;								//potential bandwidth in kbps
};

#endif
