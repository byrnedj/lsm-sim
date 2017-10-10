
#ifndef MRC_H
#define MRC_H

#include <map>
#include "policy.h"

/**
 * Computes the miss-ratio curve using AET algorithm presented
 * in USENIX '16 by Hu et. al
 */

/*
 * Constants in AET
 */

#define PGAP 10000 //mrc output step
#define MAXL 100000+3 //number of bins in  reuse time histogram
#define MAXH 1000000 //size of hash table
#define domain 256 //reuse time histogram compression factor
#define STEP 10000 // sampling rate
#define MAX_TENANTS 10 //max number of MRCs to create
                        //MAX_TENANTS+1 is the combined MRC


class mrc {
    
    public:
        mrc();
        ~mrc();

        void sample(uint32_t key);
        long long getN();
        long long solve(long long *c_size_idx, double* miss_rate);
    private:
        long long AET_domain_value_to_index(long long value);
        long long AET_domain_index_to_value(long long index);

        /*
         * AET variables
         */
        //reuse time distribution
        long long AET_rtd[MAXL];
        
        //Hash table to track logical reuse time
        std::map<uint32_t,long long> AET_hash;
        
        //number of accesses recorded
        //in rtd
        long long AET_n;
        //number of cold misses
        long long AET_m;
        
        //counts for hash table
        long long AET_node_cnt;
        long long AET_node_max;
        
        //for sampling, if n==loc insert to hash table
        long long AET_loc;
        
        //AET hash table total
        long long AET_tott;
}; 

#endif





