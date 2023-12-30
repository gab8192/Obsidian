#include "uci.h"
#include "fathom/tbprobe.h"
#include "threads.h"
#include "tt.h"

#include <cassert>
#include <ostream>
#include <sstream>

using std::string;

UCI::OptionsMap Options;

namespace UCI {

void clearHashClicked(const Option&)   {
   TT::clear(); 
}

void hashChanged(const Option& o) {
   TT::resize(size_t(o)); 
}

void threadsChanged(const Option& o) { 
  Threads::setThreadCount(int(o)); 
}

void syzygyPathChanged(const Option& o) {
  std::string str = o;
  tb_init(str.c_str());
  if (TB_LARGEST)
    std::cout << "info string Syzygy tablebases loaded. Pieces: " << TB_LARGEST << std::endl;
  else
    std::cout << "info string Syzygy tablebases failed to load" << std::endl;
}


bool CaseInsensitiveLess::operator() (const string& s1, const string& s2) const {

  return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
         [](char c1, char c2) { return tolower(c1) < tolower(c2); });
}


void init(OptionsMap& o) {

  constexpr int MaxHashMB = 33554432;

  o["Hash"]                  << Option(64, 1, MaxHashMB, hashChanged);
  o["Clear Hash"]            << Option(clearHashClicked);
  o["Threads"]               << Option(1, 1, 1024, threadsChanged);
  o["Move Overhead"]         << Option(20, 0, 1000);
  o["SyzygyPath"]            << Option("", syzygyPathChanged);
}


std::ostream& operator<<(std::ostream& os, const OptionsMap& om) {

  for (size_t idx = 0; idx < om.size(); ++idx)
      for (const auto& it : om)
          if (it.second.idx == idx)
          {
              const Option& o = it.second;
              os << "\noption name " << it.first << " type " << o.type;

              if (o.type == "string" || o.type == "check" || o.type == "combo")
                  os << " default " << o.defaultValue;

              if (o.type == "spin")
                  os << " default " << int(stof(o.defaultValue))
                     << " min "     << o.min
                     << " max "     << o.max;

              break;
          }

  return os;
}


Option::Option(const char* v, OnChange f) : type("string"), min(0), max(0), on_change(f)
{ defaultValue = currentValue = v; }

Option::Option(bool v, OnChange f) : type("check"), min(0), max(0), on_change(f)
{ defaultValue = currentValue = (v ? "true" : "false"); }

Option::Option(OnChange f) : type("button"), min(0), max(0), on_change(f)
{}

Option::Option(double v, int minv, int maxv, OnChange f) : type("spin"), min(minv), max(maxv), on_change(f)
{ defaultValue = currentValue = std::to_string(v); }

Option::Option(const char* v, const char* cur, OnChange f) : type("combo"), min(0), max(0), on_change(f)
{ defaultValue = v; currentValue = cur; }

Option::operator int() const {
  assert(type == "check" || type == "spin");
  return (type == "spin" ? std::stoi(currentValue) : currentValue == "true");
}

Option::operator std::string() const {
  assert(type == "string");
  return currentValue;
}

bool Option::operator==(const char* s) const {
  assert(type == "combo");
  return   !CaseInsensitiveLess()(currentValue, s)
        && !CaseInsensitiveLess()(s, currentValue);
}

void Option::operator<<(const Option& o) {

  static size_t insert_order = 0;

  *this = o;
  idx = insert_order++;
}

Option& Option::operator=(const string& v) {

  assert(!type.empty());

  if (   (type != "button" && type != "string" && v.empty())
      || (type == "check" && v != "true" && v != "false")
      || (type == "spin" && (stof(v) < min || stof(v) > max)))
      return *this;

  if (type == "combo")
  {
      OptionsMap comboMap; // To have case insensitive compare
      string token;
      std::istringstream ss(defaultValue);
      while (ss >> token)
          comboMap[token] << Option();
      if (!comboMap.count(v) || v == "var")
          return *this;
  }

  if (type != "button")
      currentValue = v;

  if (on_change)
      on_change(*this);

  return *this;
}

}
