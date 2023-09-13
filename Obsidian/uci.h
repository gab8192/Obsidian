#pragma once

#include "position.h"
#include "types.h"

#include <map>
#include <string>
#include <vector>

extern std::vector<uint64_t> seenPositions;

namespace UCI {

class Option;

/// Define a custom comparator, because the UCI options should be case-insensitive
struct CaseInsensitiveLess {
  bool operator() (const std::string&, const std::string&) const;
};

/// The options container is defined as a std::map
using OptionsMap = std::map<std::string, Option, CaseInsensitiveLess>;

/// The Option class implements each option as specified by the UCI protocol
class Option {

  using OnChange = void (*)(const Option&);

public:
  Option(OnChange = nullptr);
  Option(bool v, OnChange = nullptr);
  Option(const char* v, OnChange = nullptr);
  Option(double v, int minv, int maxv, OnChange = nullptr);
  Option(const char* v, const char* cur, OnChange = nullptr);

  Option& operator=(const std::string&);
  void operator<<(const Option&);
  operator int() const;
  operator std::string() const;
  bool operator==(const char*) const;

private:
  friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

  std::string defaultValue, currentValue, type;
  int min, max;
  size_t idx;
  OnChange on_change;
};

void init(OptionsMap&);
void loop(int argc, char* argv[]);
int to_cp(Value v);
std::string value(Value v);
std::string square(Square s);
std::string move(Move m);
Move to_move(const Position& pos, std::string& str);

} // namespace UCI

extern UCI::OptionsMap Options;