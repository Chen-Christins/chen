# 定义时间记录函数
define time_wrapper
	@echo "开始时间: $$(date '+%Y-%m-%d %H:%M:%S.%3N')" && \
	start_time=$$(date +%s%3N) && \
	$(1) && \
	end_time=$$(date +%s%3N) && \
	duration=$$((end_time - start_time)) && \
	echo "结束时间: $$(date '+%Y-%m-%d %H:%M:%S.%3N')" && \
	echo "总计耗时: $$(echo "scale=3; $$duration/1000" | bc) 秒"
endef

.PHONY: xx
xx:
	$(call time_wrapper, \
		if [ -d "build" ]; then \
			cd build && cmake .. && $(MAKE); \
		else \
			mkdir build; \
			cd build && cmake ..; \
		fi)

%:
	$(call time_wrapper, \
		if [ -d "build" ]; then \
			cd build && cmake .. && +$(MAKE) $@; \
		else \
			mkdir build; \
			cd build && cmake ..; \
		fi)

clean:
	$(call time_wrapper, \
		if [ -d "build" ]; then \
			cd build && $(MAKE) clean && cd ..; \
		fi)

# 运行单元测试 + 冒烟用例（需先在 Debug 下构建：make）
.PHONY: test
test:
	$(call time_wrapper, \
		if [ ! -d "build" ]; then \
			echo "build 目录不存在，请先执行 make"; \
			exit 1; \
		fi; \
		if [ ! -f "build/CTestTestfile.cmake" ]; then \
			echo "当前构建未启用测试，请用 Debug 构建或 -DCHEN_BUILD_TESTS=ON 重新 configure"; \
			exit 1; \
		fi; \
		cd build && ctest -L "unit|smoke" --output-on-failure)

# 运行 CTest 中注册的全部用例
.PHONY: test-all
test-all:
	$(call time_wrapper, \
		if [ ! -d "build" ]; then \
			echo "build 目录不存在，请先执行 make"; \
			exit 1; \
		fi; \
		if [ ! -f "build/CTestTestfile.cmake" ]; then \
			echo "当前构建未启用测试，请用 Debug 构建或 -DCHEN_BUILD_TESTS=ON 重新 configure"; \
			exit 1; \
		fi; \
		cd build && ctest --output-on-failure)

xlsx2bin:
	$(call time_wrapper, python3 tools/scripts/xlsx_to_bin.py tools/xlsx/*.xlsx -o tools/data/data.bin)

xlsx2bin_separate:
	$(call time_wrapper, python3 tools/scripts/xlsx_to_bin.py tools/xlsx/*.xlsx -o tools/data/)

protocol:
	$(call time_wrapper, $(MAKE) -C protocol)

protocol-%:
	$(call time_wrapper, $(MAKE) -C protocol $*)

.PHONY: sdk
sdk:
	@sh package_sdk.sh -v $(if $(version),$(version),1.0.0)

.PHONY: deps deps-sync deps-build deps-package deps-release deps-clean
deps: deps-sync

deps-sync:
	@python3 deps.py sync

deps-build:
	@python3 deps.py build

deps-package:
	@python3 deps.py package

deps-release:
	@python3 deps.py release

deps-clean:
	@python3 deps.py clean
