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
#include <memory>
#include <systemc>

namespace riscv_gpgpu {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
    
    void setLogLevel(LogLevel level) {
        log_level_ = level;
    }
    
    void setLogFile(const std::string& filename) {
        log_file_.open(filename, std::ios::app);
    }
    
    void log(LogLevel level, const std::string& message) {
        if (level < log_level_) {
            return;
        }
        
        std::string level_str;
        switch (level) {
            case LogLevel::DEBUG: level_str = "[DEBUG]"; break;
            case LogLevel::INFO: level_str = "[INFO]"; break;
            case LogLevel::WARNING: level_str = "[WARN]"; break;
            case LogLevel::ERROR: level_str = "[ERROR]"; break;
        }
        
        std::stringstream ss;
        ss << level_str << " [" << sc_core::sc_time_stamp() << "] "
           << message << std::endl;
        
        std::cout << ss.str();
        if (log_file_.is_open()) {
            log_file_ << ss.str();
        }
    }
    
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void warning(const std::string& msg) { log(LogLevel::WARNING, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }

private:
    Logger() : log_level_(LogLevel::INFO) {}
    ~Logger() {
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }
    
    LogLevel log_level_;
    std::ofstream log_file_;
};

#define LOG_DEBUG(msg) riscv_gpgpu::Logger::instance().debug(msg)
#define LOG_INFO(msg) riscv_gpgpu::Logger::instance().info(msg)
#define LOG_WARNING(msg) riscv_gpgpu::Logger::instance().warning(msg)
#define LOG_ERROR(msg) riscv_gpgpu::Logger::instance().error(msg)

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_LOGGING_H
