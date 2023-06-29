#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#include <sqlite3.h>


const char FIREFOX_BIN[] = "/usr/bin/firefox";

const char SQL[] = R"(
    INSERT OR IGNORE INTO moz_cookies
        (originAttributes, name, value, host, path, expiry, lastAccessed, creationTime,
         isSecure, isHttpOnly, inBrowserElement, sameSite, rawSameSite, schemeMap)
      VALUES ('', ?, ?, ?, '/', ?, ?, ?, 1, 1, 0, 0, 0, 2) )";

enum {
    PARAM_NAME = 1,
    PARAM_VALUE,
    PARAM_HOST,
    PARAM_EXPIRY,
    PARAM_LAST_ACCESSED,
    PARAM_CREATION_TIME,
};

const struct {
    const char* name;
    const char* value;
    const char* host;
} COOKIES[] = {
    {"ANID", "OPT_OUT", ".google.com"},
    {"ANID", "OPT_OUT", ".google.co.uk"},
    {"CONSENT", "YES+cb", ".google.com"},
    {"CONSENT", "YES+cb", ".google.co.uk"},
    {"CONSENT", "YES+cb", ".youtube.com"},
};


bool reportPosixError(std::string_view action, int code)
{
    if (code == 0) {
        return false;
    }

    std::cerr << "Failed to " << action << ": "
              << std::error_code(code, std::system_category()).message()
              << '\n';

    return true;
}


bool reportSqliteError(std::string_view action, int code)
{
    if (code == 0) {
        return false;
    }

    std::cerr << "Failed to " << action << ": " << sqlite3_errstr(code) << '\n';
    
    return true;
}


std::optional<std::string> firefoxProfilesPath()
{
    const char* homeDir = std::getenv("HOME");
    if (!homeDir) {
        passwd* pwd = getpwuid(geteuid());
        if (!pwd) {
            reportPosixError("determine home directory", errno);
            return {};
        }
        homeDir = pwd->pw_dir;
    }

    return homeDir + std::string("/.mozilla/firefox");
}


std::optional<std::string> firefoxCookiesDbPath()
{
    const auto profilePath = firefoxProfilesPath();
    if (!profilePath) {
        return {};
    }
    const std::string profilesIniPath = *profilePath + "/profiles.ini";
    std::ifstream profilesIni(profilesIniPath);
    if (!profilesIni) {
        std::cerr << "Failed to open " << profilesIniPath << '\n';
        return {};
    }

    bool inInstallSection = false;
    std::string line;
    while (getline(profilesIni, line)) {
        if (line.starts_with('[')) {
            inInstallSection = line.starts_with("[Install");
        }
        else if (inInstallSection && line.starts_with("Default=")) {
            return std::format("{}/{}/{}", *profilePath, line.substr(8),
                               "cookies.sqlite");
        }
    }

    std::cerr << "Failed to determine default Firefox profile\n";
    return {};
}


struct SqliteGuard {
    sqlite3* db;
    ~SqliteGuard()
    {
        reportSqliteError("close database", sqlite3_close(db));
    }
};


struct SqliteStmtGuard {
    sqlite3_stmt* stmt;
    ~SqliteStmtGuard()
    {
        reportSqliteError("finalize prepared statement", sqlite3_finalize(stmt));
    }
};


time_t distantFuture()
{
    using namespace std::chrono;
    return system_clock::to_time_t(sys_days{2100y/1/1});
}


int insertCookies()
{
    const auto cookiesPath = firefoxCookiesDbPath();
    if (!cookiesPath) {
        return 1;
    }

    std::cout << "Inserting Google cookie consent cookies" << std::endl;

    int rc = sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
    if (reportSqliteError("set SQLite single-thread mode", rc)) {
        return 1;
    }

    sqlite3* db{};
    rc = sqlite3_open_v2(cookiesPath->c_str(), &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_EXRESCODE, nullptr);
    if (reportSqliteError("open cookies database", rc)) {
        return 1;
    }
    SqliteGuard sqliteGuard{db};

    rc = sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
    if (reportSqliteError("set synchronous=NORMAL mode", rc)) {
        return 1;
    }

    rc = sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    if (reportSqliteError("begin transaction", rc)) {
        return 1;
    }

    sqlite3_stmt* insertStmt;
    rc = sqlite3_prepare_v2(db, SQL, sizeof(SQL), &insertStmt, nullptr);
    if (reportSqliteError("prepare SQL statement", rc)) {
        return 1;
    }
    SqliteStmtGuard stmtGuard{insertStmt};

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto nowMicros =
            std::chrono::duration_cast<std::chrono::microseconds>(now).count();

    rc = sqlite3_bind_int64(insertStmt, PARAM_EXPIRY, distantFuture());
    if (reportSqliteError("bind cookie expiry to SQL statement", rc)) {
        return 1;
    }
    rc = sqlite3_bind_int64(insertStmt, PARAM_LAST_ACCESSED, nowMicros);
    if (reportSqliteError("bind cookie lastAccessed to SQL statement", rc)) {
        return 1;
    }
    rc = sqlite3_bind_int64(insertStmt, PARAM_CREATION_TIME, nowMicros);
    if (reportSqliteError("bind cookie creationTime to SQL statement", rc)) {
        return 1;
    }

    for (const auto& cookie : COOKIES) {
        rc = sqlite3_bind_text(insertStmt, PARAM_NAME, cookie.name, -1, nullptr);
        if (reportSqliteError("bind cookie name to SQL statement", rc)) {
            return 1;
        }
        rc = sqlite3_bind_text(insertStmt, PARAM_VALUE, cookie.value, -1, nullptr);
        if (reportSqliteError("bind cookie value to SQL statement", rc)) {
            return 1;
        }
        rc = sqlite3_bind_text(insertStmt, PARAM_HOST, cookie.host, -1, nullptr);
        if (reportSqliteError("bind cookie host to SQL statement", rc)) {
            return 1;
        }
        rc = sqlite3_step(insertStmt);
        if (rc != SQLITE_DONE && reportSqliteError("insert cookie", rc)) {
            return 1;
        }
        rc = sqlite3_reset(insertStmt);
        if (reportSqliteError("reset SQL statement", rc)) {
            return 1;
        }
    }

    rc = sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    if (reportSqliteError("commit transaction", rc)) {
        return 1;
    }

    return 0;
}


int main(int argc, char* argv[])
{
    std::ios::sync_with_stdio(false);

    if (0 != insertCookies()) {
        return 1;
    }

    std::vector<char*> firefoxArgv;
    firefoxArgv.reserve(argc);
    firefoxArgv.push_back(const_cast<char*>(FIREFOX_BIN));
    if (argc >= 2) {
        firefoxArgv.insert(firefoxArgv.end(), argv + 1, argv + argc);
    }
    firefoxArgv.push_back(nullptr);

    std::cout << "Starting Firefox" << std::endl;

    return execv(FIREFOX_BIN, firefoxArgv.data());
}

// vim: set tw=88
