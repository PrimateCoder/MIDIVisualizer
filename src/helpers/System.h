#pragma once

#include <fstream>
#include <string>

/**
 \brief Performs system basic operations such as directory creation, timing, threading, file picking.
 \ingroup System
 */
class System {
public:

	/** Notify the user by sending a 'Bell' signal. */
	static void ping();
	
	/** Return the current value of a time counter.
	 \return the current counter value, in seconds.
	 */
	static double time();

	/** Obtain a YYYY_MM_DD_HH_MM_SS timestamp of the current time.
	 \return the string representation
	 */
	static std::string timestamp();

	static std::ifstream openInputFile(const std::string& path, bool binary = false);

	static std::ofstream openOutputFile(const std::string& path, bool binary = false);

};
