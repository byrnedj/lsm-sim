#include "policy.h"
#include <iostream>

Policy::Policy (const uint64_t gbl_mem) {

  this->global_mem = gbl_mem;

  std::cout << "policy constructed" << std:: endl;

}

Policy::~Policy() {

}








