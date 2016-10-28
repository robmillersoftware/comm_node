#ifndef COMMNODELOG_H
#define COMMNODELOG_H

#include <string>
#include <fstream>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <boost/filesystem.hpp>

using namespace std;

/**
 * This is a singleton class used for basic logging.
 */
class CommNodeLog {
	public:
		enum class severities {
			CN_INFO,
			CN_DEBUG,
			CN_WARNING,
			CN_ERROR
		};
			
		static CommNodeLog* getInstance() {
			if (!instance)
				instance = new CommNodeLog;
			return instance;
		}
		
		/**
		 * Writes a message to the log with the passed in severity, current time, and
		 * the message parameter.
		 * @param sev This is an entry from the CommNodeLog::severities enum
		 * @param msg The message the user wants to display in the log
		 */
		void writeMessage(severities sev, string msg) {
			if (!fileStream.is_open()) {
				if (logFilePath.length() == 0) {
					cout << "Log file path not set. Call init() before trying " 
							 << "to write to the log.";
				} else {
					cout << "Unable to open log file at path: " << logFilePath;
				}
				return;
			}

			using namespace boost::posix_time;

			time_facet* facet = new time_facet("%d-%b-%Y %H:%M:%S");
			fileStream.imbue(locale(fileStream.getloc(), facet));
	
			//The format is "day-month-year H:M:S (severity) message"
			fileStream << second_clock::local_time() << " (" << 
				getSevString(sev) << ") " << msg << endl;
			fileStream.flush();
		}

		/**
		 * Creates the directory for the log file if it doesn't already exist. Also 
		 * opens the file stream for the logs.
		 */
		void init(string newFile) {
			logFilePath = newFile;
			fileStream.close();

			boost::filesystem::path dir(newFile);

			//If the file exists, just append to it
			if (boost::filesystem::exists(newFile)) {
				fileStream.open(newFile, std::ofstream::out | std::ofstream::app);
			} else {
				boost::filesystem::create_directories(dir.parent_path());
				fileStream.open(newFile);
			}
		}

		void close() {
			fileStream.close();

			//Archive the log when you are done with it
			boost::filesystem::path logPath(logFilePath);
			std::string logFile = logPath.filename().string();
			std::stringstream newFileStream;

			using namespace boost::posix_time;
					
			time_facet* facet = new time_facet("%H-%M-%S");
			newFileStream.imbue(locale(newFileStream.getloc(), facet));

			newFileStream << logPath.stem() << "_" << 
				second_clock::local_time() << endl;
			boost::filesystem::path logArchive(logPath.parent_path().string() + 
				"archive/" + newFileStream.str());
			boost::filesystem::create_directories(logArchive);
			boost::filesystem::rename(logPath, logArchive);
		}

		/**
		 * Logs and error then exits the application
		 */
		void exitWithError(std::string msg) {
			error(msg);
			exit(1);
		}

		/**
		 * These functions are shortcuts for calling writeMessage with a severity 
		 * parameter
		 */
		void error(std::string msg) {
			writeMessage(severities::CN_ERROR, msg + ": " + 
				std::string(strerror(errno)));
		}

		void warning(std::string msg) {
			writeMessage(severities::CN_WARNING, msg);
		}
		void debug(std::string msg) {
			writeMessage(severities::CN_DEBUG, msg);
		}
		void info(std::string msg) {
			writeMessage(severities::CN_INFO, msg);
		}

	private:
		static CommNodeLog* instance;
		ofstream fileStream;
		string logFilePath = "";
		explicit CommNodeLog() {
		}

		string getSevString(severities sev) {
			switch (sev) {
				case severities::CN_INFO:
					return "info";
				case severities::CN_DEBUG:
					return "debug";
				case severities::CN_WARNING:
					return "warning";
				case severities::CN_ERROR:
					return "error";
				default:
					return "";
			};
		}
};

#endif
