#include "mrc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


/* Init AET structures */


mrc::mrc()
    : AET_hash{}
    , AET_n{0}
    , AET_m{0}
    , AET_node_cnt{0}
    , AET_node_max{0}
    , AET_loc{}
    , AET_tott{0}
{
    srand(128);
    memset(AET_rtd,0,sizeof(long long)*MAXL);
    AET_loc = rand()%(STEP*2)+1;

}

mrc::~mrc() {
}

/* Helper methods for AET */

long long mrc::AET_domain_value_to_index(long long value)
{
    long long loc = 0, step = 1;
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

long long mrc::AET_domain_index_to_value(long long index)
{
    long long value = 0, step = 1;
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

void mrc::sample(uint32_t kid)
{

    AET_n++;
    auto it = AET_hash.find(kid);
    //if found get the time
    if (it != AET_hash.end())
    {

        long long t = it->second;
        AET_rtd[AET_domain_value_to_index(AET_n-t)]++;
        AET_tott++;
        //remove it from table
        AET_hash.erase(it);
        AET_node_cnt--;
    }
    //check if we should add this sample to hash table
    if (AET_n == AET_loc)
    {
        long long n = AET_n;
        uint32_t nkey = kid;
        AET_hash[nkey] = n;

        AET_loc += rand()%(STEP*2)+1;
        AET_node_cnt++;
    }
    if (AET_node_cnt > AET_node_max)
        AET_node_max = AET_node_cnt;
    
}


long long mrc::getN()
{
    double N = AET_tott+1.0*AET_tott/
                (AET_n-AET_m)*AET_m;
    return N;
}

long long mrc::solve(long long* cache_size_index, 
                      double* miss_rate)
{

    //get number of cold misses
    AET_m = AET_node_cnt*STEP;
    
    double sum = 0; 
    long long T = 0;
    double tot = 0;

    //tott is total references in HT
    //m is number of cold misses
    //N should be total references including cold misses
    double N = AET_tott+1.0*AET_tott/
        (AET_n-AET_m)*AET_m;
    
    cache_size_index = (long long*)malloc(
                                     sizeof(long long)*(int)N);
    miss_rate = (double*)malloc(sizeof(double)*(int)N);
   
    double cmiss_rate = 2;
    long long cache_size = 0;


    long long step = 1; 
    int dom = 0;
    int dT = 0; 
    long long loc = 0;
    long long c_index = 0;
    
    
    long long c = 1;
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
        if (c % PGAP == 0)
        {
            //pmiss_rate = cmiss_rate;
            cmiss_rate = (N-sum)/N;
            cache_size = c;
            
            cache_size_index[c_index] = cache_size;
            miss_rate[c_index] = cmiss_rate;
            c_index++;
            //if ((c/10000) >= (total_keys/10000) )
            //{
            //    //if (log == 1)
            //    //    printf("AET cache size: %lld, 
            //    //            AET miss rate: %.4f\n"
            //    //            c,cmiss_rate);
            //    break;
            //}
        }
        c++;
    }
    
    ////look for smaller cache size within miss-rate 
    //threshold
    //for (int i = 0; i < c_index; i++)
    //{
    //    if ( ((miss_rate[i] - cmiss_rate) <= threshold) )
    //    {
    //        cache_size = cache_size_index[i];
    //        break;
    //    }
    //    //else if (last == 1)
    //    //{
    //    //    cache_size = cache_size_index[i];
    //    //}
    //}


    //return cache_size;
    return 1;
}
