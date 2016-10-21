#include "CommNode.h"
#include <boost/uuid/uuid_generators.hpp>

CommNode::CommNode(): socket(io) {
	uuid = boost::uuids::random_generator()();
}

CommNode::CommNode(int port): socket(io) {
	portNumber = port;
	uuid = boost::uuids::random_generator()();
}

void CommNode::start() {
	running = true;
}

void CommNode::stop() {
	running = false;

}
void CommNode::update() {
	boost::system::error_code error;
	socket.open(bai::udp::v4(), error);

	if (!error) {
		socket.set_option(bai::udp::socket::reuse_address(true));
		socket.set_option(boost::asio::socket_base::broadcast(true));

		bai::udp::endpoint senderEndpoint(
			boost::asio::ip::address_v4::broadcast(), portNumber);

		std::array<char, 3> arr = {"yo"};
		socket.send_to(boost::asio::buffer(arr, 2), senderEndpoint);
		socket.close(error);
	}
}
