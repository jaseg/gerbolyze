
CXX := clang++
LD := ld
PKG_CONFIG ?= pkg-config

BUILDDIR ?= build
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
	src/out_svg.cpp \
	src/out_gerber.cpp \
	src/out_sexp.cpp \
	src/out_flattener.cpp \
	src/out_dilater.cpp \
	src/lambda_sink.cpp \
	$(UPSTREAM_DIR)/cpp-base64/base64.cpp

CLIPPER_SOURCES ?= $(UPSTREAM_DIR)/clipper-6.4.2/cpp/clipper.cpp $(UPSTREAM_DIR)/clipper-6.4.2/cpp/cpp_cairo/cairo_clipper.cpp
CLIPPER_INCLUDES 	?= -I$(UPSTREAM_DIR)/clipper-6.4.2/cpp -I$(UPSTREAM_DIR)/clipper-6.4.2/cpp/cpp_cairo/
VORONOI_INCLUDES 	?= -I$(UPSTREAM_DIR)/voronoi/src
POISSON_INCLUDES 	?= -I$(UPSTREAM_DIR)/poisson-disk-sampling/thinks/poisson_disk_sampling/
BASE64_INCLUDES 	?= -I$(UPSTREAM_DIR)/cpp-base64
ARGAGG_INCLUDES 	?= -I$(UPSTREAM_DIR)/argagg/include/argagg
CAVC_INCLUDES 		?= -I$(UPSTREAM_DIR)/CavalierContours/include/cavc/
SUBPROCESS_INCLUDES	?= -I$(UPSTREAM_DIR)/subprocess.h

SOURCES += $(CLIPPER_SOURCES)
INCLUDES := -Iinclude -Isrc $(CLIPPER_INCLUDES) $(VORONOI_INCLUDES) $(POISSON_INCLUDES) $(BASE64_INCLUDES) $(ARGAGG_INCLUDES) $(CAVC_INCLUDES) $(SUBPROCESS_INCLUDES)

CXXFLAGS := -std=c++2a -g -Wall -Wextra -O0
CXXFLAGS += $(shell $(PKG_CONFIG) --cflags pangocairo pugixml opencv4) 

LDFLAGS := -lm -lc -lstdc++
LDFLAGS += $(shell $(PKG_CONFIG) --libs pangocairo pugixml opencv4)

TARGET := svg-flatten

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(dir $@) 
	$(CXX) -c $(CXXFLAGS) $(CXXFLAGS) $(INCLUDES) -o $@ $^

$(BUILDDIR)/$(TARGET): $(SOURCES:%.cpp=$(BUILDDIR)/%.o)
	@mkdir -p $(dir $@) 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^
	
.PHONY: clean
clean:
	rm -rf $(BUILDDIR)
