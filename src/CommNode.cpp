#include "CommNode.h"
#include <boost/uuid/uuid_generators.hpp>
#include "CommNodeLog.h"

extern CommNodeLog* cnLog;

//These variables are local to the implementation of this class and are not
//meant to be referenced outside of this file.
std::array<char, 1> recvBuffer;
bai::udp::endpoint remoteEndpoint;

CommNode::CommNode(): socket(io) {
	uuid = boost::uuids::random_generator()();
}

CommNode::CommNode(int port): socket(io) {
	portNumber = port;
	uuid = boost::uuids::random_generator()();
}

void CommNode::start() {
	running = true;

	//Start UDP portion of the server
	udpReceive();
	io.run();
}

void CommNode::stop() {
	running = false;
	io.stop();
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

		cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, "Message sent!");
	}
}

void CommNode::udpReceive() {
	socket.async_receive_from(
		boost::asio::buffer(recvBuffer), remoteEndpoint,
		boost::bind(&CommNode::handleUDPReceive,this,
		boost::asio::placeholders::error,
		boost::asio::placeholders::bytes_transferred)); 
}

void CommNode::handleUDPReceive(const boost::system::error_code& error,
																	std::size_t) {
	if (!error || error == boost::asio::error::message_size) {
		cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, "Got a message!");
		udpReceive();
	}
}
