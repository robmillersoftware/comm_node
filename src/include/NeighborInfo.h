#ifndef NEIGHBORINFO_H
#define NEIGHBORINFO_H

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

struct NeighborInfo {
	boost::uuids::uuid uuid;
	std::string ip;
	unsigned short port;
	int socketFD = -1;

  mutable int position;
	mutable bool left, right;

	bool operator <(const NeighborInfo& rh) const {
		return boost::uuids::to_string(uuid) < boost::uuids::to_string(rh.uuid);
	}
};

#endif
