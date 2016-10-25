# Comm Node

This is the Comm Node project by Robert Miller. In this repository, you'll find all of the documentation and code required to build this project. It will run on most or all Linux distros.

###Building the Project:
In the root directory of the project you'll find the "build" script. It has three different modes to it.

* ./build clean - delete all files in the bin and cmake directories
* ./build - builds all source files and puts executables and config files into the bin directory
* ./build all - runs clean then build

Once the project is built, simply run ./bin/commNode. This will launch a daemon process whose status you can view through its entry in ./bin/logs or ./bin/nodestatus_uuid.txt. You can run multiple instances by repeated calls to the commNode executable. This will create a new log file and nodestatus file for each instance.
