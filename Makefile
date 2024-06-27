ifeq ($(OS),Windows_NT)
	EXE := Obsidian.exe
else
	EXE := Obsidian.elf
endif

M64     = -m64 -mpopcnt
MSSE2   = $(M64) -msse -msse2
MSSSE3  = $(MSSE2) -mssse3
MAVX2   = $(MSSSE3) -msse4.1 -mbmi -mfma -mavx2
MAVX512 = $(MAVX2) -mavx512f -mavx512bw

FILES = Obsidian/*.cpp Obsidian/fathom/src/tbprobe.c

OPTIMIZE = -O3 -fno-stack-protector -fno-math-errno -funroll-loops -fno-exceptions -flto -flto-partition=one

FLAGS = -s -pthread -std=c++17 -DNDEBUG

ifeq ($(build),)
	build = native
endif

ifeq ($(build), native)
    FLAGS += -march=native
else ifeq ($(findstring sse2, $(build)), sse2)
	FLAGS += $(MSSE2)
else ifeq ($(findstring ssse3, $(build)), ssse3)
	FLAGS += $(MSSSE3)
else ifeq ($(findstring avx2, $(build)), avx2)
	FLAGS += $(MAVX2)
else ifeq ($(findstring avx512, $(build)), avx512)
	FLAGS += $(MAVX512)
endif

COMMAND = g++ $(OPTIMIZE) $(FLAGS) $(FILES) -o $(EXE)

make: $(FILES)
	$(COMMAND)
