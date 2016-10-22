#ifndef COMMNODE_H
#define COMMNODE_H

#include <boost/uuid/uuid.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/system/error_code.hpp>

namespace bai = boost::asio::ip;

class CommNode {
	public:
		CommNode();
		CommNode(int port);
		void start();
		void stop();
		void update();
		boost::uuids::uuid getUUID() { return uuid; };
		bool isRunning() { return running; };
	private:
		boost::uuids::uuid uuid;	
		int portNumber;	
		bool running;
		boost::asio::io_service io;		
		bai::udp::socket socket;
		void udpReceive();
		void handleUDPReceive(const boost::system::error_code& error,
			std::size_t);
};

#endif
