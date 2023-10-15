ifeq ($(OS),Windows_NT)
	EXE := Obsidian.exe
else
	EXE := Obsidian.elf
endif

FILES = Obsidian/*.cpp

OPTIMIZE = -O2 -fno-stack-protector -fno-math-errno -funroll-loops -fno-exceptions

FLAGS = -s -std=c++17 -flto

DEFINITIONS = -DNDEBUG -D_CONSOLE

ifeq ($(build),)
	build = avx2
endif

ifeq ($(build), avx512)
    ARCH = -DUSE_AVX512 -march=skylake-avx512
endif

ifeq ($(build), avx2)
	ARCH = -DUSE_AVX2 -march=haswell
endif

make: $(FILES)
	g++ $(ARCH) $(OPTIMIZE) $(FLAGS) $(DEFINITIONS) $(FILES) -o $(EXE)