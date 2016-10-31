/**
 *  This is the main class for the CommNode program. It parses the config file 
 *  and starts the node. The application runs as a daemon/service.
 *
 *  Author: Robert Miller
 **/

#include "CommNode.h"
#include "CommNodeLog.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <stdlib.h>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <sys/types.h>
#include <sys/stat.h>
/*#include <iostream>
#include <string>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>


#include <boost/lexical_cast.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>
*/
extern CommNodeLog* cnLog;
boost::property_tree::ptree pt;

//Global variables
int portNumber = 8000;
int heartbeatIntervalSecs = 10;

void loadConfigFile();

int main(int argc, char *argv[]) {
	//If the INSTALL_DIRECTORY environment variable isn't present, then the 
	//node wasn't launched with the run script.
	const char* installDir = getenv("INSTALL_DIRECTORY");
	if (installDir == NULL) {
		std::cout << "Use run.sh to launch commNode" << endl;
		exit(1);
	}

	pid_t pid, sid;

	//Here's how we make our daemon
	pid = fork();

	if (pid < 0) { exit(EXIT_FAILURE); }

	//We have a good PID, so close the parent.
	if (pid > 0) { exit(EXIT_SUCCESS); }

	umask(0);

	//Setting a new signature ID for the child process
	sid = setsid();
	if (sid < 0) { exit(EXIT_FAILURE); }
	//Change working directory to somewhere guaranteed to be there
	if ((chdir("/")) < 0) { exit(EXIT_FAILURE); }

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	
	loadConfigFile();
	
	//Generate the node's UUID first so we can append it to the log file name
	boost::uuids::uuid nodeId = boost::uuids::random_generator()();

	const std::string logFileName = pt.get<std::string>(
		"NodeProperties.logFileName") + 
		boost::uuids::to_string(nodeId) + ".log";
	std::stringstream ssPath;
	ssPath << std::string(installDir) << "/logs/" << logFileName;
	cnLog->init(ssPath.str());

	cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, 
		"Launching process with PID: " + std::to_string(::getpid()) + "\n");
	cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, 
		"Starting node with heartbeat every " + 
		std::to_string(heartbeatIntervalSecs) + 
		" seconds...");

	//We're now set up as a service, create node object and begin
	CommNode c(nodeId, portNumber);
	c.start();

	while(c.isRunning()) {
		sleep(heartbeatIntervalSecs);
		c.update();
	}
	cnLog->close();
}

/**
 * This function reads and parses an ini file to get configuration options. 
 */
void loadConfigFile() {
	//Load the .ini file into a boost Property Tree.
	boost::property_tree::ini_parser::read_ini(
		std::string(getenv("INSTALL_DIRECTORY")) +  "/config/CommNodeConfig.ini", 
		pt);

	//Convert properties from std::strings to numbers
	const std::string heartbeatIntervalString = pt.get<std::string>(
		"NodeProperties.heartbeatInterval");
}
