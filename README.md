# Comm Node

This is the project that was given to Rob Miller by Uber ATC. In this repository, you'll find all of the documentation and code required to build this project. It will run on most or all Linux distros.

###Building the Project:
In the root directory of the project you'll find the "build" script. It has three different modes to it.

* ./build clean - delete all files in the bin and cmake directories
* ./build - builds all source files and puts executables and config files into the bin directory
* ./build all - runs clean then build

Once the project is built, simply run ./bin/commNode. Logs will be written to ./bin/logs.
