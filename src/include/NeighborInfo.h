#ifndef NEIGHBORINFO_H
#define NEIGHBORINFO_H

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

struct NeighborInfo {
	boost::uuids::uuid uuid;			//Unique ID
	std::string ip;								//Neighbor's IP Address in string format
	unsigned short port;					//Neighbor's TCP port number
	int socketFD = -1;						//TCP socket file descriptor
	long latency;									//latency in milliseconds
	long bandwidth;								//potential bandwidth in kbps

	//For use with some std templates
  mutable int position;		
	mutable bool left, right;

	bool operator <(const NeighborInfo& rh) const {
		return boost::uuids::to_string(uuid) < boost::uuids::to_string(rh.uuid);
	}
};

#endif
