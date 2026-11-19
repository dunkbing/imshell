#pragma once

// what the video tests need from the shell, the same on every platform
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace videotest {
    inline std::string tempDir() {
        for (const char* name : {"TMPDIR", "TEMP", "TMP"}) {
            if (const char* v = std::getenv(name); v && *v) {
                return v;
            }
        }
        return "/tmp";
    }

    inline void setEnv(const char* name, const char* value) {
#ifdef _WIN32
        _putenv_s(name, value);
#else
        setenv(name, value, 1);
#endif
    }

    // double quotes work in both cmd and sh for the paths the tests use
    inline std::string quoted(const std::string& s) { return "\"" + s + "\""; }

    inline bool hasTool(const char* name) {
#ifdef _WIN32
        return std::system((std::string("where ") + name + " >nul 2>nul").c_str()) == 0;
#else
        return std::system((std::string("command -v ") + name + " >/dev/null 2>&1").c_str()) == 0;
#endif
    }

    // a command's output ("" when it cannot run)
    inline std::string capture(const std::string& command) {
        std::string out;
#ifdef _WIN32
        FILE* p = _popen(command.c_str(), "r");
#else
        FILE* p = popen(command.c_str(), "r");
#endif
        if (!p) {
            return out;
        }
        char line[256];
        while (std::fgets(line, sizeof line, p)) {
            out += line;
        }
#ifdef _WIN32
        _pclose(p);
#else
        pclose(p);
#endif
        return out;
    }
}
