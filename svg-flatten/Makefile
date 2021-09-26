
CXX ?= clang++
LD ?= ld
INSTALL := install
PKG_CONFIG ?= pkg-config
PYTHON3 ?= python3

BUILDDIR ?= build
CACHEDIR ?= cache
PREFIX ?= /usr/local
UPSTREAM_DIR ?= upstream

WASI_SDK ?= wasi-sdk-11.0
WASI_SDK_URL ?= https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-11/wasi-sdk-11.0-linux.tar.gz
WASI_SDK_PATH ?= $(CACHEDIR)/$(WASI_SDK)
WASI_CXX ?= $(WASI_SDK_PATH)/bin/clang++ --sysroot=$(WASI_SDK_PATH)/share/wasi-sysroot

SOURCES := \
	src/svg_color.cpp \
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
	src/out_scaler.cpp \
	src/lambda_sink.cpp \
	src/flatten.cpp \
	src/util.cpp \
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
LDFLAGS := -lm -lstdc++

ifdef USE_SYSTEM_PUGIXML
	HOST_CXXFLAGS := $(CXXFLAGS) $(shell $(PKG_CONFIG) --cflags pugixml)
	HOST_LDFLAGS := $(LDFLAGS) $(shell $(PKG_CONFIG) --libs pugixml)
	HOST_SOURCES := $(SOURCES)
	HOST_INCLUDES := $(INCLUDES)
else
	HOST_CXXFLAGS := $(CXXFLAGS)
	HOST_LDFLAGS := $(LDFLAGS)
	HOST_SOURCES := $(SOURCES) $(PUGIXML_SOURCES)
	HOST_INCLUDES := $(INCLUDES) $(PUGIXML_INCLUDES)
endif

HOST_LDFLAGS += -lstdc++fs # for debian's ancient compilers

WASI_CXXFLAGS ?= -DNOFORK -DNOTHROW -DWASI -DPUGIXML_NO_EXCEPTIONS -fno-exceptions $(CXXFLAGS)

BINARY := svg-flatten

all: $(BUILDDIR)/$(BINARY) $(BUILDDIR)/nopencv-test

$(CACHEDIR)/$(WASI_SDK):
	mkdir -p $(dir $@)
	cd $(dir $@); curl -L ${WASI_SDK_URL} | tar xzf -

$(BUILDDIR)/wasi/%.o: %.cpp $(CACHEDIR)/$(WASI_SDK) 
	@mkdir -p $(dir $@) 
	$(WASI_CXX) -c $(WASI_CXXFLAGS) $(INCLUDES) $(PUGIXML_INCLUDES) -o $@ $<

$(BUILDDIR)/$(BINARY).wasm: $(patsubst %.cpp,$(BUILDDIR)/wasi/%.o,$(SOURCES) $(PUGIXML_SOURCES))
	@mkdir -p $(dir $@)
	$(WASI_CXX) $(WASI_CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/host/%.o: %.cpp
	@mkdir -p $(dir $@) 
	$(CXX) -c $(HOST_CXXFLAGS) $(HOST_CXXFLAGS) $(HOST_INCLUDES) -o $@ $<

.INTERMEDIATE: $(HOST_SOURCES:%.cpp=$(BUILDDIR)/host/%.o)
$(BUILDDIR)/$(BINARY): $(HOST_SOURCES:%.cpp=$(BUILDDIR)/host/%.o)
	@mkdir -p $(dir $@) 
	$(CXX) $(HOST_CXXFLAGS) -o $@ $^ $(HOST_LDFLAGS)

$(BUILDDIR)/nopencv-test: src/test/nopencv_test.cpp src/nopencv.cpp src/util.cpp
	@mkdir -p $(dir $@) 
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(HOST_LDFLAGS)


.PHONY: tests
tests: $(BUILDDIR)/nopencv-test
	$(BUILDDIR)/nopencv-test
	$(PYTHON3) src/test/svg_tests.py || ( mkdir testcase-fails && cp /tmp/gerbolyze-*.{svg,png} testcase-fails/ && false )

.PHONY: install
install:
	$(INSTALL) $(BUILDDIR)/$(BINARY) $(PREFIX)/bin
	
.PHONY: clean
clean:
	rm -rf $(BUILDDIR)

.PHONY: mrproper
mrproper: clean
	rm -rf $(CACHEDIR)
