#============================================================================
# GPS测速仪 - Makefile
#============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O0
CFLAGS += -I. -Iconfig -Iplatform -Ilib -Iapp

# 源文件
APP_SRC = app/app_main.c app/app_speed.c app/app_display.c \
          app/app_ble_hid.c app/app_key.c
LIB_SRC = lib/nmea_parser.c

# PC模拟目标
SIM_SRC = platform/platform_sim/hal_sim.c platform/platform_sim/sim_main.c
SIM_OBJ = $(patsubst %.c, build/%.o, $(APP_SRC) $(LIB_SRC) $(SIM_SRC))
SIM_BIN = build/gps_sim

# 默认目标
.PHONY: all sim clean test

all: sim

sim: $(SIM_BIN)
	@echo "编译完成: $(SIM_BIN)"
	@echo "运行: $(SIM_BIN)"

$(SIM_BIN): $(SIM_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ -lm

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DPLATFORM_SIM -c $< -o $@

# 单元测试
TEST_SRC = tests/test_nmea.c tests/test_speed.c tests/test_main.c
TEST_OBJ = $(patsubst %.c, build/%.o, $(LIB_SRC) app/app_speed.c \
            platform/platform_sim/hal_sim.c $(TEST_SRC))
TEST_BIN = build/gps_test

test: $(TEST_BIN)
	@echo "运行测试..."
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DPLATFORM_SIM -o $@ $^ -lm

clean:
	rm -rf build/
	rm -f sim_flash.bin
	@echo "清理完成"
