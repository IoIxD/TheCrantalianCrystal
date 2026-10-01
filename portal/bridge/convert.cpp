#include "convert.hpp"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

TCCFds::~TCCFds() {
  for (int fd : fds)
    close(fd);
}

static long put_bool(TCCVarlinkOut out, bool v) {
  return out.array ? varlink_array_append_bool(out.array, v)
                   : varlink_object_set_bool(out.object, out.field, v);
}
static long put_int(TCCVarlinkOut out, int64_t v) {
  return out.array ? varlink_array_append_int(out.array, v)
                   : varlink_object_set_int(out.object, out.field, v);
}
static long put_float(TCCVarlinkOut out, double v) {
  return out.array ? varlink_array_append_float(out.array, v)
                   : varlink_object_set_float(out.object, out.field, v);
}
static long put_string(TCCVarlinkOut out, const char *v) {
  return out.array ? varlink_array_append_string(out.array, v)
                   : varlink_object_set_string(out.object, out.field, v);
}
static long put_array(TCCVarlinkOut out, VarlinkArray *v) {
  return out.array ? varlink_array_append_array(out.array, v)
                   : varlink_object_set_array(out.object, out.field, v);
}
static long put_object(TCCVarlinkOut out, VarlinkObject *v) {
  return out.array ? varlink_array_append_object(out.array, v)
                   : varlink_object_set_object(out.object, out.field, v);
}

static long get_bool(TCCVarlinkIn in, bool *v) {
  return in.array ? varlink_array_get_bool(in.array, in.index, v)
                  : varlink_object_get_bool(in.object, in.field, v);
}
static long get_int(TCCVarlinkIn in, int64_t *v) {
  return in.array ? varlink_array_get_int(in.array, in.index, v)
                  : varlink_object_get_int(in.object, in.field, v);
}
static long get_float(TCCVarlinkIn in, double *v) {
  return in.array ? varlink_array_get_float(in.array, in.index, v)
                  : varlink_object_get_float(in.object, in.field, v);
}
static long get_string(TCCVarlinkIn in, const char **v) {
  return in.array ? varlink_array_get_string(in.array, in.index, v)
                  : varlink_object_get_string(in.object, in.field, v);
}
static long get_array(TCCVarlinkIn in, VarlinkArray **v) {
  return in.array ? varlink_array_get_array(in.array, in.index, v)
                  : varlink_object_get_array(in.object, in.field, v);
}
static long get_object(TCCVarlinkIn in, VarlinkObject **v) {
  return in.array ? varlink_array_get_object(in.array, in.index, v)
                  : varlink_object_get_object(in.object, in.field, v);
}

size_t tcc_convert::complete_type_length(const char *signature) {
  const char *p = signature;
  while (*p == 'a')
    p++;
  if (*p != '(' && *p != '{')
    return *p ? p - signature + 1 : 0;
  char close = *p == '(' ? ')' : '}';
  p++;
  while (*p && *p != close) {
    size_t n = complete_type_length(p);
    if (n == 0)
      return 0;
    p += n;
  }
  return *p ? p - signature + 1 : 0;
}

std::vector<std::string> tcc_convert::split(const char *signature) {
  std::vector<std::string> out;
  while (*signature) {
    size_t n = complete_type_length(signature);
    if (n == 0)
      break;
    out.emplace_back(signature, n);
    signature += n;
  }
  return out;
}

static bool is_basic_int(int type) {
  return type == DBUS_TYPE_BYTE || type == DBUS_TYPE_INT16 ||
         type == DBUS_TYPE_UINT16 || type == DBUS_TYPE_INT32 ||
         type == DBUS_TYPE_UINT32 || type == DBUS_TYPE_INT64 ||
         type == DBUS_TYPE_UINT64;
}

static int64_t read_int(DBusMessageIter *iter, int type) {
  switch (type) {
  case DBUS_TYPE_BYTE: {
    uint8_t v;
    dbus_message_iter_get_basic(iter, &v);
    return v;
  }
  case DBUS_TYPE_INT16: {
    int16_t v;
    dbus_message_iter_get_basic(iter, &v);
    return v;
  }
  case DBUS_TYPE_UINT16: {
    uint16_t v;
    dbus_message_iter_get_basic(iter, &v);
    return v;
  }
  case DBUS_TYPE_INT32: {
    int32_t v;
    dbus_message_iter_get_basic(iter, &v);
    return v;
  }
  case DBUS_TYPE_UINT32: {
    uint32_t v;
    dbus_message_iter_get_basic(iter, &v);
    return v;
  }
  default: {
    // JSON only has the one integer type, so a t past INT64_MAX wraps.
    int64_t v;
    dbus_message_iter_get_basic(iter, &v);
    return v;
  }
  }
}

static bool append_int(DBusMessageIter *iter, int type, int64_t v) {
  switch (type) {
  case DBUS_TYPE_BYTE: {
    uint8_t n = v;
    return dbus_message_iter_append_basic(iter, type, &n);
  }
  case DBUS_TYPE_INT16: {
    int16_t n = v;
    return dbus_message_iter_append_basic(iter, type, &n);
  }
  case DBUS_TYPE_UINT16: {
    uint16_t n = v;
    return dbus_message_iter_append_basic(iter, type, &n);
  }
  case DBUS_TYPE_INT32: {
    int32_t n = v;
    return dbus_message_iter_append_basic(iter, type, &n);
  }
  case DBUS_TYPE_UINT32: {
    uint32_t n = v;
    return dbus_message_iter_append_basic(iter, type, &n);
  }
  default:
    return dbus_message_iter_append_basic(iter, type, &v);
  }
}

// Dict keys have to be strings in JSON.
static std::string key_from_dbus(DBusMessageIter *iter) {
  int type = dbus_message_iter_get_arg_type(iter);
  if (type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH ||
      type == DBUS_TYPE_SIGNATURE) {
    const char *s;
    dbus_message_iter_get_basic(iter, &s);
    return s;
  }
  if (type == DBUS_TYPE_BOOLEAN) {
    dbus_bool_t b;
    dbus_message_iter_get_basic(iter, &b);
    return b ? "true" : "false";
  }
  if (type == DBUS_TYPE_DOUBLE) {
    double d;
    dbus_message_iter_get_basic(iter, &d);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.17g", d);
    return buf;
  }
  return std::to_string(read_int(iter, type));
}

static bool key_to_dbus(const char *key, int type, DBusMessageIter *iter) {
  if (type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH ||
      type == DBUS_TYPE_SIGNATURE)
    return dbus_message_iter_append_basic(iter, type, &key);
  if (type == DBUS_TYPE_BOOLEAN) {
    dbus_bool_t b = strcmp(key, "true") == 0;
    return dbus_message_iter_append_basic(iter, type, &b);
  }
  if (type == DBUS_TYPE_DOUBLE) {
    double d = strtod(key, nullptr);
    return dbus_message_iter_append_basic(iter, type, &d);
  }
  if (is_basic_int(type))
    return append_int(iter, type, strtoll(key, nullptr, 10));
  return false;
}

bool tcc_convert::from_dbus(DBusMessageIter *iter, TCCVarlinkOut out,
                            TCCFds &fds) {
  int type = dbus_message_iter_get_arg_type(iter);
  switch (type) {
  case DBUS_TYPE_BOOLEAN: {
    dbus_bool_t b;
    dbus_message_iter_get_basic(iter, &b);
    return put_bool(out, b) == 0;
  }
  case DBUS_TYPE_DOUBLE: {
    double d;
    dbus_message_iter_get_basic(iter, &d);
    return put_float(out, d) == 0;
  }
  case DBUS_TYPE_STRING:
  case DBUS_TYPE_OBJECT_PATH:
  case DBUS_TYPE_SIGNATURE: {
    const char *s;
    dbus_message_iter_get_basic(iter, &s);
    return put_string(out, s) == 0;
  }
  case DBUS_TYPE_UNIX_FD: {
    // libdbus hands us our own duplicate.
    int fd = -1;
    dbus_message_iter_get_basic(iter, &fd);
    if (fd < 0)
      return false;
    fds.fds.push_back(fd);
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/fd/%d", getpid(), fd);
    return put_string(out, path) == 0;
  }
  case DBUS_TYPE_VARIANT: {
    DBusMessageIter sub;
    dbus_message_iter_recurse(iter, &sub);
    char *signature = dbus_message_iter_get_signature(&sub);
    VarlinkObject *variant = nullptr;
    bool ok = signature && varlink_object_new(&variant) == 0 &&
              varlink_object_set_string(variant, "signature", signature) ==
                  0 &&
              from_dbus(&sub, {variant, "value"}, fds) &&
              put_object(out, variant) == 0;
    dbus_free(signature);
    if (variant)
      varlink_object_unref(variant);
    return ok;
  }
  case DBUS_TYPE_STRUCT: {
    VarlinkObject *object = nullptr;
    if (varlink_object_new(&object) != 0)
      return false;
    DBusMessageIter sub;
    dbus_message_iter_recurse(iter, &sub);
    bool ok = true;
    for (int i = 0; ok && dbus_message_iter_get_arg_type(&sub) !=
                              DBUS_TYPE_INVALID;
         i++, dbus_message_iter_next(&sub))
      ok = from_dbus(&sub, {object, std::to_string(i).c_str()}, fds);
    ok = ok && put_object(out, object) == 0;
    varlink_object_unref(object);
    return ok;
  }
  case DBUS_TYPE_ARRAY: {
    char *signature = dbus_message_iter_get_signature(iter);
    bool dict = signature && signature[1] == '{';
    dbus_free(signature);
    DBusMessageIter sub;
    dbus_message_iter_recurse(iter, &sub);
    bool ok = true;
    if (dict) {
      VarlinkObject *object = nullptr;
      if (varlink_object_new(&object) != 0)
        return false;
      for (; ok && dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_DICT_ENTRY;
           dbus_message_iter_next(&sub)) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&sub, &entry);
        std::string key = key_from_dbus(&entry);
        dbus_message_iter_next(&entry);
        ok = from_dbus(&entry, {object, key.c_str()}, fds);
      }
      ok = ok && put_object(out, object) == 0;
      varlink_object_unref(object);
    } else {
      VarlinkArray *array = nullptr;
      if (varlink_array_new(&array) != 0)
        return false;
      for (; ok && dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID;
           dbus_message_iter_next(&sub))
        ok = from_dbus(&sub, {nullptr, nullptr, array}, fds);
      ok = ok && put_array(out, array) == 0;
      varlink_array_unref(array);
    }
    return ok;
  }
  default:
    if (is_basic_int(type))
      return put_int(out, read_int(iter, type)) == 0;
    fprintf(stderr, "tcc_portal: can't convert D-Bus type %c\n", type);
    return false;
  }
}

// Opens what an `h` value names. A /proc/<pid>/fd/<n> path gets the access
// mode the fd had, or a pipe's read end would become a write end too.
static int reopen(const char *path) {
  int flags = O_RDWR;
  int pid, fd, end = 0;
  if (sscanf(path, "/proc/%d/fd/%d%n", &pid, &fd, &end) == 2 &&
      path[end] == '\0') {
    char info_path[64];
    snprintf(info_path, sizeof(info_path), "/proc/%d/fdinfo/%d", pid, fd);
    if (FILE *info = fopen(info_path, "r")) {
      char line[128];
      unsigned int fd_flags;
      while (fgets(line, sizeof(line), info)) {
        if (sscanf(line, "flags: %o", &fd_flags) == 1) {
          flags = fd_flags & O_ACCMODE;
          break;
        }
      }
      fclose(info);
    }
  }

  int ret = open(path, flags | O_CLOEXEC | O_NOCTTY);
  if (ret >= 0 || errno != ENXIO)
    return ret;

  // A socket, e.g. for libei.
  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  if (strlen(path) >= sizeof(addr.sun_path))
    return -1;
  strcpy(addr.sun_path, path);
  ret = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (ret >= 0 && connect(ret, (sockaddr *)&addr, sizeof(addr)) != 0) {
    close(ret);
    return -1;
  }
  return ret;
}

bool tcc_convert::to_dbus(TCCVarlinkIn in, const std::string &signature,
                          DBusMessageIter *iter) {
  int type = signature[0];
  switch (type) {
  case DBUS_TYPE_BOOLEAN: {
    bool b;
    if (get_bool(in, &b) != 0)
      return false;
    dbus_bool_t v = b;
    return dbus_message_iter_append_basic(iter, type, &v);
  }
  case DBUS_TYPE_DOUBLE: {
    double d;
    return get_float(in, &d) == 0 &&
           dbus_message_iter_append_basic(iter, type, &d);
  }
  case DBUS_TYPE_STRING:
  case DBUS_TYPE_OBJECT_PATH:
  case DBUS_TYPE_SIGNATURE: {
    const char *s;
    return get_string(in, &s) == 0 &&
           dbus_message_iter_append_basic(iter, type, &s);
  }
  case DBUS_TYPE_UNIX_FD: {
    const char *path;
    if (get_string(in, &path) != 0)
      return false;
    int fd = reopen(path);
    if (fd < 0) {
      fprintf(stderr, "tcc_portal: could not open %s: %s\n", path,
              strerror(errno));
      return false;
    }
    // libdbus keeps its own duplicate.
    bool ok = dbus_message_iter_append_basic(iter, type, &fd);
    close(fd);
    return ok;
  }
  case DBUS_TYPE_VARIANT: {
    VarlinkObject *variant;
    const char *contained;
    if (get_object(in, &variant) != 0 ||
        varlink_object_get_string(variant, "signature", &contained) != 0 ||
        complete_type_length(contained) != strlen(contained) ||
        !*contained)
      return false;
    DBusMessageIter sub;
    return dbus_message_iter_open_container(iter, type, contained, &sub) &&
           to_dbus({variant, "value"}, contained, &sub) &&
           dbus_message_iter_close_container(iter, &sub);
  }
  case DBUS_STRUCT_BEGIN_CHAR: {
    VarlinkObject *object;
    if (get_object(in, &object) != 0)
      return false;
    std::vector<std::string> members =
        split(signature.substr(1, signature.size() - 2).c_str());
    DBusMessageIter sub;
    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, nullptr,
                                          &sub))
      return false;
    for (size_t i = 0; i < members.size(); i++) {
      if (!to_dbus({object, std::to_string(i).c_str()}, members[i], &sub))
        return false;
    }
    return dbus_message_iter_close_container(iter, &sub);
  }
  case DBUS_TYPE_ARRAY: {
    std::string element = signature.substr(1);
    DBusMessageIter sub;
    if (element[0] == DBUS_DICT_ENTRY_BEGIN_CHAR) {
      VarlinkObject *object;
      if (get_object(in, &object) != 0)
        return false;
      std::vector<std::string> kv =
          split(element.substr(1, element.size() - 2).c_str());
      const char **names = nullptr;
      long n = varlink_object_get_field_names(object, &names);
      if (n < 0 || kv.size() != 2 ||
          !dbus_message_iter_open_container(iter, type, element.c_str(),
                                            &sub)) {
        free(names);
        return false;
      }
      bool ok = true;
      for (long i = 0; ok && i < n; i++) {
        DBusMessageIter entry;
        ok = dbus_message_iter_open_container(&sub, DBUS_TYPE_DICT_ENTRY,
                                              nullptr, &entry) &&
             key_to_dbus(names[i], kv[0][0], &entry) &&
             to_dbus({object, names[i]}, kv[1], &entry) &&
             dbus_message_iter_close_container(&sub, &entry);
      }
      free(names);
      return ok && dbus_message_iter_close_container(iter, &sub);
    }
    VarlinkArray *array;
    if (get_array(in, &array) != 0 ||
        !dbus_message_iter_open_container(iter, type, element.c_str(), &sub))
      return false;
    unsigned long n = varlink_array_get_n_elements(array);
    for (unsigned long i = 0; i < n; i++) {
      if (!to_dbus({nullptr, nullptr, array, i}, element, &sub))
        return false;
    }
    return dbus_message_iter_close_container(iter, &sub);
  }
  default:
    if (is_basic_int(type)) {
      int64_t v;
      return get_int(in, &v) == 0 && append_int(iter, type, v);
    }
    fprintf(stderr, "tcc_portal: can't convert to D-Bus type %s\n",
            signature.c_str());
    return false;
  }
}
