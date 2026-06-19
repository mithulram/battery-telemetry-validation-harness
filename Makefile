BUILD_DIR := cpp/build
BIN := $(BUILD_DIR)/bms-validate
ARTIFACTS := artifacts

.PHONY: all build test demo clean

all: build test

build:
	cmake -S cpp -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) --parallel

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

demo: build
	mkdir -p $(ARTIFACTS) examples docs/screenshots
	python3 python/generate_telemetry.py --output examples/generated.csv --records 20 --inject-faults 3
	-$(BIN) --input examples/sample_telemetry.csv --output $(ARTIFACTS)/sample_report.json --format csv
	-$(BIN) --input examples/generated.csv --output $(ARTIFACTS)/generated_report.json --format csv
	python3 python/render_report_html.py --input $(ARTIFACTS)/sample_report.json --output $(ARTIFACTS)/validation-report.html
	@echo "Demo complete. Reports written to $(ARTIFACTS)/"

clean:
	rm -rf $(BUILD_DIR) $(ARTIFACTS)
