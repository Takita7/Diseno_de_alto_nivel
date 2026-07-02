// config.h - Configuration parameter management
//
// Provides unified configuration loading and parameter access
// for all components of the RISCV GPGPU platform
//

#ifndef RISCV_GPGPU_CONFIG_H
#define RISCV_GPGPU_CONFIG_H

#include <string>
#include <map>
#include <any>
#include <memory>

namespace riscv_gpgpu {

class Configuration {
public:
    // Load configuration from YAML file
    static std::shared_ptr<Configuration> load(const std::string& config_file);
    
    // Load configuration from JSON file
    static std::shared_ptr<Configuration> loadJSON(const std::string& config_file);
    
    // Get configuration parameter
    template<typename T>
    T get(const std::string& key, const T& default_value = T()) const {
        auto it = params_.find(key);
        if (it != params_.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                return default_value;
            }
        }
        return default_value;
    }
    
    // Set configuration parameter
    template<typename T>
    void set(const std::string& key, const T& value) {
        params_[key] = std::any(value);
    }
    
    // Get all parameters as map
    const std::map<std::string, std::any>& getAllParams() const {
        return params_;
    }
    
    // Save configuration to file
    void save(const std::string& output_file);
    
    // Print configuration summary
    void printSummary() const;

private:
    Configuration() = default;
    std::map<std::string, std::any> params_;
};

// Execution model parameters
struct ExecutionParams {
    uint32_t num_compute_units;
    uint32_t threads_per_warp;
    uint32_t max_warps_per_cu;
    uint32_t max_threads_per_cu;
    
    static ExecutionParams fromConfig(const Configuration& config);
};

// Memory configuration parameters
struct MemoryParams {
    uint32_t shared_memory_size;
    uint32_t global_memory_size;
    uint32_t cache_line_size;
    uint32_t l1_cache_size;
    uint32_t l2_cache_size;
    
    static MemoryParams fromConfig(const Configuration& config);
};

// Scheduler configuration parameters
struct SchedulerParams {
    std::string policy;
    bool enable_optimization;
    uint32_t batch_size;
    
    static SchedulerParams fromConfig(const Configuration& config);
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_CONFIG_H
