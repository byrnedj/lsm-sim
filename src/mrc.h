
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

#define domain 256 //reuse time histogram compression factor 

extern size_t AET_sample_rate;
extern size_t AET_PGAP;
extern size_t AET_RTH;

#define OSIZE 200 //object size

class mrc {
    
    public:
        mrc();
        ~mrc();
        //copy cons
        mrc(const mrc &m);
        //override =
        mrc & operator=(const mrc &m);

        void sample(uint64_t key);
        size_t get_hash_table_size();

        uint64_t solve_MRC();
        
        //not used yet, may be defined as 95% of max HR
        uint64_t wss;

        //MRC
        std::map<uint64_t, double> MRC;
    private:
        uint64_t AET_domain_value_to_index(uint64_t value);
        uint64_t AET_domain_index_to_value(uint64_t index);

        /*
         * AET variables
         */
        
        //Hash table to track logical reuse time
        std::map<uint32_t,uint64_t> AET_hash;
        
        
        //number of accesses recorded
        //in rtd
        uint64_t AET_n;
        //number of cold misses
        uint64_t AET_m;
        
        //counts for hash table
        uint64_t AET_node_cnt;
        uint64_t AET_node_max;
        
        //for sampling, if n==loc insert to hash table
        uint64_t AET_loc;
        
        //AET hash table total
        uint64_t AET_tott;
        
        //reuse time distribution
        uint64_t *AET_rtd;
       
}; 

#endif





