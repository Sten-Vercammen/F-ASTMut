LLVM_BUILD_PATH = /usr/lib/llvm-4.0/build
LLVM_BIN_PATH = $(LLVM_BUILD_PATH)/Debug+Asserts/bin

CXX := clang++-4.0
CXXFLAGS := -std=c++11 -DNDEBUG -finline-functions -fno-strict-aliasing -g -v

LLVM_CXXFLAGS := `$(LLVM_BIN_PATH)/llvm-config --cxxflags`
LLVM_LDFLAGS := `$(LLVM_BIN_PATH)/llvm-config --ldflags --libs --system-libs `

CLANG_INCLUDES := \
        -I/usr/include/clang/4.0/include \
        -Iinclude

LLVM_INCLUDES := \
        -I/usr/include/llvm-4.0/

CLANG_LIBS := \
        -Wl,--start-group \
        -lclangAST \
        -lclangASTMatchers \
        -lclangAnalysis \
        -lclangBasic \
        -lclangDriver \
        -lclangEdit \
        -lclangFrontend \
        -lclangFrontendTool \
        -lclangLex \
        -lclangParse \
        -lclangSema \
        -lclangEdit \
        -lclangRewrite \
        -lclangRewriteFrontend \
        -lclangStaticAnalyzerFrontend \
        -lclangStaticAnalyzerCheckers \
        -lclangStaticAnalyzerCore \
        -lclangIndex \
        -lclangSerialization \
        -lclangToolingCore \
        -lclangTooling \
        -lclangFormat \
        -Wl,--end-group

LIB_INCLUDES := \
        -Isrc_incl

all: main.cpp
	 $(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) $(CLANG_INCLUDES) $(LLVM_INCLUDES) $^ \
                $(CLANG_LIBS) $(LIB_INCLUDES) $(LLVM_LDFLAGS) -lclangAST -o rewritersample.o

clean:
	rm -rf *.o *.ll rewritersample
