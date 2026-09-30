#pragma once

#include "varlink_loader.hpp"

class TCCVarlinkConnection {
  bool mValidConn = true;
  VarlinkConnection *mConn = nullptr;

public:
  TCCVarlinkConnection();

  bool open() { return mValidConn; };
  void InitiateClose();
  void step();
};
