# Host (x86) unit-test build for NSPanelManager firmware.
# No ESP32, no emulator: firmware sources compile against ./shims, which stand in
# for the ESP-IDF headers, and against ./fakes, which replace collaborator classes
# at link time.
#
#   make        build and run all tests
#   make clean

IDF     ?= $(HOME)/.platformio/packages/framework-espidf@3.50500.0
FW      := ..
BUILD   := build

PB_INC  := $(IDF)/components/protobuf-c/protobuf-c
LIB_INC := $(patsubst %,-I%,$(wildcard $(FW)/lib/*))

INCLUDES := -Ishims -I$(FW)/include $(LIB_INC) \
            -I$(FW)/managed_components/mittelab__nlohmann-json \
            -I$(PB_INC) -I$(IDF)/components/json/cJSON \
            -I$(FW)/components/bmx280/include

CPPFLAGS := -DBOARD_SONOFF $(INCLUDES)
CXXFLAGS := -std=c++2a -g -O0 -Wall -Wno-attributes -Wno-unused-variable
CFLAGS   := -std=c11 -g -O0 -w

# C sources (protobuf runtime + generated code) must build as C, not C++.
C_SRCS := $(FW)/lib/ProtoBuf/protobuf_nspanel.pb-c.c \
          $(FW)/lib/ProtoBuf/protobuf_nspanel_entity.pb-c.c \
          $(PB_INC)/protobuf-c/protobuf-c.c
C_OBJS := $(addprefix $(BUILD)/,$(notdir $(C_SRCS:.c=.o)))

SUPPORT := shims/host_shims.cpp fakes/fake_collaborators.cpp

# One binary per test file; add the firmware units each test needs here.
TESTS := $(BUILD)/test_room_navigation

.PHONY: all clean
all: $(TESTS)
	@echo; for t in $(TESTS); do echo "--- $$t ---"; ./$$t || exit 1; done

$(BUILD)/test_room_navigation: tests/test_room_navigation.cpp $(SUPPORT) $(FW)/lib/RoomManager/RoomManager.cpp $(C_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(filter %.cpp %.o,$^) -lpthread

$(BUILD)/%.o: $(FW)/lib/ProtoBuf/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/protobuf-c.o: $(PB_INC)/protobuf-c/protobuf-c.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

# Compile-only sweep: how much of the firmware builds on the host right now.
.PHONY: compile-all
compile-all:
	@ok=0; fail=0; \
	for f in $(wildcard $(FW)/lib/*/*.cpp) $(FW)/src/main.cpp; do \
	  case $$f in *ProtoBuf*) continue;; esac; \
	  if $(CXX) -std=c++2a -w -fsyntax-only $$f $(CPPFLAGS) 2>/dev/null; then \
	    echo "  ok   $$f"; ok=$$((ok+1)); \
	  else \
	    echo "  FAIL $$f"; fail=$$((fail+1)); \
	  fi; \
	done; \
	echo; echo "$$ok compile on host, $$fail do not"
