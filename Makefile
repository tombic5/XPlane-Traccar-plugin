# Makefile pre X-Plane plugin pre OsmAnd

# Premenné
PLUGIN_NAME = TraccarPlugin
PLUGIN_FILE = $(PLUGIN_NAME).xpl
SOURCE_DIR = src
BUILD_DIR = build
SOURCE_FILES = $(SOURCE_DIR)/traccar.cpp
OBJECT_FILES = $(BUILD_DIR)/traccar.o

# SDK paths
SDK_PATH = SDK
XPLM_INCLUDE_PATH = $(SDK_PATH)/CHeaders/XPLM
WIDGETS_INCLUDE_PATH = $(SDK_PATH)/CHeaders/Widgets
XPLM_FRAMEWORK_PATH = $(SDK_PATH)/Libraries/Mac/XPLM.framework

# Compiler and flags
CC = clang++
CFLAGS = -Wall -O2 -fPIC -std=c++11 -stdlib=libc++
CXXFLAGS = $(CFLAGS) -DXPLM_MACOSX=1 -DAPL=1 -DIBM=0 -DLIN=0

# Include paths
INCLUDES = -I$(XPLM_INCLUDE_PATH) -I$(WIDGETS_INCLUDE_PATH)

LIBS = -lcurl
# Framework paths (using the framework instead of library)
FRAMEWORKS = -F$(dir $(XPLM_FRAMEWORK_PATH)) -framework XPLM

# Build target
$(PLUGIN_FILE): $(OBJECT_FILES)
	$(CC) -dynamiclib $(OBJECT_FILES) $(FRAMEWORKS) $(LIBS) -o $(PLUGIN_FILE)

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Inštalácia
install: $(PLUGIN_FILE)
	cp $(PLUGIN_FILE) ~/Library/Application\ Support/X-Plane/Resources/plugins/

# Clean
clean:
	rm -rf $(BUILD_DIR) $(PLUGIN_FILE)

.PHONY: all clean install
all: $(PLUGIN_FILE)

