#pragma once

#include "position.h"
#include "types.h"

#include <map>
#include <string>
#include <vector>

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

    void set(const std::string&);

    operator int() const;

    operator std::string() const;

    bool operator==(const char*) const;

  private:
    friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

    std::string defaultValue, currentValue, type;
    int min, max;
    OnChange on_change;
  };

  void init();

  void loop(int argc, char* argv[]);

  int normalizeToCp(Score v);

  std::string scoreToString(Score v);

  std::string squareToString(Square s);

  std::string moveToString(Move m);

  Move stringToMove(const Position& pos, std::string& str);

  extern OptionsMap Options;

  extern int contemptValue;

} // namespace UCI