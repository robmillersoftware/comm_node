/**
 *  This is the main class for the CommNode program. It parses the config file 
 *  and starts the node. The application runs as a daemon/service.
 *
 *  Author: Robert Miller
 **/

#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "CommNode.h"
#include "CommNodeLog.h"

//I'm leaning pretty heavily on boost for this application to speed up 
//development. I acknowledge that there are faster and more lightweight
//alternatives for some of the things I'm doing. 
#include <boost/uuid/uuid_io.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

//Some variables declared before main to be used by both Windows and 
//Linux implementations
CommNodeLog* cnLog = CommNodeLog::getInstance();
boost::property_tree::ptree pt;

//This struct holds properties passed in from the configuration file
struct Properties {
	int portNumber = 8000;
	int heartbeatIntervalSecs = 10;
} properties;

void loadConfigFile();

int main(int argc, char *argv[]) {
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
	cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, 
		"Launching process with PID: " + std::to_string(::getpid()) + "\n");
	cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, 
		"Starting node with heartbeat every " + 
		std::to_string(properties.heartbeatIntervalSecs) + 
		" seconds...");

	CommNode c(properties.portNumber);
	c.start();

	while(c.isRunning()) {
		sleep(properties.heartbeatIntervalSecs);
		c.update();
	}

	cnLog->close();
}

/**
 * This function reads and parses an ini file to get configuration options. 
 * LOG_DIRECTORY and PARENT_DIRECTORY are preprocessor macros defined by cmake
 */
void loadConfigFile() {
	//Load the .ini file into a boost Property Tree.
	boost::property_tree::ini_parser::read_ini(
		PARENT_DIRECTORY "/config/CommNodeConfig.ini", pt);

	//Set up logging
	const std::string logFileName = pt.get<std::string>(
		"NodeProperties.logFileName");
	std::stringstream ssPath;
	ssPath << LOG_DIRECTORY << "/" << logFileName;
	cnLog->init(ssPath.str());

	//Convert properties from std::strings to numbers
	const std::string processName = pt.get<std::string>(
		"NodeProperties.processName");
	const std::string portString = pt.get<std::string>(
		"NodeProperties.portNumber");
	const std::string heartbeatIntervalString = pt.get<std::string>(
		"NodeProperties.heartbeatInterval");
	
	//If the user has specified a value for either of these in the 
	//config file, use those. Otherwise write to the log and use the defaults.
	try {
		properties.portNumber = boost::lexical_cast<int>(portString);
	} catch (boost::bad_lexical_cast const&) {
		cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, 
			"No port number specified, continuing with default value of " + 
			std::to_string(properties.portNumber));
	}

	try {
		properties.heartbeatIntervalSecs = boost::lexical_cast<int>(
			heartbeatIntervalString);
	} catch (boost::bad_lexical_cast const&) {
		cnLog->writeMessage(CommNodeLog::severities::CN_DEBUG, 
			"No heartbeat interval specified, continuing with default value of " + 
			std::to_string(properties.heartbeatIntervalSecs));
	}
}
