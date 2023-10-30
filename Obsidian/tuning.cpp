#include "tuning.h"

#include <sstream>
#include <vector>

std::vector<EngineParam*> tuningParams;

void registerParam(EngineParam* param) {
  tuningParams.push_back(param);
}

EngineParam* findParam(std::string name) {
  for (int i = 0; i < tuningParams.size(); i++) {
    if (tuningParams.at(i)->name == name)
      return tuningParams.at(i);
  }
  return nullptr;
}

std::string paramsToUci() {
  std::ostringstream ss;

  for (int i = 0; i < tuningParams.size(); i++) {
    EngineParam* p = tuningParams.at(i);

    ss << "option name " << p->name << " type spin default " << p->value << " min -999999999 max 999999999\n";
  }

  return ss.str();
}

std::string paramsToSpsaInput() {
  std::ostringstream ss;

  for (int i = 0; i < tuningParams.size(); i++) {
    EngineParam* p = tuningParams.at(i);

    ss << p->name
      << ", " << "int"
      << ", " << double(p->value)
      << ", " << double(p->min)
      << ", " << double(p->max)
      << ", " << std::max(0.5, double(p->max-p->min)/20.0)
      << ", " << 0.002
      << "\n";
  }

  return ss.str();
}