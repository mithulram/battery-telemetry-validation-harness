#include "validator.hpp"

#include <gtest/gtest.h>

namespace bms {
namespace {

TelemetryRecord make_record(const std::string& timestamp,
                            const std::string& cell_id,
                            double voltage,
                            double temperature,
                            double soc) {
    TelemetryRecord record;
    record.timestamp_iso = timestamp;
    record.cell_id = cell_id;
    record.voltage_v = voltage;
    record.temperature_c = temperature;
    record.soc_percent = soc;
    record.has_voltage = true;
    record.has_temperature = true;
    record.has_soc = true;
    return record;
}

}  // namespace

TEST(ValidatorRules, AcceptsValidIso8601Timestamp) {
    EXPECT_TRUE(is_valid_iso8601_timestamp("2024-06-01T12:30:45Z"));
    EXPECT_FALSE(is_valid_iso8601_timestamp("2024-06-01 12:30:45"));
    EXPECT_FALSE(is_valid_iso8601_timestamp(""));
}

TEST(ValidatorRules, VoltageRange) {
    EXPECT_TRUE(is_voltage_in_range(3.7));
    EXPECT_FALSE(is_voltage_in_range(2.4));
    EXPECT_FALSE(is_voltage_in_range(4.3));
}

TEST(ValidatorRules, TemperatureRange) {
    EXPECT_TRUE(is_temperature_in_range(25.0));
    EXPECT_FALSE(is_temperature_in_range(-21.0));
    EXPECT_FALSE(is_temperature_in_range(61.0));
}

TEST(ValidatorRules, SocRange) {
    EXPECT_TRUE(is_soc_in_range(50.0));
    EXPECT_FALSE(is_soc_in_range(-1.0));
    EXPECT_FALSE(is_soc_in_range(101.0));
}

TEST(ValidatorRules, RejectsMissingTimestamp) {
    auto record = make_record("", "CELL-01", 3.7, 25.0, 80.0);
    const ValidationReport report = validate_batch({record});
    ASSERT_EQ(report.invalid_records, 1u);
    ASSERT_FALSE(report.rule_violations.empty());
    EXPECT_EQ(report.rule_violations.front().rule, "timestamp_required");
}

TEST(ValidatorRules, RejectsBadTimestampFormat) {
    auto record = make_record("not-a-timestamp", "CELL-01", 3.7, 25.0, 80.0);
    const ValidationReport report = validate_batch({record});
    ASSERT_EQ(report.invalid_records, 1u);
    EXPECT_EQ(report.rule_violations.front().rule, "timestamp_format");
}

TEST(ValidatorRules, RejectsMissingCellId) {
    auto record = make_record("2024-06-01T12:00:00Z", "", 3.7, 25.0, 80.0);
    const ValidationReport report = validate_batch({record});
    ASSERT_EQ(report.invalid_records, 1u);
    EXPECT_EQ(report.rule_violations.front().rule, "cell_id_required");
}

TEST(ValidatorRules, RejectsOutOfRangeVoltage) {
    auto record = make_record("2024-06-01T12:00:00Z", "CELL-01", 5.0, 25.0, 80.0);
    const ValidationReport report = validate_batch({record});
    ASSERT_EQ(report.invalid_records, 1u);
    EXPECT_EQ(report.rule_violations.front().rule, "voltage_range");
}

TEST(ValidatorRules, RejectsDuplicateReading) {
    const std::vector<TelemetryRecord> records = {
        make_record("2024-06-01T12:00:00Z", "CELL-01", 3.7, 25.0, 80.0),
        make_record("2024-06-01T12:00:00Z", "CELL-01", 3.6, 25.0, 79.0),
    };
    const ValidationReport report = validate_batch(records);
    ASSERT_EQ(report.invalid_records, 1u);
    EXPECT_EQ(report.rule_violations.front().rule, "duplicate_reading");
}

TEST(ValidatorRules, RejectsNonMonotonicTimestampsPerCell) {
    const std::vector<TelemetryRecord> records = {
        make_record("2024-06-01T12:00:00Z", "CELL-01", 3.7, 25.0, 80.0),
        make_record("2024-06-01T11:00:00Z", "CELL-01", 3.6, 25.0, 79.0),
    };
    const ValidationReport report = validate_batch(records);
    ASSERT_EQ(report.invalid_records, 1u);
    EXPECT_EQ(report.rule_violations.front().rule, "non_monotonic_timestamp");
}

TEST(ValidatorRules, ValidBatchPassesAllRules) {
    const std::vector<TelemetryRecord> records = {
        make_record("2024-06-01T12:00:00Z", "CELL-01", 3.7, 25.0, 80.0),
        make_record("2024-06-01T12:01:00Z", "CELL-01", 3.69, 25.1, 79.5),
        make_record("2024-06-01T12:00:00Z", "CELL-02", 3.65, 24.0, 75.0),
    };
    const ValidationReport report = validate_batch(records);
    EXPECT_EQ(report.total_records, 3u);
    EXPECT_EQ(report.valid_records, 3u);
    EXPECT_EQ(report.invalid_records, 0u);
    EXPECT_DOUBLE_EQ(report.pass_rate, 1.0);
}

TEST(IoHelpers, LoadsJsonRecords) {
    const std::vector<TelemetryRecord> records =
        load_json("../../examples/sample_telemetry.json");
    ASSERT_EQ(records.size(), 2u);
    const ValidationReport report = validate_batch(records);
    EXPECT_EQ(report.total_records, 2u);
    EXPECT_EQ(report.invalid_records, 1u);
    EXPECT_EQ(report.rule_violations.front().rule, "voltage_range");
}

TEST(IoHelpers, LoadsCsvAndProducesJsonReport) {
    const std::vector<TelemetryRecord> records = load_csv("../../examples/sample_telemetry.csv");
    ASSERT_EQ(records.size(), 5u);
    const ValidationReport report = validate_batch(records);
    const std::string json = report_to_json(report);
    EXPECT_NE(json.find("\"total_records\": 5"), std::string::npos);
    EXPECT_NE(json.find("\"valid_records\": 4"), std::string::npos);
    EXPECT_NE(json.find("\"invalid_records\": 1"), std::string::npos);
}

}  // namespace bms
