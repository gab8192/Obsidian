ifeq ($(OS),Windows_NT)
	EXE := Obsidian.exe
else
	EXE := Obsidian
endif

FILES = Obsidian/*.cpp

OPTIMIZE = -O2 -fno-stack-protector -fno-math-errno -funroll-loops -fno-exceptions

FLAGS = -s -std=c++17 -flto -DNDEBUG -DUSE_AVX2 -march=native

make: $(FILES)
	g++ $(OPTIMIZE) $(FLAGS) $(FILES) -o $(EXE)