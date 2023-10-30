#pragma once

#include <iostream>

struct EngineParam;

void registerParam(EngineParam* param);

EngineParam* findParam(std::string name);

std::string paramsToUci();

std::string paramsToSpsaInput();

struct EngineParam {
  std::string name;
  int value;
  int min, max;

  EngineParam(std::string _name, int _value, int _min, int _max) :
    name(_name), value(_value), min(_min), max(_max)
  {
    if (_max < _min) {
      std::cout << "[Warning] Parameter " << _name << " has invalid bounds" << std::endl;
    }

    registerParam(this);
  }

  inline operator int() const {
    return value;
  }
};

// #define DO_TUNING

#ifdef DO_TUNING

constexpr bool doTuning = true;

#define DEFINE_PARAM(_name, _value, _min, _max) EngineParam _name(#_name, _value, _min, _max)

#else

constexpr bool doTuning = false;

#define DEFINE_PARAM(_name, _value, _min, _max) constexpr int _name = _value

#endif // DO_TUNING
