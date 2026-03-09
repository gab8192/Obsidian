CXX = g++
CC = gcc

ifeq ($(OS),Windows_NT)
	EXE := Obsidian.exe
else
	EXE := Obsidian
endif

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

DEFAULT_NET = net89perm.bin

ifndef EVALFILE
	EVALFILE = $(DEFAULT_NET)
	DOWNLOAD_NET = true
endif

M64     = -m64 -mpopcnt
MSSE2   = $(M64) -msse -msse2
MSSSE3  = $(MSSE2) -mssse3
MAVX2   = $(MSSSE3) -msse4.1 -mbmi -mfma -mavx2
MAVX512 = $(MAVX2) -mavx512f -mavx512bw

FILES = $(wildcard src/*.cpp) src/fathom/src/tbprobe.c

OBJS = $(FILES:.cpp=.o)
OBJS := $(OBJS:.c=.o)

OPTIMIZE = -O3 -fno-stack-protector -fno-math-errno -funroll-loops -fno-exceptions -flto

ifneq ($(UNAME_S), Darwin)
	OPTIMIZE += -flto-partition=one
endif

COMMON_FLAGS = -pthread -DNDEBUG -DEvalFile=\"$(EVALFILE)\" $(OPTIMIZE)
CXXFLAGS = $(COMMON_FLAGS) -std=c++17
CFLAGS = $(COMMON_FLAGS)

ifneq ($(UNAME_S), Darwin)
	CXXFLAGS += -s
	CFLAGS += -s
endif

ifeq ($(OS),Windows_NT)
	CXXFLAGS += -static
	CFLAGS += -static
endif

ifeq ($(build),)
	build = native
endif

ifeq ($(build), native)
    CXXFLAGS += -march=native
    CFLAGS += -march=native
else ifeq ($(findstring sse2, $(build)), sse2)
	CXXFLAGS += $(MSSE2)
	CFLAGS += $(MSSE2)
else ifeq ($(findstring ssse3, $(build)), ssse3)
	CXXFLAGS += $(MSSSE3)
	CFLAGS += $(MSSSE3)
else ifeq ($(findstring avx2, $(build)), avx2)
	CXXFLAGS += $(MAVX2)
	CFLAGS += $(MAVX2)
else ifeq ($(findstring avx512, $(build)), avx512)
	CXXFLAGS += $(MAVX512)
	CFLAGS += $(MAVX512)
endif

ifeq ($(build), native)
	ifeq ($(UNAME_M), x86_64)
		PROPS = $(shell echo | $(CC) -march=native -E -dM -)
		ifneq ($(findstring __BMI2__, $(PROPS)),)
			ifeq ($(findstring __znver1, $(PROPS)),)
				ifeq ($(findstring __znver2, $(PROPS)),)
					CXXFLAGS += -DUSE_PEXT
					CFLAGS += -DUSE_PEXT
				else ifeq ($(shell uname), Linux)
					ifneq ($(findstring AMD EPYC 7B, $(shell lscpu)),)
						CXXFLAGS += -DUSE_PEXT
						CFLAGS += -DUSE_PEXT
					endif
				endif
			endif
		endif
	endif
else ifeq ($(findstring avx512, $(build)), avx512)
	CXXFLAGS += -DUSE_PEXT -mbmi2
	CFLAGS += -DUSE_PEXT -mbmi2
else ifeq ($(findstring pext, $(build)), pext)
	CXXFLAGS += -DUSE_PEXT -mbmi2
	CFLAGS += -DUSE_PEXT -mbmi2
endif

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

make: download-net $(FILES)
ifeq ($(UNAME_S), Darwin)
	$(CXX) $(CXXFLAGS) -fprofile-instr-generate -c src/*.cpp
	$(CC) $(CFLAGS) -fprofile-instr-generate -c src/fathom/src/tbprobe.c
	$(CXX) $(CXXFLAGS) -fprofile-instr-generate *.o -o $(EXE)
	LLVM_PROFILE_FILE="obs_pgo.profraw" ./$(EXE) bench
	xcrun llvm-profdata merge -output=obs_pgo.profdata obs_pgo.profraw
	$(CXX) $(CXXFLAGS) -fprofile-instr-use=obs_pgo.profdata -c src/*.cpp
	$(CC) $(CFLAGS) -fprofile-instr-use=obs_pgo.profdata -c src/fathom/src/tbprobe.c
	$(CXX) $(CXXFLAGS) -fprofile-instr-use=obs_pgo.profdata *.o -o $(EXE)
	rm -f *.o obs_pgo.profraw obs_pgo.profdata
else
	$(CXX) $(CXXFLAGS) $(FILES) -o $(EXE) -fprofile-generate="obs_pgo"
ifeq ($(OS),Windows_NT)
	$(EXE) bench
else
	./$(EXE) bench
endif
	$(CXX) $(CXXFLAGS) $(FILES) -o $(EXE) -fprofile-use="obs_pgo"
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "Remove-Item -Recurse -Force obs_pgo"
else
	rm -rf obs_pgo
endif
endif

nopgo: download-net $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(EXE)

clean:
	rm -f $(OBJS)
	
download-net:
ifdef DOWNLOAD_NET
	@if test -f "$(EVALFILE)"; then \
		echo "File $(EVALFILE) already exists, skipping download."; \
	else \
		echo Downloading net; \
		curl -sOL https://github.com/gab8192/Obsidian-nets/releases/download/nets/$(EVALFILE); \
	fi
endif
