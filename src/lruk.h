#ifndef LRUK_H
#define LRUK_H

#include <iostream>
#include <vector>
#include <list>
#include <unordered_map>
#include "policy.h"

extern size_t K_LRU;
extern size_t KLRU_QUEUE_SIZE;

class Lruk : public policy {
private:
	typedef std::list<uint64_t>::iterator keyIt;

	struct LKItem {
		uint64_t kId;
		int32_t size;
		keyIt iter;
		size_t queueNumber;
		
		LKItem() : kId(0), size(0), iter(), queueNumber(0){}		
	};

	std::vector<size_t> kLruSizes;
	std::vector< std::list<uint64_t> > kLru;
	std::unordered_map<uint64_t, LKItem> allObjects;
	std::vector<size_t> kLruHits;
	std::vector<size_t> kLruNumWrites;

	void insert(std::vector<uint64_t>& objects, 
		size_t sum, 
		size_t k, 
		bool updateWrites,
		bool warmup);
public:
	Lruk(stats stat);
	~Lruk();
	size_t proc(const request *r, bool warmup);
	size_t get_bytes_cached() const;
	void dump_stats(void);
};

#endif
