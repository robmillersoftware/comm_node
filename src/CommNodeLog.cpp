#include "CommNodeLog.h"

CommNodeLog* CommNodeLog::instance;

//Global variable. Any file that includes CommNodeLog.h can access this 
//through extern
CommNodeLog* cnLog = CommNodeLog::getInstance();
