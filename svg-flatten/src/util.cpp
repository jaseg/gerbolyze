
#include <sys/types.h>
#include <string>
#include <iostream>
#include <vector>

#ifndef NOFORK
#include <pwd.h>
#include <subprocess.h>
#endif
#include <filesystem>

#include "util.h"

#ifndef NOFORK
int gerbolyze::run_cargo_command(const char *cmd_name, std::vector<std::string> &cmdline, const char *envvar) {

    //std::cerr << "Running command: " << cmd_name << " ";
    std::vector<const char *> cmdline_c = {nullptr};
    for (std::string &s : cmdline) {
        //std::cerr << s << " ";
        cmdline_c.push_back(s.c_str());
    }
    cmdline_c.push_back(nullptr);
    //std::cerr << std::endl;

    const char *homedir;
    if ((homedir = getenv("HOME")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }
    std::string homedir_s(homedir);
    std::string cargo_bin_dir = homedir_s + "/.cargo/bin/" + cmd_name;

    bool found = false;
    int proc_rc = -1;
    for (int i=0; i<3; i++) {
        const char *envvar_val;
        switch (i) {
        case 0:
            if ((envvar_val = getenv(envvar)) == NULL) {
                continue;
            } else {
                cmdline_c[0] = envvar_val;
            }
            break;

        case 1:
            cmdline_c[0] = cmd_name;
            break;

        case 2:
            cmdline_c[0] = cargo_bin_dir.c_str();
            break;
        }

        struct subprocess_s subprocess;
        int rc = subprocess_create(cmdline_c.data(), subprocess_option_inherit_environment, &subprocess);
        if (rc) {
            std::cerr << "Error calling " << cmd_name << std::endl;
            return EXIT_FAILURE;
        }

        proc_rc = -1;
        rc = subprocess_join(&subprocess, &proc_rc);
        if (rc) {
            std::cerr << "Error calling " << cmd_name << std::endl;
            return EXIT_FAILURE;
        }

        rc = subprocess_destroy(&subprocess);
        if (rc) {
            std::cerr << "Error calling " << cmd_name << std::endl;
            return EXIT_FAILURE;
        }

        /* Fail if the command given in the environment variable could not be found. */
        if (i > 0 && proc_rc == 255) {
            continue;
        }
        found = true;
        break;
    }

    if (!found) {
        std::cerr << "Error: Cannot find " << cmd_name << ". Is it installed and in $PATH?" << std::endl;
        return EXIT_FAILURE;
    }

    if (proc_rc) {
        std::cerr << cmd_name << " returned an error code: " << proc_rc << std::endl;
        return EXIT_FAILURE;
    }

    return 0;
}
#else
int gerbolyze::run_cargo_command(const char *cmd_name, std::vector<std::string> &cmdline, const char *envvar) {
    (void) cmd_name, (void) cmdline, (void) envvar;
    std::cerr << "Error: Cannot spawn " << cmd_name << " subprocess since binary was built with fork/exec disabled (-DNOFORK=1)" << std::endl;
    return EXIT_FAILURE;
}
#endif

