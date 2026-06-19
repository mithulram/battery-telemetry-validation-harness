#include "validator.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace bms {
namespace {

constexpr double kMinVoltage = 2.5;
constexpr double kMaxVoltage = 4.25;
constexpr double kMinTemperature = -20.0;
constexpr double kMaxTemperature = 60.0;
constexpr double kMinSoc = 0.0;
constexpr double kMaxSoc = 100.0;

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string escape_json(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

bool parse_double(const std::string& text, double& out) {
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        out = std::stod(text, &consumed);
        return consumed == text.size();
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
            continue;
        }
        if (ch == ',' && !in_quotes) {
            fields.push_back(trim(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    if (in_quotes) {
        throw std::runtime_error("Unterminated quoted field in CSV line");
    }
    fields.push_back(trim(current));
    return fields;
}

void add_violation(ValidationReport& report,
                   std::unordered_set<std::size_t>& quarantined,
                   const std::string& rule,
                   std::size_t record_index,
                   const std::string& cell_id,
                   const std::string& detail) {
    report.rule_violations.push_back({rule, record_index, cell_id, detail});
    quarantined.insert(record_index);
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to open file: " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::size_t skip_whitespace(const std::string& text, std::size_t pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
        ++pos;
    }
    return pos;
}

std::string parse_json_string(const std::string& text, std::size_t& pos) {
    if (pos >= text.size() || text[pos] != '"') {
        throw std::runtime_error("Expected JSON string at position " + std::to_string(pos));
    }
    ++pos;
    std::string value;
    while (pos < text.size()) {
        const char ch = text[pos++];
        if (ch == '"') {
            return value;
        }
        if (ch == '\\') {
            if (pos >= text.size()) {
                throw std::runtime_error("Invalid JSON escape sequence");
            }
            const char escaped = text[pos++];
            switch (escaped) {
                case '"':
                    value.push_back('"');
                    break;
                case '\\':
                    value.push_back('\\');
                    break;
                case '/':
                    value.push_back('/');
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case 'u':
                    throw std::runtime_error(
                        "Unicode escape sequences are not supported in telemetry JSON");
                default:
                    throw std::runtime_error("Unsupported JSON escape sequence");
            }
            continue;
        }
        value.push_back(ch);
    }
    throw std::runtime_error("Unterminated JSON string");
}

double parse_json_number(const std::string& text, std::size_t& pos) {
    const std::size_t start = pos;
    if (text[pos] == '-') {
        ++pos;
    }
    while (pos < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[pos])) != 0 || text[pos] == '.')) {
        ++pos;
    }
    double value = 0.0;
    if (!parse_double(text.substr(start, pos - start), value)) {
        throw std::runtime_error("Invalid JSON number at position " + std::to_string(start));
    }
    return value;
}

void expect_char(const std::string& text, std::size_t& pos, char expected) {
    pos = skip_whitespace(text, pos);
    if (pos >= text.size() || text[pos] != expected) {
        throw std::runtime_error(std::string("Expected '") + expected + "' in JSON input");
    }
    ++pos;
}

TelemetryRecord parse_json_object(const std::string& text, std::size_t& pos) {
    TelemetryRecord record;
    expect_char(text, pos, '{');

    bool first = true;
    while (true) {
        pos = skip_whitespace(text, pos);
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            break;
        }
        if (!first) {
            expect_char(text, pos, ',');
            pos = skip_whitespace(text, pos);
        }
        first = false;

        const std::string key = parse_json_string(text, pos);
        expect_char(text, pos, ':');
        pos = skip_whitespace(text, pos);

        if (key == "timestamp_iso") {
            record.timestamp_iso = parse_json_string(text, pos);
        } else if (key == "cell_id") {
            record.cell_id = parse_json_string(text, pos);
        } else if (key == "voltage_v") {
            record.voltage_v = parse_json_number(text, pos);
            record.has_voltage = true;
        } else if (key == "temperature_c") {
            record.temperature_c = parse_json_number(text, pos);
            record.has_temperature = true;
        } else if (key == "soc_percent") {
            record.soc_percent = parse_json_number(text, pos);
            record.has_soc = true;
        } else if (text[pos] == '"') {
            (void)parse_json_string(text, pos);
        } else if (text[pos] == '{' || text[pos] == '[') {
            throw std::runtime_error("Nested JSON values are not supported for telemetry records");
        } else {
            (void)parse_json_number(text, pos);
        }
    }

    return record;
}

}  // namespace

namespace {

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(int year, int month) {
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return kDays[month - 1];
}

int parse_fixed_width_int(const std::string& text, std::size_t start, std::size_t length) {
    if (start + length > text.size()) {
        return -1;
    }
    int value = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const char ch = text[start + i];
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return -1;
        }
        value = value * 10 + (ch - '0');
    }
    return value;
}

}  // namespace

bool is_valid_iso8601_timestamp(const std::string& timestamp) {
    if (timestamp.size() != 20) {
        return false;
    }
    if (timestamp[4] != '-' || timestamp[7] != '-' || timestamp[10] != 'T' ||
        timestamp[13] != ':' || timestamp[16] != ':' || timestamp[19] != 'Z') {
        return false;
    }

    for (std::size_t i = 0; i < timestamp.size(); ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16 || i == 19) {
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(timestamp[i]))) {
            return false;
        }
    }

    const int year = parse_fixed_width_int(timestamp, 0, 4);
    const int month = parse_fixed_width_int(timestamp, 5, 2);
    const int day = parse_fixed_width_int(timestamp, 8, 2);
    const int hour = parse_fixed_width_int(timestamp, 11, 2);
    const int minute = parse_fixed_width_int(timestamp, 14, 2);
    const int second = parse_fixed_width_int(timestamp, 17, 2);

    if (year < 0 || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59 ||
        second > 59) {
        return false;
    }

    const int max_day = days_in_month(year, month);
    return day <= max_day;
}

bool is_voltage_in_range(double voltage_v) {
    return voltage_v >= kMinVoltage && voltage_v <= kMaxVoltage;
}

bool is_temperature_in_range(double temperature_c) {
    return temperature_c >= kMinTemperature && temperature_c <= kMaxTemperature;
}

bool is_soc_in_range(double soc_percent) {
    return soc_percent >= kMinSoc && soc_percent <= kMaxSoc;
}

ValidationReport validate_batch(const std::vector<TelemetryRecord>& records) {
    ValidationReport report;
    report.total_records = records.size();
    std::unordered_set<std::size_t> quarantined;

    std::map<std::pair<std::string, std::string>, std::size_t> seen_pairs;
    std::unordered_map<std::string, std::string> last_timestamp_by_cell;

    for (std::size_t index = 0; index < records.size(); ++index) {
        const TelemetryRecord& record = records[index];
        const std::string cell_id = record.cell_id;

        if (trim(record.timestamp_iso).empty()) {
            add_violation(report, quarantined, "timestamp_required", index, cell_id,
                          "timestamp_iso is required");
        } else if (!is_valid_iso8601_timestamp(record.timestamp_iso)) {
            add_violation(report, quarantined, "timestamp_format", index, cell_id,
                          "timestamp_iso must match YYYY-MM-DDTHH:MM:SSZ");
        }

        if (trim(record.cell_id).empty()) {
            add_violation(report, quarantined, "cell_id_required", index, cell_id,
                          "cell_id is required and must be non-empty");
        }

        if (!record.has_voltage) {
            add_violation(report, quarantined, "voltage_missing", index, cell_id,
                          "voltage_v is required");
        } else if (!is_voltage_in_range(record.voltage_v)) {
            add_violation(report, quarantined, "voltage_range", index, cell_id,
                          "voltage_v must be within [2.5, 4.25] volts");
        }

        if (!record.has_temperature) {
            add_violation(report, quarantined, "temperature_missing", index, cell_id,
                          "temperature_c is required");
        } else if (!is_temperature_in_range(record.temperature_c)) {
            add_violation(report, quarantined, "temperature_range", index, cell_id,
                          "temperature_c must be within [-20, 60] Celsius");
        }

        if (!record.has_soc) {
            add_violation(report, quarantined, "soc_missing", index, cell_id,
                          "soc_percent is required");
        } else if (!is_soc_in_range(record.soc_percent)) {
            add_violation(report, quarantined, "soc_range", index, cell_id,
                          "soc_percent must be within [0, 100]");
        }

        if (!record.timestamp_iso.empty() && !record.cell_id.empty()) {
            const auto key = std::make_pair(record.timestamp_iso, record.cell_id);
            const auto existing = seen_pairs.find(key);
            if (existing != seen_pairs.end()) {
                add_violation(report, quarantined, "duplicate_reading", index, cell_id,
                              "duplicate (timestamp_iso, cell_id) pair at index " +
                                  std::to_string(existing->second));
            } else {
                seen_pairs.emplace(key, index);
            }
        }

        if (!record.cell_id.empty() && !record.timestamp_iso.empty() &&
            is_valid_iso8601_timestamp(record.timestamp_iso)) {
            const auto previous = last_timestamp_by_cell.find(record.cell_id);
            if (previous != last_timestamp_by_cell.end() &&
                record.timestamp_iso < previous->second) {
                add_violation(report, quarantined, "non_monotonic_timestamp", index, cell_id,
                              "timestamp_iso must be non-decreasing for cell_id " + record.cell_id);
            }
            last_timestamp_by_cell[record.cell_id] = record.timestamp_iso;
        }
    }

    report.quarantined_record_indices.assign(quarantined.begin(), quarantined.end());
    std::sort(report.quarantined_record_indices.begin(), report.quarantined_record_indices.end());
    report.invalid_records = report.quarantined_record_indices.size();
    report.valid_records = report.total_records - report.invalid_records;
    report.pass_rate =
        report.total_records == 0 ? 0.0
                                  : static_cast<double>(report.valid_records) /
                                        static_cast<double>(report.total_records);
    return report;
}

std::vector<TelemetryRecord> load_csv(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to open CSV file: " + path);
    }

    std::vector<TelemetryRecord> records;
    std::string line;
    if (!std::getline(input, line)) {
        return records;
    }

    const auto header_fields = split_csv_line(line);
    std::unordered_map<std::string, std::size_t> column_index;
    for (std::size_t i = 0; i < header_fields.size(); ++i) {
        column_index[header_fields[i]] = i;
    }

    const auto require_column = [&](const char* name) -> std::size_t {
        const auto it = column_index.find(name);
        if (it == column_index.end()) {
            throw std::runtime_error(std::string("Missing required CSV column: ") + name);
        }
        return it->second;
    };

    const std::size_t timestamp_idx = require_column("timestamp_iso");
    const std::size_t cell_idx = require_column("cell_id");
    const std::size_t voltage_idx = require_column("voltage_v");
    const std::size_t temperature_idx = require_column("temperature_c");
    const std::size_t soc_idx = require_column("soc_percent");

    while (std::getline(input, line)) {
        if (trim(line).empty()) {
            continue;
        }
        const auto fields = split_csv_line(line);
        auto field_at = [&](std::size_t idx) -> std::string {
            return idx < fields.size() ? fields[idx] : "";
        };

        TelemetryRecord record;
        record.timestamp_iso = field_at(timestamp_idx);
        record.cell_id = field_at(cell_idx);

        const std::string voltage_text = field_at(voltage_idx);
        if (!voltage_text.empty() && parse_double(voltage_text, record.voltage_v)) {
            record.has_voltage = true;
        }

        const std::string temperature_text = field_at(temperature_idx);
        if (!temperature_text.empty() && parse_double(temperature_text, record.temperature_c)) {
            record.has_temperature = true;
        }

        const std::string soc_text = field_at(soc_idx);
        if (!soc_text.empty() && parse_double(soc_text, record.soc_percent)) {
            record.has_soc = true;
        }

        records.push_back(record);
    }

    return records;
}

std::vector<TelemetryRecord> load_json(const std::string& path) {
    const std::string text = read_file(path);
    std::size_t pos = skip_whitespace(text, 0);
    expect_char(text, pos, '[');

    std::vector<TelemetryRecord> records;
    bool first = true;
    while (true) {
        pos = skip_whitespace(text, pos);
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            break;
        }
        if (!first) {
            expect_char(text, pos, ',');
            pos = skip_whitespace(text, pos);
        }
        first = false;
        records.push_back(parse_json_object(text, pos));
    }

    pos = skip_whitespace(text, pos);
    if (pos != text.size()) {
        throw std::runtime_error("Unexpected trailing content in JSON input");
    }
    return records;
}

std::string report_to_json(const ValidationReport& report) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"total_records\": " << report.total_records << ",\n";
    out << "  \"valid_records\": " << report.valid_records << ",\n";
    out << "  \"invalid_records\": " << report.invalid_records << ",\n";
    out << "  \"pass_rate\": " << report.pass_rate << ",\n";
    out << "  \"rule_violations\": [\n";
    for (std::size_t i = 0; i < report.rule_violations.size(); ++i) {
        const auto& violation = report.rule_violations[i];
        out << "    {\"rule\": \"" << escape_json(violation.rule) << "\", "
            << "\"record_index\": " << violation.record_index << ", "
            << "\"cell_id\": \"" << escape_json(violation.cell_id) << "\", "
            << "\"detail\": \"" << escape_json(violation.detail) << "\"}";
        if (i + 1 < report.rule_violations.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"quarantined_record_indices\": [";
    for (std::size_t i = 0; i < report.quarantined_record_indices.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << report.quarantined_record_indices[i];
    }
    out << "]\n";
    out << "}\n";
    return out.str();
}

}  // namespace bms
