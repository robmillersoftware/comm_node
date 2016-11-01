# Comm Node

This is the Comm Node project by Robert Miller. In this repository, you'll find all of the documentation and code required to build this project. It will run on most or all Linux distros.

### Building the Project:
In the root directory of the project you'll find the "build" script. It has three different modes to it.

* ./build clean - delete all files in the bin and cmake directories
* ./build - builds all source files and puts executables and config files into the bin directory
* ./build all - runs clean then build

Once the project is built, simply run ./dist/runCN.sh. This will launch a daemon process whose status you can view through its entry in ./dist/logs/commnodeUUID.log or ./dist/nodestatus_UUID.txt. You can run multiple instances by repeated calls to the commNode executable. This will create a new log file and nodestatus file for each instance.

### Approach
My plan was to write my code using mostly POSIX-compliant C and architecture-agnostic C++11. I wanted to show my ability to work at both a low and high level of abstraction. The architecture mostly built itself and is discussed in more detail in the design document (docs/CommNode_High_Level_Design.pdf).

I knew I would be using Boost for their file manipulation capabilities, but wanted to avoid using ASIO. I most likely made it more difficult on myself than I needed to, but I wanted the challenge. I wanted to write mostly asynchronous IO on the sockets, and I believe I succeeded at a basic implementation of that. Mutex locks were used for manipulating members of the CommNode class and for the logger. Given more time, I would have turned the logger into a message queue and probably changed my class architecture. There is a list of improvements I would like to make on the last page of the design document. I've also got an excel spreadsheet showing my planned schedule vs my real time spent with comments on each step.
