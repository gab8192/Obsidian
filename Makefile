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

COMMAND = g++ $(OPTIMIZE) $(FLAGS) $(FILES) -o $(EXE) -lnuma

make: $(FILES)
	$(COMMAND) -fprofile-generate="obs_pgo"
ifeq ($(OS),Windows_NT)
	$(EXE) bench
else
	./$(EXE) bench
endif
	$(COMMAND) -fprofile-use="obs_pgo"
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "Remove-Item -Recurse -Force obs_pgo"
else
	rm -rf obs_pgo
endif

nopgo: $(FILES)
	$(COMMAND)
