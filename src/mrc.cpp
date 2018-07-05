#include "mrc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


/* Init AET structures */


mrc::mrc()
    : wss{0}
    , MRC{}
    , AET_hash{}
    , AET_n{0}
    , AET_m{0}
    , AET_node_cnt{0}
    , AET_node_max{0}
    , AET_loc{}
    , AET_tott{0}
    , AET_rtd{}
{
    srand(128);
    AET_rtd = (uint64_t*) calloc(AET_RTH,sizeof(uint64_t));
    AET_loc = rand()%(AET_sample_rate*2)+1;

}

mrc::mrc(const mrc &m)
    : wss{0}
    , MRC{}
    , AET_hash{}
    , AET_n{0}
    , AET_m{0}
    , AET_node_cnt{0}
    , AET_node_max{0}
    , AET_loc{}
    , AET_tott{0}
    , AET_rtd{}
{
    srand(128);
    AET_rtd = m.AET_rtd;
    AET_loc = rand()%(AET_sample_rate*2)+1;

}

mrc::~mrc() {}


uint64_t mrc::get_hash_table_size() { return AET_hash.size(); }

/* Helper methods for AET */

uint64_t mrc::AET_domain_value_to_index(uint64_t value)
{
    uint64_t loc = 0, step = 1;
    int index = 0;
    while (loc+(step*domain) < value) 
    {
        loc += step*domain;
        step *= 2;
        index += domain;
    }
    while (loc < value) 
    {
        index++;
        loc += step;
    }
    return index;
}

uint64_t mrc::AET_domain_index_to_value(uint64_t index)
{
    uint64_t value = 0, step = 1;
    while (index > domain) 
    {
        value += step*domain;
        step *= 2;
        index -= domain;
    }
    while (index > 0) 
    {
        value += step;
        index--;
    }
    return value;
}

void mrc::sample(uint64_t kid)
{

    AET_n++;
    auto it = AET_hash.find(kid);
    //if found get the time
    if (it != AET_hash.end())
    {

        uint64_t t = it->second;
        AET_rtd[AET_domain_value_to_index(AET_n-t)]++;
        AET_tott++;
        //remove it from table
        AET_hash.erase(it);
        AET_node_cnt--;
    }
    //check if we should add this sample to hash table
    if (AET_n == AET_loc)
    {
        uint64_t n = AET_n;
        uint64_t nkey = kid;
        AET_hash[nkey] = n;

        AET_loc += rand()%(AET_sample_rate*2)+1;
        AET_node_cnt++;
    }
    if (AET_node_cnt > AET_node_max)
        AET_node_max = AET_node_cnt;
    
}



uint64_t mrc::solve_MRC()
{

    //get number of cold misses
    AET_m = AET_node_cnt*AET_sample_rate;


    double sum = 0; 
    uint64_t T = 0;
    double tot = 0;

    //tott is total references in HT
    //m is number of cold misses
    //N should be total references including cold misses
    double N = AET_tott+1.0*AET_tott/
        (AET_n-AET_m)*AET_m;

    //sanity check to make sure we actually have refs
    if (N < 1) return 0;

    uint64_t cache_size = 0;

    uint64_t step = 1; 
    int dom = 0;
    int dT = 0; 
    uint64_t loc = 0;
    uint64_t c_index = 0;
    
    uint64_t c = 1;
    double cmiss_rate = 0;
    
    while (c <= AET_m)
    {
        while (T <= AET_n && tot/N < c) 
        {
            tot += N-sum;
            T++;
            if (T > loc) 
            {
                if (++dom > domain) 
                {
                    dom = 1;
                    step *= 2;
                }
                loc += step;
                dT++;
            }
            sum += 1.0*AET_rtd[dT]/step;
        }
        //how to get AET(c), we know mr(c) = P(AET(c))
        //so (N-sum)/N must be P(AET(c))
        //T == AET(c)
        if (c % AET_PGAP == 0)
        {
            cmiss_rate = (N-sum)/N;
            cache_size = c;
            
            c_index++;
            MRC[cache_size] = cmiss_rate;

        }
        c++;
    }


    //double tmiss_rate = cmiss_rate + 0.05;
    //uint64_t icache_size = cache_size;
    
    return cache_size;
}
