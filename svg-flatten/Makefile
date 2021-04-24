
CXX := clang++
LD ?= ld
INSTALL := install
PKG_CONFIG ?= pkg-config

BUILDDIR ?= build
PREFIX ?= /usr/local
UPSTREAM_DIR ?= ../upstream

SOURCES := src/svg_color.cpp \
	src/svg_doc.cpp \
	src/svg_geom.cpp \
	src/svg_import_util.cpp \
	src/svg_path.cpp \
	src/svg_pattern.cpp \
	src/vec_core.cpp \
	src/vec_grid.cpp \
	src/main.cpp \
	src/flatten.cpp \
	src/out_svg.cpp \
	src/out_gerber.cpp \
	src/out_sexp.cpp \
	src/out_flattener.cpp \
	src/out_dilater.cpp \
	src/lambda_sink.cpp \
	src/flatten.cpp \
	$(UPSTREAM_DIR)/cpp-base64/base64.cpp

CLIPPER_SOURCES ?= $(UPSTREAM_DIR)/clipper-6.4.2/cpp/clipper.cpp
CLIPPER_INCLUDES 	?= -I$(UPSTREAM_DIR)/clipper-6.4.2/cpp
VORONOI_INCLUDES 	?= -I$(UPSTREAM_DIR)/voronoi/src
POISSON_INCLUDES 	?= -I$(UPSTREAM_DIR)/poisson-disk-sampling/thinks/poisson_disk_sampling/
BASE64_INCLUDES 	?= -I$(UPSTREAM_DIR)/cpp-base64
ARGAGG_INCLUDES 	?= -I$(UPSTREAM_DIR)/argagg/include/argagg
CAVC_INCLUDES 		?= -I$(UPSTREAM_DIR)/CavalierContours/include/cavc/
SUBPROCESS_INCLUDES	?= -I$(UPSTREAM_DIR)/subprocess.h

SOURCES += $(CLIPPER_SOURCES)
INCLUDES := -Iinclude -Isrc $(CLIPPER_INCLUDES) $(VORONOI_INCLUDES) $(POISSON_INCLUDES) $(BASE64_INCLUDES) $(ARGAGG_INCLUDES) $(CAVC_INCLUDES) $(SUBPROCESS_INCLUDES)

PKG_CONFIG_DEPS := pugixml
CXXFLAGS := -std=c++2a -g -Wall -Wextra -O0
CXXFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKG_CONFIG_DEPS)) 
# hack for stone age opencv in debian stable
CXXFLAGS += $(shell $(PKG_CONFIG) --cflags opencv4 2> /dev/null || $(PKG_CONFIG) --cflags opencv 2>/dev/null) 

LDFLAGS := -lm -lc -lstdc++
LDFLAGS += $(shell $(PKG_CONFIG) --libs $(PKG_CONFIG_DEPS))
# debian hack. see above.
OPENCV_LDFLAGS := $(shell $(PKG_CONFIG) --libs opencv4 2> /dev/null || $(PKG_CONFIG) --libs opencv 2>/dev/null) 
LDFLAGS += $(shell echo $(OPENCV_LDFLAGS) | sed 's/-l\S\+ //g') -lopencv_core -lopencv_imgproc -lopencv_imgcodecs

TARGET := svg-flatten

all: $(BUILDDIR)/$(TARGET)

.PHONY: check-deps
check-deps:
	@echo
	@$(PKG_CONFIG) --cflags --libs pugixml >/dev/null
	# debian hack. see above.
	@$(PKG_CONFIG) --cflags --libs opencv4 >/dev/null ||$(PKG_CONFIG) --cflags --libs opencv >/dev/null 

$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(dir $@) 
	$(CXX) -c $(CXXFLAGS) $(CXXFLAGS) $(INCLUDES) -o $@ $^

$(BUILDDIR)/$(TARGET): $(SOURCES:%.cpp=$(BUILDDIR)/%.o)
	@mkdir -p $(dir $@) 
	if [ $$(uname -s) = "Darwin" ]; then \
		$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^; \
	else \
		$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ -Wl,--start-group $^ -lstdc++fs -Wl,--end-group; \
	fi

.PHONY: install
install:
	$(INSTALL) $(BUILDDIR)/$(TARGET) $(PREFIX)/bin
	
.PHONY: clean
clean:
	rm -rf $(BUILDDIR)
