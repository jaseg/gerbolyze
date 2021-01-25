
CXX := clang++
LD := ld
PKG_CONFIG ?= pkg-config

BUILDDIR ?= build

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
	src/lambda_sink.cpp \

SOURCES += upstream/clipper-6.4.2/cpp/clipper.cpp upstream/clipper-6.4.2/cpp/cpp_cairo/cairo_clipper.cpp
CLIPPER_INCLUDES := -Iupstream/clipper-6.4.2/cpp -Iupstream/clipper-6.4.2/cpp/cpp_cairo/
VORONOI_INCLUDES := -Iupstream/voronoi/src
POISSON_INCLUDES := -Iupstream/poisson-disk-sampling/thinks/poisson_disk_sampling/
BASE64_INCLUDES := -Iupstream/cpp-base64
ARGAGG_INCLUDES := -Iupstream/argagg/include/argagg
INCLUDES := -Iinclude -Isrc $(CLIPPER_INCLUDES) $(VORONOI_INCLUDES) $(POISSON_INCLUDES) $(BASE64_INCLUDES) $(ARGAGG_INCLUDES)

CXXFLAGS := -std=c++2a -g -Wall -Wextra
CXXFLAGS += $(shell $(PKG_CONFIG) --cflags pangocairo pugixml opencv4) 

LDFLAGS := -lm -lc -lstdc++
LDFLAGS += $(shell $(PKG_CONFIG) --libs pangocairo pugixml opencv4)

all: $(BUILDDIR)/svg-render

test.gbr test.svg &: render
	./render test.svg > test.gbr

$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(dir $@) 
	$(CXX) -c $(CXXFLAGS) $(CXXFLAGS) $(INCLUDES) -o $@ $^

$(BUILDDIR)/svg-render: $(SOURCES:%.cpp=$(BUILDDIR)/%.o) $(BUILDDIR)/upstream/cpp-base64/base64.o $(CLIPPER_SOURCES:.cpp=.o)
	@mkdir -p $(dir $@) 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^
	
.PHONY: clean
clean:
	rm -rf $(BUILDDIR)
