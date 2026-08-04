// logging.h - Logging and tracing utilities
//
// Provides logging, tracing, and debug utilities
// for the SystemC models
//

#ifndef RISCV_GPGPU_LOGGING_H
#define RISCV_GPGPU_LOGGING_H

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <systemc>

// Undefine DEBUG before enum to avoid collision with -DDEBUG=1 build flag
#ifdef DEBUG
#undef DEBUG
#endif

namespace riscv_gpgpu {

enum class LogLevel {
    DEBUG   = 0,
    INFO    = 1,
    WARNING = 2,
    ERR     = 3   // was ERROR – renamed to avoid system macro collision
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void setLogLevel(LogLevel level) { log_level_ = level; }

    void setLogFile(const std::string& filename) {
        log_file_.open(filename, std::ios::app);
    }

    void log(LogLevel level, const std::string& message) {
        if (level < log_level_) return;

        const char* tag = "[?????]";
        switch (level) {
            case LogLevel::DEBUG:   tag = "[DEBUG]"; break;
            case LogLevel::INFO:    tag = "[INFO ]"; break;
            case LogLevel::WARNING: tag = "[WARN ]"; break;
            case LogLevel::ERR:     tag = "[ERROR]"; break;
        }

        std::ostringstream ss;
        ss << tag << " [" << sc_core::sc_time_stamp() << "] " << message << "\n";

        std::cout << ss.str() << std::flush;
        if (log_file_.is_open()) log_file_ << ss.str() << std::flush;
    }

    void debug  (const std::string& msg) { log(LogLevel::DEBUG,   msg); }
    void info   (const std::string& msg) { log(LogLevel::INFO,    msg); }
    void warning(const std::string& msg) { log(LogLevel::WARNING, msg); }
    void error  (const std::string& msg) { log(LogLevel::ERR,     msg); }

    // Prints a labelled separator line – use between test phases
    void printSeparator(const std::string& label = "") {
        std::string out;
        if (label.empty()) {
            out = std::string(60, '-');
        } else {
            out = "-- " + label + " ";
            if (out.size() < 60) out += std::string(60 - out.size(), '-');
        }
        std::cout << out << "\n" << std::flush;
    }

private:
    Logger() : log_level_(LogLevel::INFO) {}
    ~Logger() { if (log_file_.is_open()) log_file_.close(); }

    LogLevel      log_level_;
    std::ofstream log_file_;
};

// ── Convenience macros ────────────────────────────────────────────────────────
#define LOG_DEBUG(msg)   riscv_gpgpu::Logger::instance().debug(msg)
#define LOG_INFO(msg)    riscv_gpgpu::Logger::instance().info(msg)
#define LOG_WARNING(msg) riscv_gpgpu::Logger::instance().warning(msg)
#define LOG_ERROR(msg)   riscv_gpgpu::Logger::instance().error(msg)
#define LOG_SEP(label)   riscv_gpgpu::Logger::instance().printSeparator(label)

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_LOGGING_H