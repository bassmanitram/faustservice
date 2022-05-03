/************************************************************************
 FAUST Architecture File
 Copyright (C) 2017 GRAME, Centre National de Creation Musicale
 ---------------------------------------------------------------------
 This Architecture section is free software; you can redistribute it
 and/or modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 3 of
 the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; If not, see <http://www.gnu.org/licenses/>.

 EXCEPTION : As a special exception, you may create a larger work
 that contains this FAUST architecture section and distribute
 that work under terms of your choice, so long as this FAUST
 architecture section is not modified.

 ************************************************************************
 ************************************************************************/

#include <sys/stat.h>
#include <ctime>
#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <signal.h>
#include <unistd.h>
#include <string>

#include "server.hh"
#include "utilities.hh"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

int  gPort        = 8888;
int  gMaxClients  = 2;
bool gAnyOrigin   = true;
int  gMaxSessions = 50;
int  gVerbosity   = 0;

fs::path gCurrentDirectory;   ///< root directory were makefiles and sessions and log are located
fs::path gSessionsDirectory;  ///< directory where sessions are stored
fs::path
    gMakefilesDirectory;  ///< directory containing all the "<os>/Makefile.<architecture>[-32bits|-64bits]" makefiles
fs::path gLogfile;        ///< faustweb logfile
string   gRecoverCmd;     ///< system command to launch for recovery after intercepted crash
char*    gArguments[256];

// Processes command line arguments using boost/parse_options
static void process_cmdline(int argc, char* argv[])
{
    po::options_description desc("faustweb program options");
    desc.add_options()("sessions-dir,d", po::value<string>(), "directory in which sessions files will be written");
    desc.add_options()("help,h", "produce this help message");
    desc.add_options()("any-origin,a", "Adds any origin when answering requests");
    desc.add_options()("max-clients,m", po::value<int>(), "maximum number of clients allowed to concurrently upload");
    desc.add_options()("port,p", po::value<int>(), "the listening port");
    desc.add_options()("max-sessions,n", po::value<int>(), "maximum number of cached sessions");
    desc.add_options()("verbose,v", po::value<int>(), "0: normal; 1: verbose; 2: very verbose");
    desc.add_options()("recover-cmd,r", po::value<std::string>(),
                       "program (usually self) to launch after crash recovery");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cout << desc << endl;
        exit(0);
    }

    if (vm.count("port")) {
        gPort = vm["port"].as<int>();
    }

    if (vm.count("max-clients")) {
        gMaxClients = vm["max-clients"].as<int>();
    }

    if (vm.count("max-sessions")) {
        gMaxSessions = vm["max-sessions"].as<int>();
    }

    if (vm.count("logfile")) {
        gLogfile = vm["logfile"].as<string>();
    }

    if (vm.count("allow-any-origin")) {
        gAnyOrigin = true;
    }

    if (vm.count("sessions-dir")) {
        gSessionsDirectory = vm["sessions-dir"].as<string>();
    }

    if (vm.count("verbose")) {
        gVerbosity = vm["verbose"].as<int>();
    }

    if (vm.count("recover-cmd")) {
        gRecoverCmd = vm["recover-cmd"].as<string>();
    }
}

static void printFaustVersion()
{
    int err = system("faust -v");
    if (err != 0) std::cerr << "ERROR: Faust not found";
}

static size_t computeSessionSize()
{
    size_t size = 0;

    try {
        fs::recursive_directory_iterator it(gSessionsDirectory);
        for (; it != fs::recursive_directory_iterator(); ++it) {
            if (!fs::is_directory(*it)) size += fs::file_size(*it);
        }
        return size;
    } catch (const boost::filesystem::filesystem_error& e) {
        return 0;
    }
}

// static void recover()
// {
//     std::cerr << "execv(" << gRecoverCmd << ")" << endl;
//     execv(gRecoverCmd.c_str(), gArguments);
// }

static void _sigaction(int signal, siginfo_t*, void*)
{
    cerr << "\n\n";
    cerr << "SIGNAL #" << signal << " CATCHED!" << endl;
    if (gRecoverCmd.size() > 0) {
        std::cerr << "EXEC RECOVERING CMD: " << gRecoverCmd << endl;
        execv(gRecoverCmd.c_str(), gArguments);
    } else {
        std::cerr << "NO RECOVERING CMD -> EXIT" << endl;
        exit(-1);
    }
}

static void catchsigs()
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = _sigaction;
    sa.sa_flags     = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
}

int main(int argc, char* argv[], char* env[])
{
    catchsigs();

    // Make a copy the command arguments
    for (int i = 0; i < argc; i++) {
        gArguments[i] = argv[i];
    }
    gArguments[argc] = nullptr;

    // Set the various default paths
    gCurrentDirectory   = fs::absolute(fs::current_path());
    gMakefilesDirectory = gCurrentDirectory / "makefiles";
    gSessionsDirectory  = gCurrentDirectory / "sessions";

    try {
        process_cmdline(argc, argv);
    } catch (...) {
        cerr << "Unknown parameter" << endl;
        exit(1);
    }

    if (gVerbosity >= 0) {
        time_t tmNow = time(0);
        std::cerr << "faustweb starting " << ctime(&tmNow) << "\n"
                  << "         port: " << gPort << "\n"
                  << "    directory: " << gCurrentDirectory << "\n"
                  << "    makefiles: " << gMakefilesDirectory << "\n"
                  << " sessions dir: " << gSessionsDirectory << "\n"
                  << "sessions size: " << computeSessionSize() << "\n"
                  << "    verbosity: " << gVerbosity << "\n"
                  << "  recover-cmd: " << gRecoverCmd << "\n"
                  << std::endl;

        // Print Faust version
        printFaustVersion();

        // Print running environment
        if (gVerbosity >= 2) {
            std::cerr << "\n\nBEGIN ENVIRONMENT" << std::endl;
            for (int i = 0; env[i] != 0; i++) {
                std::cerr << env[i] << std::endl;
            }
            std::cerr << "END ENVIRONMENT\n\n" << std::endl;
        }
    }

    // Check for ".../makefiles/" directory
    if (is_directory(gMakefilesDirectory)) {
        if (gVerbosity >= 2) std::cerr << "Makefiles directory available at path " << gMakefilesDirectory << std::endl;
    } else {
        std::cerr << "ERROR: no makefiles directory available at path " << gMakefilesDirectory << std::endl;
        exit(1);
    }

    // If needed creates ".../sessions/" directory
    if (create_directory(gSessionsDirectory)) {
        if (gVerbosity >= 1) std::cerr << "Create \"sessions\" directory at path " << gSessionsDirectory << std::endl;
    } else {
        if (gVerbosity >= 1) std::cerr << "Reuse \"sessions\" directory at path " << gSessionsDirectory << std::endl;
    }

    // Create, start and stop the http server
    FaustServer server(gPort, gMaxClients, gSessionsDirectory, gMakefilesDirectory, gLogfile, gMaxSessions);

    if (!server.start()) {
        std::cerr << "ERROR: unable to start webserver ! Check if port " << gPort << " is available" << std::endl;
        exit(1);
    } else {
        if (gVerbosity >= 2) std::cerr << "webserver started succesfully" << std::endl;
    }

    std::cerr << "type ctrl-c to quit" << std::endl;

    while (true) {
        // We never stop the server
        sleep(30);
    }
    return 0;
}
