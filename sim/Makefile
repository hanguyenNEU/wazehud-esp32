# Simulator HUD trên macOS. Biên dịch chính mã sẽ chạy trên ESP32.
ROOT   := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
LVGL   := $(ROOT)/firmware/managed_components/lvgl__lvgl
MAIN   := $(ROOT)/firmware/main
# cJSON: IDF 6.x gỡ components/json; ưu tiên managed_components (dep
# espressif/cjson), fallback đường dẫn cũ của IDF <=5.x.
CJSON  := $(if $(wildcard $(ROOT)/firmware/managed_components/espressif__cjson/cJSON/cJSON.c),$(ROOT)/firmware/managed_components/espressif__cjson/cJSON,$(if $(IDF_PATH),$(IDF_PATH),$(HOME)/esp/esp-idf)/components/json/cJSON)
BUILD  := $(ROOT)/sim/build

# LVGL include <SDL2/SDL.h>, mà pkg-config chỉ trả thư mục .../include/SDL2,
# nên phải thêm cả thư mục cha.
# sdl2-config trả -I.../include/SDL2 nên `#include <SDL2/SDL.h>` không thấy;
# phải thêm cả thư mục cha. macOS không có sẵn pkg-config nên dùng sdl2-config.
SDL_PREFIX := $(shell sdl2-config --prefix 2>/dev/null)
SDL_CFLAGS := $(shell sdl2-config --cflags) $(if $(SDL_PREFIX),-I$(SDL_PREFIX)/include,)
SDL_LIBS   := $(shell sdl2-config --libs)

# Phải khớp board đang dùng, nếu không board.h rơi về mặc định 240x240 và
# simulator vẽ sai kích thước so với phần cứng thật.
#   make BOARD=ES3C28P   (mặc định — bo 2.8" đang dùng)
BOARD ?= ES3C28P

CFLAGS := -std=gnu17 -O1 -g -Wall \
  -DBOARD_$(BOARD) \
  -DLV_CONF_INCLUDE_SIMPLE -I$(ROOT)/sim \
  -I$(LVGL) -I$(LVGL)/src \
  -I$(MAIN)/ui -I$(MAIN)/app -I$(MAIN)/hlp -I$(MAIN)/system -I$(CJSON) \
  $(SDL_CFLAGS)

SRC := $(ROOT)/sim/main.c \
       $(MAIN)/ui/ui_navigator.c \
       $(MAIN)/ui/ui_router.c \
       $(MAIN)/ui/ui_system.c \
       $(MAIN)/ui/ui_clock.c \
       $(MAIN)/ui/ui_menu.c \
       $(MAIN)/ui/ui_wifi.c \
       $(MAIN)/ui/ui_settings.c \
       $(MAIN)/ui/ui_trip.c \
       $(MAIN)/ui/ui_input.c \
       $(MAIN)/ui/anim_bg.c \
       $(MAIN)/ui/ui_maneuver.c \
       $(MAIN)/system/gesture_router.c \
       $(MAIN)/system/lunar_calendar.c \
       $(wildcard $(MAIN)/ui/assets/*.c) \
       $(MAIN)/app/hud_decode.c \
       $(MAIN)/app/hud_alerts.c \
       $(MAIN)/hlp/hlp_core.c \
       $(CJSON)/cJSON.c \
       $(shell find $(LVGL)/src -name '*.c')

# Header của project cũng phải là phụ thuộc. Trước đây rule chỉ liệt kê $(SRC)
# nên sửa một file .h là make báo "đã mới nhất" rồi chạy binary CŨ — không lỗi,
# không cảnh báo, chỉ âm thầm cho kết quả sai. Không glob header của LVGL vì
# chúng gần như không đổi mà lại làm chậm mỗi lần gọi make.

# CLT SDK 27 có tbd định dạng mới mà ld của CLT chưa đọc được; dùng SDK của Xcode.
SDKROOT ?= $(if $(wildcard /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk),/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk,)
export SDKROOT
HDR := $(wildcard $(MAIN)/ui/*.h) $(wildcard $(MAIN)/app/*.h) \
       $(wildcard $(MAIN)/hlp/*.h) $(wildcard $(MAIN)/system/*.h) $(ROOT)/sim/lv_conf.h $(ROOT)/sim/nvs.h

$(BUILD)/hudsim: $(SRC) $(HDR)
	@mkdir -p $(BUILD)
	@echo "── biên dịch $(words $(SRC)) file"
	@$(CC) $(CFLAGS) $(SRC) $(SDL_LIBS) -o $@
	@echo "→ $@"

run: $(BUILD)/hudsim
	$(BUILD)/hudsim $(ARGS)

clean:
	rm -rf $(BUILD)

.PHONY: run clean
