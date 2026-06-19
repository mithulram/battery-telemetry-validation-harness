#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace bms {

struct TelemetryRecord {
    std::string timestamp_iso;
    std::string cell_id;
    double voltage_v = 0.0;
    double temperature_c = 0.0;
    double soc_percent = 0.0;
    bool has_voltage = false;
    bool has_temperature = false;
    bool has_soc = false;
};

struct RuleViolation {
    std::string rule;
    std::size_t record_index = 0;
    std::string cell_id;
    std::string detail;
};

struct ValidationReport {
    std::size_t total_records = 0;
    std::size_t valid_records = 0;
    std::size_t invalid_records = 0;
    double pass_rate = 0.0;
    std::vector<RuleViolation> rule_violations;
    std::vector<std::size_t> quarantined_record_indices;
};

bool is_valid_iso8601_timestamp(const std::string& timestamp);
bool is_voltage_in_range(double voltage_v);
bool is_temperature_in_range(double temperature_c);
bool is_soc_in_range(double soc_percent);

ValidationReport validate_batch(const std::vector<TelemetryRecord>& records);
std::vector<TelemetryRecord> load_csv(const std::string& path);
std::vector<TelemetryRecord> load_json(const std::string& path);
std::string report_to_json(const ValidationReport& report);

}  // namespace bms
