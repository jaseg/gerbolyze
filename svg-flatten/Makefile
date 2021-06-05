
CXX ?= clang++
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
	src/nopencv.cpp \
	$(UPSTREAM_DIR)/cpp-base64/base64.cpp \
	$(UPSTREAM_DIR)/clipper-6.4.2/cpp/clipper.cpp \
	$(UPSTREAM_DIR)/pugixml/src/pugixml.cpp

PUGIXML_INCLUDES 	?= -I$(UPSTREAM_DIR)/pugixml/src
CLIPPER_INCLUDES 	?= -I$(UPSTREAM_DIR)/clipper-6.4.2/cpp
VORONOI_INCLUDES 	?= -I$(UPSTREAM_DIR)/voronoi/src
POISSON_INCLUDES 	?= -I$(UPSTREAM_DIR)/poisson-disk-sampling/thinks/poisson_disk_sampling/
BASE64_INCLUDES 	?= -I$(UPSTREAM_DIR)/cpp-base64
ARGAGG_INCLUDES 	?= -I$(UPSTREAM_DIR)/argagg/include/argagg
CAVC_INCLUDES 		?= -I$(UPSTREAM_DIR)/CavalierContours/include/cavc/
SUBPROCESS_INCLUDES	?= -I$(UPSTREAM_DIR)/subprocess.h
MINUNIT_INCLUDES	?= -I$(UPSTREAM_DIR)/minunit
STB_INCLUDES		?= -isystem$(UPSTREAM_DIR)/stb

INCLUDES := -Iinclude -Isrc $(CLIPPER_INCLUDES) $(VORONOI_INCLUDES) $(POISSON_INCLUDES) $(BASE64_INCLUDES) $(ARGAGG_INCLUDES) $(CAVC_INCLUDES) $(SUBPROCESS_INCLUDES) $(MINUNIT_INCLUDES) $(STB_INCLUDES)

CXXFLAGS := -std=c++2a -g -Wall -Wextra -O2
LDFLAGS := -lm

PKG_CONFIG_DEPS := 
ifdef USE_SYSTEM_PUGIXML
	CXXFLAGS += $(shell $(PKG_CONFIG) --cflags pugixml)
	LDFLAGS += $(shell $(PKG_CONFIG) --libs pugixml)
else
	SOURCES += $(PUGIXML_SOURCES)
	INCLUDES += $(PUGIXML_INCLUDES)
endif

TARGET := svg-flatten

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(dir $@) 
	$(CXX) -c $(CXXFLAGS) $(CXXFLAGS) $(INCLUDES) -o $@ $^

$(BUILDDIR)/$(TARGET): $(SOURCES:%.cpp=$(BUILDDIR)/%.o)
	@mkdir -p $(dir $@) 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BUILDDIR)/nopencv-test: src/test/nopencv_test.cpp src/nopencv.cpp
	@mkdir -p $(dir $@) 
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $^


.PHONY: tests
tests: $(BUILDDIR)/nopencv-test
	$(BUILDDIR)/nopencv-test

.PHONY: install
install:
	$(INSTALL) $(BUILDDIR)/$(TARGET) $(PREFIX)/bin
	
.PHONY: clean
clean:
	rm -rf $(BUILDDIR)
