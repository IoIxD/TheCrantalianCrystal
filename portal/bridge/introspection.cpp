#include "introspection.hpp"

#include <cstdio>
#include <cstring>
#include <map>

const TCCDBusMember *TCCDBusInterface::method(const char *name) const {
  for (const TCCDBusMember &m : methods)
    if (m.name == name)
      return &m;
  return nullptr;
}

const TCCDBusProperty *TCCDBusInterface::property(const char *name) const {
  for (const TCCDBusProperty &p : properties)
    if (p.name == name)
      return &p;
  return nullptr;
}

static void args_xml(std::string &xml, const std::vector<TCCDBusArg> &args,
                     const char *direction) {
  for (const TCCDBusArg &a : args) {
    xml += "      <arg name=\"" + a.name + "\" type=\"" + a.type + "\"";
    if (direction)
      xml += std::string(" direction=\"") + direction + "\"";
    xml += "/>\n";
  }
}

std::string TCCDBusInterface::to_xml() const {
  std::string xml = "  <interface name=\"" + name + "\">\n";
  for (const TCCDBusMember &m : methods) {
    xml += "    <method name=\"" + m.name + "\">\n";
    args_xml(xml, m.in, "in");
    args_xml(xml, m.out, "out");
    xml += "    </method>\n";
  }
  for (const TCCDBusMember &s : signals) {
    xml += "    <signal name=\"" + s.name + "\">\n";
    args_xml(xml, s.out, nullptr);
    xml += "    </signal>\n";
  }
  for (const TCCDBusProperty &p : properties) {
    xml += "    <property name=\"" + p.name + "\" type=\"" + p.type +
           "\" access=\"" + (p.writable ? "readwrite" : "read") + "\"/>\n";
  }
  xml += "  </interface>\n";
  return xml;
}

namespace {
struct Parser {
  const char *p;

  bool skip_past(const char *end) {
    const char *found = strstr(p, end);
    if (!found)
      return false;
    p = found + strlen(end);
    return true;
  }

  void skip_space() {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
      p++;
  }

  std::string name() {
    const char *start = p;
    while (*p && !strchr(" \t\r\n/>=", *p))
      p++;
    return std::string(start, p);
  }

  // Reads the rest of a start tag.
  bool attributes(std::map<std::string, std::string> &attrs,
                  bool &self_closing) {
    for (;;) {
      skip_space();
      if (*p == '>') {
        p++;
        self_closing = false;
        return true;
      }
      if (p[0] == '/' && p[1] == '>') {
        p += 2;
        self_closing = true;
        return true;
      }
      std::string key = name();
      skip_space();
      if (key.empty() || *p++ != '=')
        return false;
      skip_space();
      char quote = *p++;
      if (quote != '"' && quote != '\'')
        return false;
      const char *end = strchr(p, quote);
      if (!end)
        return false;
      // None of the attributes we read have entities in them.
      attrs[key] = std::string(p, end);
      p = end + 1;
    }
  }
};
} // namespace

bool tcc_introspection::parse(const char *xml,
                              std::vector<TCCDBusInterface> &out) {
  Parser parser{xml};
  TCCDBusInterface *iface = nullptr;
  TCCDBusMember *member = nullptr;
  bool in_signal = false;

  while (parser.skip_past("<")) {
    const char *&p = parser.p;
    if (strncmp(p, "!--", 3) == 0) {
      if (!parser.skip_past("-->"))
        return false;
      continue;
    }
    if (*p == '?' || *p == '!') {
      if (!parser.skip_past(">"))
        return false;
      continue;
    }

    if (*p == '/') {
      p++;
      std::string tag = parser.name();
      if (tag == "interface")
        iface = nullptr;
      else if (tag == "method" || tag == "signal")
        member = nullptr;
      if (!parser.skip_past(">"))
        return false;
      continue;
    }

    std::string tag = parser.name();
    std::map<std::string, std::string> attrs;
    bool self_closing;
    if (!parser.attributes(attrs, self_closing)) {
      fprintf(stderr, "tcc_portal: bad introspection XML near <%s>\n",
              tag.c_str());
      return false;
    }

    if (tag == "interface") {
      out.push_back({attrs["name"]});
      iface = self_closing ? nullptr : &out.back();
    } else if (iface && (tag == "method" || tag == "signal")) {
      in_signal = tag == "signal";
      auto &list = in_signal ? iface->signals : iface->methods;
      list.push_back({attrs["name"]});
      member = self_closing ? nullptr : &list.back();
    } else if (iface && tag == "property") {
      iface->properties.push_back({attrs["name"], attrs["type"],
                                   attrs["access"] == "readwrite"});
    } else if (member && tag == "arg") {
      bool in = !in_signal && attrs["direction"] != "out";
      (in ? member->in : member->out).push_back({attrs["name"], attrs["type"]});
    }
  }
  return true;
}
