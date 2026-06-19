#include "validator.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct CliOptions {
    std::string input_path;
    std::string output_path;
    std::string format = "csv";
};

void print_usage(const char* program) {
    std::cerr << "Usage: " << program
              << " --input <path> --output <path> [--format csv|json]\n";
}

CliOptions parse_args(int argc, char* argv[]) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            options.input_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            options.output_path = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            options.format = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (options.input_path.empty() || options.output_path.empty()) {
        print_usage(argv[0]);
        throw std::runtime_error("Both --input and --output are required");
    }
    if (options.format != "csv" && options.format != "json") {
        throw std::runtime_error("--format must be csv or json");
    }
    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const CliOptions options = parse_args(argc, argv);

        std::vector<bms::TelemetryRecord> records;
        if (options.format == "csv") {
            records = bms::load_csv(options.input_path);
        } else {
            records = bms::load_json(options.input_path);
        }

        const bms::ValidationReport report = bms::validate_batch(records);
        const std::string json = bms::report_to_json(report);

        const std::filesystem::path output_path = options.output_path;
        if (output_path.has_parent_path()) {
            std::filesystem::create_directories(output_path.parent_path());
        }

        std::ofstream output(options.output_path);
        if (!output) {
            throw std::runtime_error("Unable to open output file: " + options.output_path);
        }
        output << json;

        std::cout << "Validated " << report.total_records << " records: "
                  << report.valid_records << " valid, " << report.invalid_records
                  << " quarantined (pass rate " << report.pass_rate << ")\n";
        return report.invalid_records > 0 ? 1 : 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 2;
    }
}
