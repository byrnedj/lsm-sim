#ifndef FIFO_H
#define FIFO_H

#include "policy.h"
#include <unordered_map>
#include <list>

class fifo : public policy {
  typedef std::unordered_map<uint32_t, request*> hash_map;

  public:
    fifo(uint64_t size);
   ~fifo();
    int64_t proc(const request *r, bool warmup);
    uint32_t get_size(); 

    void log_header();
    void log();

  private:
    // Number of access requests fed to the cache.
    size_t accesses;

    // Subset of accesses which hit in the simulated cache.
    size_t hits;

    uint32_t current_size;  // the current number of bytes in eviction queue

    hash_map hash; 
    std::list<request> queue; 
};






#endif
