ifeq ($(OS),Windows_NT)
	EXE := Obsidian.exe
else
	EXE := Obsidian.elf
endif

FILES = Obsidian/*.cpp Obsidian/fathom/tbprobe.c

OPTIMIZE = -O3 -fno-stack-protector -fno-math-errno -funroll-loops -fno-exceptions

FLAGS = -s -pthread -std=c++17 -flto

DEFINITIONS = -DNDEBUG -D_CONSOLE

NATIVE = false

ifeq ($(build),)
	build = avx2
	NATIVE = true
endif

ifeq ($(build), avx512)
	DEFINITIONS += -DUSE_AVX512
    ARCH = -march=skylake-avx512
endif

ifeq ($(build), avx2)
	DEFINITIONS += -DUSE_AVX2
	ARCH = -march=haswell
endif

ifeq ($(NATIVE), true)
	ARCH = -march=native
endif

COMMAND = g++ $(ARCH) $(OPTIMIZE) $(FLAGS) $(DEFINITIONS) $(FILES) -o $(EXE)



make: $(FILES)
	$(COMMAND) -fprofile-generate="obs_pgo"
ifeq ($(OS),Windows_NT)
	$(EXE) bench
else
	./$(EXE) bench
endif
	$(COMMAND) -fprofile-use="obs_pgo"
	rm -rf obs_pgo

nopgo: $(FILES)
	$(COMMAND)
