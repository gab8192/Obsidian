#include "uci.h"
#include "fathom/src/tbprobe.h"
#include "nnue.h"
#include "threads.h"
#include "tt.h"

#include <cassert>
#include <ostream>
#include <sstream>

using std::string;

namespace UCI {

OptionsMap Options;

int contemptValue = 0;

void clearHashClicked(const Option&)   {
   TT::clear();
}

void hashChanged(const Option& o) {
   TT::resize(size_t(o));
}

void threadsChanged(const Option& o) {
  int count = int(o);
  Threads::setThreadCount(count);
  //NNUE::loadWeights(count > 32); // CCC and TCEC
}

void syzygyPathChanged(const Option& o) {
  std::string str = o;
  tb_init(str.c_str());
  if (TB_LARGEST)
    std::cout << "info string Syzygy tablebases loaded. Pieces: " << TB_LARGEST << std::endl;
  else
    std::cout << "info string Syzygy tablebases failed to load" << std::endl;
}

void refreshContemptImpl() {
  contemptValue = Options["Contempt"];

  std::string uciOppString = Options["UCI_Opponent"];
  std::string overrides = Options["ContemptOverrides"];

  if (uciOppString.empty() || overrides.empty())
    return;

  std::string opponent = "";
  {
    std::string token;
    std::istringstream iss(uciOppString);
    iss >> token; // title|none
    iss >> token; // elo|none
    iss >> token; // computer|human
    while (iss >> token)
      opponent += (opponent.empty() ? "" : " ") + token;
  }

  std::stringstream ss(overrides);
  std::string pair; // engine=value

  while (std::getline(ss, pair, ',')) {
    std::string key = pair.substr(0, pair.find('='));
    std::string value = pair.substr(pair.find('=') + 1);
    if (key == opponent) {
      contemptValue = std::stoi(value);
      break;
    }
  }
}

void refreshContempt(const Option&) {
  int oldContempt = contemptValue;
  refreshContemptImpl();
  if (contemptValue != oldContempt)
    std::cout << "Contempt updated to " << contemptValue << std::endl;
}

bool CaseInsensitiveLess::operator() (const string& s1, const string& s2) const {

  return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
         [](char c1, char c2) { return tolower(c1) < tolower(c2); });
}


void init() {

  constexpr int MaxHashMB = 33554432;

  Options["Contempt"]          = Option(0, 0, 512, refreshContempt);
  Options["ContemptOverrides"] = Option("", refreshContempt);
  Options["Hash"]              = Option(64, 1, MaxHashMB, hashChanged);
  Options["Clear Hash"]        = Option(clearHashClicked);
  Options["Threads"]           = Option(1, 1, 1024, threadsChanged);
  Options["Move Overhead"]     = Option(10, 0, 1000);
  Options["SyzygyPath"]        = Option("", syzygyPathChanged);
  Options["Minimal"]           = Option("false");
  Options["MultiPV"]           = Option(1, 1, MAX_MOVES);
  Options["UCI_Opponent"]      = Option("", refreshContempt);
}


std::ostream& operator<<(std::ostream& os, const OptionsMap& om) {

  for (const auto& [name, opt] : om) {
    os << "\noption name " << name << " type " << opt.type;

    if (opt.type == "string" || opt.type == "check" || opt.type == "combo")
      os << " default " << opt.defaultValue;

    if (opt.type == "spin")
      os << " default " << int(stof(opt.defaultValue))
         << " min "     << opt.min
         << " max "     << opt.max;
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

void Option::set(const string& v) {

  assert(!type.empty());

  if (   (type != "button" && type != "string" && v.empty())
      || (type == "check" && v != "true" && v != "false")
      || (type == "spin" && (stof(v) < min || stof(v) > max)))
      return;

  if (type == "combo")
  {
      OptionsMap comboMap; // To have case insensitive compare
      string token;
      std::istringstream ss(defaultValue);
      while (ss >> token)
          comboMap[token] = Option();
      if (!comboMap.count(v) || v == "var")
          return;
  }

  if (type != "button")
      currentValue = v;

  if (on_change)
      on_change(*this);
}

}
