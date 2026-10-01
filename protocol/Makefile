# XML to Header Conversion Makefile
# Usage:
#   make                    - Convert all XML files to headers and format them
#   make convert_only       - Convert XML files without formatting
#   make format_only       - Format existing header files only
#   make xml_to_header INPUT=xml/protocol_userinfo.xml - Convert specific file

# Export library path for all commands
CHEN_SDK_LIB := $(shell for dir in ../chen-sdk-*; do if [ -d "$$dir/lib" ]; then echo "$$dir/lib"; fi; done | head -1)
OTHER_LIBS := $(shell find .. -name "lib" -type d 2>/dev/null | tr '\n' ':' | sed 's/:$$//')
export LD_LIBRARY_PATH := $(OTHER_LIBS):$(CHEN_SDK_LIB):$(LD_LIBRARY_PATH)

# Get current directory name for unified header
DIR_NAME := $(shell basename $(shell pwd))
UNIFIED_HEADER := $(DIR_NAME)_all.h

.PHONY: xml_to_header clean all help convert_only format_only generate_unified_header

# Default target - convert all XML files and format them
all:
	@echo "Converting all XML files to headers..."
	@mkdir -p include
	../tools/bin/converter xml include
	@echo "Generating unified $(UNIFIED_HEADER)..."
	@$(MAKE) generate_unified_header
	@echo "Formatting generated header files..."
	@if [ -f "../tools/scripts/format_code.sh" ]; then \
		../tools/scripts/format_code.sh -v ./include; \
	fi

# Convert XML files only (no formatting)
convert_only:
	@echo "Converting all XML files to headers..."
	@mkdir -p include
	../tools/bin/converter xml include
	@echo "Generating unified $(UNIFIED_HEADER)..."
	@$(MAKE) generate_unified_header

# Format existing header files only
format_only:
	@echo "Formatting existing header files..."
	@if [ -f "../tools/scripts/format_code.sh" ]; then \
		../tools/scripts/format_code.sh -v ./include; \
	else \
		echo "Error: format_code.sh not found"; \
		exit 1; \
	fi

# Convert specific XML to header file
xml_to_header:
	@if [ -z "$(INPUT)" ]; then \
		echo "Error: INPUT parameter is required"; \
		echo "Usage: make xml_to_header INPUT=xml/protocol_userinfo.xml"; \
		echo "Output will be generated in include/ directory"; \
		exit 1; \
	fi
	@echo "Converting $(INPUT)..."
	@mkdir -p include
	@basename=$$(basename "$(INPUT)" .xml); \
	../tools/bin/converter "$(INPUT)" "include/$$basename.h"
	@echo "Formatting generated header file..."
	@if [ -f "../tools/scripts/format_code.sh" ]; then \
		../tools/scripts/format_code.sh -v ./include; \
	fi
	@echo "Updating unified $(UNIFIED_HEADER)..."
	@$(MAKE) generate_unified_header

# Clean generated files
clean:
	@echo "Cleaning generated header files..."
	@rm -rf include/*.h

# Show help
help:
	@echo "Available targets:"
	@echo "  make (all)        - Convert all XML files to headers and format them"
	@echo "  make convert_only - Convert XML files without formatting"
	@echo "  make format_only  - Format existing header files only"
	@echo "  make xml_to_header INPUT=<xml_file>"
	@echo "                     - Convert specific XML file to header"
	@echo "  clean             - Remove generated header files"
	@echo "  help              - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make                          # Convert and format all"
	@echo "  make xml_to_header INPUT=xml/test.xml  # Convert specific file"
	@echo "  make convert_only              # Convert without formatting"
	@echo "  make generate_unified_header - Generate $(UNIFIED_HEADER) from existing headers"

# Generate unified header file
generate_unified_header:
	@echo "Creating unified $(UNIFIED_HEADER)..."
	@mkdir -p include
	@echo "/**" > include/$(UNIFIED_HEADER)
	@echo " * @file $(UNIFIED_HEADER)" >> include/$(UNIFIED_HEADER)
	@echo " * @brief 统一协议头文件 - 包含所有生成的协议头文件" >> include/$(UNIFIED_HEADER)
	@echo " *" >> include/$(UNIFIED_HEADER)
	@echo " * 此文件由Makefile自动生成和维护，包含所有从XML转换而来的协议头文件。" >> include/$(UNIFIED_HEADER)
	@echo " * 业务层只需包含此文件即可使用所有协议定义。" >> include/$(UNIFIED_HEADER)
	@echo " *" >> include/$(UNIFIED_HEADER)
	@echo " * @warning 请勿手动编辑此文件，它会在构建过程中被自动覆盖。" >> include/$(UNIFIED_HEADER)
	@echo " */" >> include/$(UNIFIED_HEADER)
	@echo "#ifndef __$(shell echo $(UNIFIED_HEADER) | tr '.' '_' | tr '-' '_' | tr a-z A-Z)__" >> include/$(UNIFIED_HEADER)
	@echo "#define __$(shell echo $(UNIFIED_HEADER) | tr '.' '_' | tr '-' '_' | tr a-z A-Z)__" >> include/$(UNIFIED_HEADER)
	@echo "" >> include/$(UNIFIED_HEADER)
	@echo "// 包含所有生成的协议头文件" >> include/$(UNIFIED_HEADER)
	@if [ -d "include" ]; then \
		for header in include/*.h; do \
			if [ -f "$$header" ]; then \
				basename=$$(basename "$$header" .h); \
				if [ "$$basename" != "$$(basename "$(UNIFIED_HEADER)" .h)" ]; then \
					echo '#include "'$$basename'.h" // IWYU pragma: keep' >> include/$(UNIFIED_HEADER); \
				fi; \
			fi; \
		done; \
	fi
	@echo "" >> include/$(UNIFIED_HEADER)
	@echo "#endif // __$(shell echo $(UNIFIED_HEADER) | tr '.' '_' | tr '-' '_' | tr a-z A-Z)__" >> include/$(UNIFIED_HEADER)
	@echo "Unified header generated: include/$(UNIFIED_HEADER)"