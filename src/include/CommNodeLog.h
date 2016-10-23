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
		
		void writeMessage(severities sev, string msg) {
			if (!fileStream.is_open()) {
				if (fileName.length() == 0) {
					cout << "Log file path not set. Call init() before trying " 
							 << "to write to the log.";
				} else {
					cout << "Unable to open log file at path: " << fileName;
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

		void init(string newFile) {
			fileName = newFile;
			fileStream.close();

			boost::filesystem::path dir(newFile);
			boost::filesystem::create_directories(dir.parent_path());
			fileStream.open(newFile);
		}

		void close() {
			fileStream.close();
		}
	private:
		static CommNodeLog* instance;
		ofstream fileStream;
		string fileName = "";
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
			};
		}
};

#endif
