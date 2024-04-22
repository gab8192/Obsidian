#include "debug.h"

#include <cstdint>
#include <iostream>

int initDebug();

int* values;
int valueCount = initDebug();

int initDebug() {
    values = new int[10000000];
    return 0;
}

void debug_add(int value) {
    values[valueCount++] = value;
}

void debug_print() {
    int64_t sum = 0;
    for (int i = 0; i < valueCount; i++)
        sum += values[i];

    std::cout << "mean: " << (sum / double(valueCount)) << std::endl;
}