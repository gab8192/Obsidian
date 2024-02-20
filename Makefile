ifeq ($(OS),Windows_NT)
	EXE := Obsidian.exe
	PROF_GEN_EXE := Obsidian_prof_gen.exe
else
	EXE := Obsidian.elf
	PROF_GEN_EXE := Obsidian_prof_gen
endif

FILES = Obsidian/*.cpp Obsidian/fathom/tbprobe.c

OPTIMIZE = -O3 -fno-stack-protector -fno-math-errno -funroll-loops -fno-exceptions

FLAGS = -s -pthread -std=c++17 -flto -DNDEBUG

ifeq ($(build),)
	build = native
endif

ifeq ($(build), native)
    FLAGS += -march=native
else ifeq ($(findstring avx2, $(build)), avx2)
	FLAGS += -march=haswell
else ifeq ($(findstring avx512, $(build)), avx512)
	FLAGS += -march=skylake-avx512
endif

ifeq ($(build), native)
	PROPS = $(shell echo | $(CC) -march=native -E -dM -)
	ifneq ($(findstring __BMI2__, $(PROPS)),)
		ifeq ($(findstring __znver1, $(PROPS)),)
			ifeq ($(findstring __znver2, $(PROPS)),)
				FLAGS += -DUSE_PEXT
			else ifeq ($(shell uname), Linux)
				ifneq ($(findstring AMD EPYC 7B, $(shell lscpu)),)
					FLAGS += -DUSE_PEXT
				endif
			endif
		endif
	endif
else ifeq ($(findstring pext, $(build)), pext)
	FLAGS += -DUSE_PEXT -mbmi2
endif

make: $(FILES)
	g++ $(OPTIMIZE) $(FLAGS) $(FILES) -o $(PROF_GEN_EXE) -fprofile-generate="obs_pgo"
ifeq ($(OS),Windows_NT)
	$(PROF_GEN_EXE) bench
else
	./$(PROF_GEN_EXE) bench
endif
	rm -f $(PROF_GEN_EXE)
	g++ $(OPTIMIZE) $(FLAGS) $(FILES) -o $(EXE) -fprofile-use="obs_pgo"
	rm -rf obs_pgo

nopgo: $(FILES)
	g++ $(OPTIMIZE) $(FLAGS) $(FILES) -o $(EXE)
