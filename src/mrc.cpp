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
                (AET_n-(AET_node_cnt*STEP))*(AET_node_cnt*STEP);
    return N;
}


void mrc::balance(long long *app_idx, double *app_mrc, 
                  long long app_actual, long long app_upper,
                  long long app_reserved, 
                  long long *other_idx, double *other_mrc,
                  long long other_actual, long long other_upper,
                  long long other_reserved,
                  long long *app_m, long long *other_m,
                  double *m1, double *m2)
{
    int i = 0;
    int j = 0;
    int k = 0;
    int l = 0;
    double min1 = 2;
    double min2 = 2;
    
    long long app_min1 = -1;
    long long other_min1 = -1;
    
    long long app_min2 = -1;
    long long other_min2 = -1;


    while (app_idx[i] != app_actual ) i++;
    while (other_idx[j] != other_actual) j++;

    k = i;
    l = j;

    //print out rebalancing info
    //std::cerr << "upper "  << "app " << app_upper <<  "\n";
    //std::cerr << "actual " << "app " << app_idx[i] <<  "\n";
    //std::cerr << "actual " << "other " << other_idx[j] <<  "\n";
    //std::cerr << "lower "  << "other " << other_reserved <<  "\n";



    while ( app_idx[i] < app_upper  &&
            other_idx[j] > other_reserved )
    {
        double t = (app_mrc[i] + other_mrc[j])/2;
        //std::cerr << "mr app[" << i*PGAP << "]: " << app_mrc[i] << "\n";
        //std::cerr << "mr other[" << j*PGAP << "]: " << other_mrc[j] << "\n";
        //std::cerr << "mr avg: " << t << "\n";
        if (t < min1)
        {
            min1 = t;
            app_min1 = app_idx[i];
            other_min1 = other_idx[j];
        }
        i++;
        j--;
    }

    //std::cerr << "second direction" << "\n";
    
    while ( other_idx[l] < other_upper  &&
            app_idx[k] > app_reserved )
    {
        double t = (app_mrc[k] + other_mrc[l])/2;
        //std::cerr << "mr app[" << k*PGAP << "]: " << app_mrc[k] << "\n";
        //std::cerr << "mr other[" << l*PGAP << "]: " << other_mrc[l] << "\n";
        //std::cerr << "mr avg: " << t << "\n";
        if (t < min2)
        {
            min2 = t;
            app_min2 = app_idx[k];
            other_min2 = other_idx[l];
        }
        k--;
        l++;
    }

    std::cerr << "app min1 " << app_min1 << "\n";
    std::cerr << "other min1 " << other_min1 << "\n";
    std::cerr << "min1 " << min1 << "\n";
    
    std::cerr << "app min2 " << app_min2 << "\n";
    std::cerr << "other min2 " << other_min2 << "\n";
    std::cerr << "min2 " << min2 << "\n";

    if (min1 < min2)
    {
        *app_m = app_min1;
        *other_m = other_min1;
    }
    else
    {
        *app_m = app_min2;
        *other_m = other_min2;
    }
    *m1 = min1;
    *m2 = min2;

}


//ADD N!
long long mrc::solve(long long* cache_size_index, 
                      double* miss_rate,
                      long long *app_max)
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
            //std::cerr << cmiss_rate << "," << c <<  "\n";
        }
        c++;
    }
   
    *app_max = cache_size_index[c_index-1];
    return 1;
}
