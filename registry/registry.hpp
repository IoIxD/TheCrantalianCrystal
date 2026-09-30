#pragma once

#include "varlink_loader.hpp"

class TCCRegistryConnection {
  bool mValidConn = true;
  VarlinkConnection *mConn = nullptr;

public:
  TCCRegistryConnection();

  bool open() { return mValidConn; };
  void InitiateClose();
  void step();
};
