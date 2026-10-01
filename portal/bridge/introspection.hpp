#pragma once

#include <string>
#include <vector>

/*
 * The parts of a D-Bus introspection XML file the bridge cares about. The
 * bridge gets everything it knows about an interface from these, the varlink
 * side just has to use the same names.
 */
struct TCCDBusArg {
  std::string name;
  std::string type;
};

// A method or a signal. Signals only have `out` args.
struct TCCDBusMember {
  std::string name;
  std::vector<TCCDBusArg> in;
  std::vector<TCCDBusArg> out;
};

struct TCCDBusProperty {
  std::string name;
  std::string type;
  bool writable = false;
};

struct TCCDBusInterface {
  std::string name;
  std::vector<TCCDBusMember> methods;
  std::vector<TCCDBusMember> signals;
  std::vector<TCCDBusProperty> properties;

  const TCCDBusMember *method(const char *name) const;
  const TCCDBusProperty *property(const char *name) const;
  // Back to introspection XML, without the documentation.
  std::string to_xml() const;
};

namespace tcc_introspection {
// Only handles what introspection files use: elements and attributes, with
// comments, processing instructions and text skipped.
bool parse(const char *xml, std::vector<TCCDBusInterface> &out);
} // namespace tcc_introspection
