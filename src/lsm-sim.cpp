#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <set>
#include <cstdint>
#include <getopt.h>
#include <cmath>
#include <boost/version.hpp>
#include <cstdio>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <memory>
#include <cassert>


//netio
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

#include "common.h"
#include "request.h"
#include "fifo.h"
#include "shadowlru.h"
#include "shadowslab.h"
#include "partslab.h"
#include "lsm.h"
#include "lsc_multi.h"
#include "mrc.h"
#include "slab_multi.h"
#include "lru.h"
#include "slab.h"
#include "mc.h"
#include "flash_cache.h"
#include "victim_cache.h"
#include "ripq.h"
#include "ripq_shield.h"
#include "lruk.h"
#include "clock.h"
#include "segment_util.h"
#include "flash_cache_lruk.h"
#include "ram_shield.h"
#include "ram_shield_fifo.h"
#include "ram_shield_sel.h"
#include "flash_shield.h"
#include "flash_cache_lruk_clock.h"
#include "replay.h"
#include "flash_cache_lruk_clock_machinelearning.h"

namespace ch = std::chrono;
typedef ch::high_resolution_clock hrc;

const char* policy_names[24] = { "shadowlru"
                               , "fifo"
                               , "lru"
                               , "slab"
                               , "shadowslab"
                               , "partslab"
                               , "lsm"
                               , "multi"
                               , "multislab"
                               , "flashcache"
                               , "victimcache"
                               , "lruk"
                               , "ripq"
                               , "ripq_shield"
                               , "clock"
                               , "flashcachelruk"
                               , "flashcachelrukclk"
                               , "segment_util"
                               , "ramshield"
                               , "ramshield_fifo"
                               , "ramshield_sel"
                               , "replay"
                               , "flashshield"
							   , "flashcachelrukclkmachinelearning"
                              };

enum pol_type { 
    SHADOWLRU = 0
  , FIFO
  , LRU
  , SLAB
  , SHADOWSLAB
  , PARTSLAB
  , LSM
  , MULTI
  , MULTISLAB
  , FLASHCACHE
  , VICTIMCACHE 
  , LRUK
  , RIPQ 
  , RIPQ_SHIELD
  , CLOCK
  , FLASHCACHELRUK
  , FLASHCACHELRUKCLK
  , SEGMENT_UTIL
  , RAMSHIELD
  , RAMSHIELD_FIFO
  , RAMSHIELD_SEL
  , REPLAY
  , FLASHSHIELD
  , FLASHCACHELRUKCLKMACHINELEARNING
  };

std::set<uint32_t> apps{}; //< apps to consider

/// How much each app steals per shadow q hit.
std::unordered_map<uint32_t, uint32_t> app_steal_sizes{};
bool all_apps = true; //< run all by default 
bool roundup  = false; //< no rounding default
float lsm_util = 1.0; //< default util factor
std::string trace = "data/m.cap.out"; //< default filepath
std::string app_str; //< for logging apps
std::string app_n; //< for logging apps sizes
std::string app_steal_sizes_str;
double hit_start_time = 1; //< default warmup time
size_t global_mem = 0;
pol_type policy_type;
lsc_multi::subpolicy subpolicy;
bool verbose = false;
double gfactor = 1.25; //< Slab growth factor.
bool memcachier_classes = false;
size_t partitions = 2;
size_t segment_size = 1 * 1024 * 1024;
size_t block_size = 1 * 1024 * 1024;
size_t num_dsections = 4;
size_t min_mem_pct = 75;
const size_t default_steal_size = 65536;
bool use_tax = false;
double tax_rate = 0.05;
double priv_mem_percentage =0.25;
bool use_percentage = false; // specify priv mem %
size_t flash_size = 0;
size_t num_sections = 0;
double USER_SVM_TH=1;

/// Amount of dram memory allocated for ripq_shield active_blocks.
size_t dram_size = 0;
double threshold = 0.7;
size_t cleaning_width = 10;

// Only parse this many requests from the CSV file before breaking.
// Helpful for limiting runtime when playing around.
int request_limit = 0;

bool debug = false;

//use synthetic traces
bool syn = false;
bool mrc = false;

//read ascii commands, see
//memcached documentation for format
//this is used to evaluate timing
bool net = false;

/* AET params */
size_t AET_sample_rate = 10000;
size_t AET_repart = 100000;
size_t AET_PGAP = 500;
size_t AET_RTH = 100000+3;

/* CH params */
size_t CH_shq_size = 10;
size_t CH_cz = 500;

/* window length for running hit rate */
size_t W_LEN = 50000;
bool lru_test = false;

/* noisy neighbor number of reqs */
size_t noisyN = -1;

const std::string usage =
  "-f    specify file path\n"
  "-a    specify app to eval\n"
  "-r    enable rounding\n"
  "-u    specify utilization\n"
  "-w    specify warmup period\n"
  "-l    number of requests after warmup\n"
  "-s    simulated cache size in bytes\n"
  "-p    policy 0, 1, 2; shadowlru, fifo, lru\n"
  "-v    incremental output\n"
  "-g    specify slab growth factor\n"
  "-M    use memcachier slab classes\n"
  "-P    number of partitions for partslab\n"
  "-S    segment size in bytes for lsm\n"
  "-B    block size in bytes for ripq and ram_shield\n"
  "-E    eviction subpolicy (for multi)\n"
  "-m    private mem percentage of target mem\n"
  "-W    per-application weights\n"
  "-c    cleaning width\n"
  "-D    dram size in flashcache, victimcache and ripq_shield policies\n"
  "-F    flash size in flashcache, victimcache, ripq and ripq_shield policies\n"
  "-K    number of queues in lruk policy\n"
  "-L    queue size in lruk policy\n"
  "-H    hit value in flashcache formula\n"
  "-n    number of flash sections for ripq and ripq_shield\n" 
  "-Y    Use synthetic trace set\n" 
  "-R    Use full MRC guided partition\n" 
  "-Z    Recieve input from mutilate ascii port 2017\n" 
  "-d    number of dram sections for ripq_shield\n";

// memcachier slab allocations at t=86400 (24 hours)
const int orig_alloc[15] = {
  1664, 2304, 512, 17408, 266240, 16384, 73728, 188416, 442368,
  3932160, 11665408, 34340864, 262144, 0 , 0
};

std::unordered_map<size_t, size_t> memcachier_app_size = { {1, 701423104}
                                                         , {2, 118577408}
                                                         , {3, 19450368}
                                                         , {5, 35743872}
                                                         , {6, 7108608}
                                                         , {7, 77842880}
                                                         , {8, 10485760}
                                                         , {10, 684898304}
                                                         , {11, 7829952}
                                                         , {13, 36647040}
                                                         , {19, 51209600}
                                                         , {18, 6313216}
                                                         , {20, 70953344}
                                                         , {23, 4542897472}
                                                         , {29, 187378624}
                                                         , {31, 1409535488}
                                                         , {53, 11044096}
                                                         , {59, 1713664}
                                                         , {94, 23238784}
                                                         , {227, 20237184}
                                                         };


//i believe this is in bytes...
//std::unordered_map<size_t, size_t>        syn_app_size = { {1, 2425000000}
//                                                         , {2, 820000000}
//                                                         };

//APP 1 is ETC
//APP 2 is PSA
std::unordered_map<size_t, size_t>        syn_app_size = { {1, 50000*OSIZE}
                                                         , {2, 50000*OSIZE}
                                                         , {3, 50000*OSIZE}
                                                         , {4, 50000*OSIZE}
                                                         , {5, 50000*OSIZE}
                                                         , {6, 50000*OSIZE}
                                                         , {7, 50000*OSIZE}
                                                         , {8, 50000*OSIZE}
                                                         , {9, 50000*OSIZE}
                                                         , {10,50000*OSIZE}
                                                         , {11,50000*OSIZE}
                                                         , {12,50000*OSIZE}
                                                         , {13,50000*OSIZE}
                                                         , {14,50000*OSIZE}
                                                         , {15,50000*OSIZE}
                                                         , {16,50000*OSIZE}
                                                         , {100,1*OSIZE}
                                                                
                                                         };


int main(int argc, char *argv[]) {
  // calculate global memory
  for (int i = 0; i < 15; i++)
    global_mem += orig_alloc[i];
  global_mem *= lsm_util;

  // parse cmd args
  int c;
  std::vector<int32_t> ordered_apps{};
  while ((c = getopt(argc, argv,
                     "p:s:l:f:a:ru:w:vhg:MP:S:E:N:W:T:t:"
                     "m:d:F:n:D:L:K:k:C:c:YP:RP:x:G:H:I:J:Q:U:i:j:Z:")) != -1)
  {
    switch (c)
    {
        case 'G':
            AET_sample_rate = atol(optarg);
            break;
        case 'H':
            AET_repart = atol(optarg);
            break;
        case 'I':
            AET_PGAP = atol(optarg);
            break;
        case 'J':
            CH_cz = atol(optarg);
            break;
        case 'Q':
            CH_shq_size = atol(optarg);
            break;
        case 'U':
            AET_RTH = atol(optarg);
            break;

        //include noisy neighbor (only writes), argument is the number of
        //writes to include, is set as the last app
        case 'j':
            noisyN = atol(optarg);
            break;
        case 'i':
            W_LEN = atol(optarg);
            break;

        //Y is for synthetic trace set 
      case 'Y':
        syn = true;
        break;
     //Z is to do LRU test 
      case 'Z':
      {  
        long flag = atol(optarg);
        if (flag == 1)
            lru_test = true;
        else
            lru_test = false;
      } 
        break;

        //R is for using full miss RATIO curve
        //to guide allocation
      case 'R':
        mrc = true;
        break;
      //only if we are using synthetic apps, specify alloc sizes
      //go in appid id order
      case 'x':
        {
          string_vec v;
          csv_tokenize(std::string(optarg), &v);
          int appid = 1;
          for (const auto& e : v) {
            size_t n = stoi(e);
            syn_app_size[appid] = n;
            appid = appid + 1;
            app_n += e;
            app_n += ",";
          }
          break;
        }
      case 'f':
        trace = optarg;
        break;
      case 'p':
        if (std::string(optarg) == "shadowlru")
          policy_type = pol_type(0); 
        else if (std::string(optarg) == "fifo")
          policy_type = pol_type(1); 
        else if (std::string(optarg) == "lru")
          policy_type = pol_type(2); 
        else if (std::string(optarg) == "slab")
          policy_type = pol_type(3);
        else if (std::string(optarg) == "shadowslab")
          policy_type = pol_type(4);
        else if (std::string(optarg) == "partslab")
          policy_type = pol_type(5);
        else if (std::string(optarg) == "lsm")
          policy_type = pol_type(6);
        else if (std::string(optarg) == "multi")
          policy_type = pol_type(7);
        else if (std::string(optarg) == "multislab")
          policy_type = pol_type(8);
        else if (std::string(optarg) == "flashcache")
          policy_type = pol_type(9);
        else if (std::string(optarg) == "victimcache")
          policy_type = pol_type(10);
        else if (std::string(optarg) == "lruk")
          policy_type = pol_type(11);
        else if (std::string(optarg) == "ripq")
          policy_type = pol_type(12);
        else if (std::string(optarg) == "ripq_shield")
          policy_type = pol_type(13);
        else if (std::string(optarg) == "clock")
          policy_type = pol_type(14);
        else if (std::string(optarg) == "flashcachelruk")
          policy_type = pol_type(15);
        else if (std::string(optarg) == "flashcachelrukclk")
          policy_type = pol_type(16);
        else if (std::string(optarg) == "segment_util")
          policy_type = pol_type(17);
        else if (std::string(optarg) == "ramshield")
          policy_type = pol_type(18);  
        else if (std::string(optarg) == "ramshield_fifo")
          policy_type = pol_type(19);
        else if (std::string(optarg) == "ramshield_sel")
          policy_type = pol_type(20);
        else if (std::string(optarg) == "replay")
          policy_type = pol_type(21);
        else if (std::string(optarg) == "flashshield")
          policy_type = pol_type(22);
		else if (std::string(optarg) == "flashcachelrukclkmachinelearning")
          policy_type = pol_type(23);
        else {
          std::cerr << "Invalid policy specified" << std::endl;
          exit(EXIT_FAILURE);
        }
        break;
      case 'E':
        if (std::string(optarg) == "normal")
          subpolicy = lsc_multi::subpolicy(0); 
        else if (std::string(optarg) == "greedy")
          subpolicy = lsc_multi::subpolicy(1); 
        else if (std::string(optarg) == "static")
          subpolicy = lsc_multi::subpolicy(2); 
        else {
          std::cerr << "Invalid subpolicy specified" << std::endl;
          exit(EXIT_FAILURE);
        }            
        break;
      case 'N':
        min_mem_pct = atol(optarg);
        break;
      case 's':
        global_mem = atol(optarg);
        break;
      case 'S':
        segment_size = atol(optarg);
        break;
      case 'l':
        request_limit = atoi(optarg); 
        break;
      case 'a':
        {
          string_vec v;
          csv_tokenize(std::string(optarg), &v);
          for (const auto& e : v) {
            all_apps = false;
            int i = stoi(e);
            apps.insert(i);
            ordered_apps.push_back(i);
            app_str += e;
            app_str += ",";
          }
          break;
        }
      case 'r':
        roundup = true;
        break;
      case 'u':
        lsm_util = atof(optarg);
        break;   
      case 'w':
        hit_start_time = atof(optarg);
        break;
      case 'v':
        verbose = true;
        break;
      case 'h': 
        std::cerr << usage << std::endl;
        break;
      case 'g':
        gfactor = atof(optarg);  
        break;
      case 'M':
        memcachier_classes = true;
        break;
      case 'P':
        partitions = atoi(optarg); 
        break;
      case 'W':
        {
          string_vec v{};
          csv_tokenize(optarg, &v);
          int i = 0;
          for (const auto& e : v) {
            app_steal_sizes.insert(std::make_pair(ordered_apps.at(i++), stoi(e)));
            app_steal_sizes_str += e;
            app_steal_sizes_str += ",";
          }
          break;
        }
      case 'c':
        cleaning_width = atol(optarg);
        break;
      case 'T':
        use_tax = true;
        tax_rate = atol(optarg) / 100.;
        break;
      case 'm':
        priv_mem_percentage = atof(optarg);
        use_percentage = true;
        break;
      case 'F':
        FLASH_SIZE = flash_size = atol(optarg);
        FLASH_SIZE_FC_KLRU = flash_size = atol(optarg);
        FLASH_SIZE_FC_KLRU_CLK = flash_size = atol(optarg);
        FLASH_SHILD_FLASH_SIZE = flash_size = atol(optarg);
		FLASH_SIZE_FC_KLRU_CLK_ML = flash_size = atol(optarg);
        break;
      case 'D':
        DRAM_SIZE = dram_size = atol(optarg);
        DRAM_SIZE_FC_KLRU = dram_size = atol(optarg);
        DRAM_SIZE_FC_KLRU_CLK = dram_size = atol(optarg);
        FLASH_SHILD_DRAM_SIZE = dram_size = atol(optarg);
		DRAM_SIZE_FC_KLRU_CLK_ML = dram_size = atol(optarg);
	break;
      case 'K':
        K_LRU = atol(optarg);
        break;
      case 'L':
        KLRU_QUEUE_SIZE = atol(optarg);
        break;  
      case 'n':
        num_sections =  atol(optarg);
        break;
      case 'd':
        num_dsections = atol(optarg);
        break;
      case 'k':
        K = atof(optarg);
        break;
      case 't':
        threshold = atof(optarg);
        break;
      case 'C':
        CLOCK_MAX_VALUE = atol(optarg);
        CLOCK_MAX_VALUE_KLRU = atol(optarg);
        break;
    }
  }

  /* setup noisy neighbor */
  //if (noisyN > 0)
  //{
  //    int noisyid = 100;
  //    app_str += "100";
  //    app_str += ",";
  //    apps.insert(noisyid);
  //    ordered_apps.push_back(noisyid);
  //}
  assert(apps.size() >= app_steal_sizes.size());
  

  if (policy_type == FLASHSHIELD ||
      policy_type == FLASHCACHE ||
      policy_type == FLASHCACHELRUK ||
      policy_type == FLASHCACHELRUKCLK ||
      policy_type == VICTIMCACHE ||
      policy_type == RIPQ ||
	  policy_type == FLASHCACHELRUKCLKMACHINELEARNING ||
      policy_type == RIPQ_SHIELD)
  {
    global_mem = DRAM_SIZE + FLASH_SIZE;
  }

  // build a stats struct with basic info relevant to every policy.
  stats sts{policy_names[policy_type], &apps, global_mem};

  //printf("APPID: %lu\n", appid);
 
  // instantiate a policy
  std::unique_ptr<policy> policy{};
  switch(policy_type) {
    case SHADOWLRU:
      policy.reset(new shadowlru(sts));
      break;
    case FIFO : 
      policy.reset(new fifo(sts));
      break;
    case LRU : 
      policy.reset(new lru(sts));
      break;
    case FLASHCACHE :
      policy.reset(new FlashCache(sts));
      break;
    case FLASHCACHELRUK :
        policy.reset(new FlashCacheLruk(sts));
        break;
    case FLASHCACHELRUKCLK :
        policy.reset(new FlashCacheLrukClk(sts));
        break;
    case FLASHCACHELRUKCLKMACHINELEARNING :
        APP_NUMBER = *std::begin(apps);
        ML_SVM_TH = USER_SVM_TH;
        policy.reset(new FlashCacheLrukClkMachineLearning(sts));
        break;
    case VICTIMCACHE :
        policy.reset(new VictimCache(sts));
        break;
    case LRUK :
        policy.reset(new Lruk(sts));
        break;
    case CLOCK :
        policy.reset(new Clock(sts));
        break;
    case SEGMENT_UTIL:
        policy.reset(new SegmentUtil(sts));
        break;
    case SLAB :
      if (memcachier_classes) {
        sts.gfactor = 2.0;
      } else {
        sts.gfactor = gfactor;
      }
      sts.memcachier_classes = memcachier_classes;
      policy.reset(new slab(sts));
      break;
    case SHADOWSLAB:
      if (memcachier_classes) {
        sts.gfactor = 2.0;
      } else {
        sts.gfactor = gfactor;
      }
      sts.memcachier_classes = memcachier_classes;
      policy.reset(new shadowslab(sts));
      break;
    case PARTSLAB:
      sts.partitions = partitions;
      policy.reset(new partslab(sts));
      break;
    case LSM:
      sts.segment_size = segment_size;
      sts.cleaning_width = cleaning_width;
      policy.reset(new lsm(sts));
      break;
    case MULTI:
      sts.segment_size = segment_size;
      sts.cleaning_width = cleaning_width;
      {
        lsc_multi* multi = new lsc_multi(sts, subpolicy);
        policy.reset(multi);

        for (size_t appid : apps) {
          if (!syn)
            assert(memcachier_app_size[appid] > 0);
          else
            assert(syn_app_size[appid] > 0);
          //uint32_t app_steal_size = 65536;
          uint32_t app_steal_size = CH_cz*OSIZE;
          std::cerr << "steal size (credit): " << app_steal_size/OSIZE << "\n";
          std::cerr << "steal size (MRC gap): " << AET_PGAP << "\n";
          auto it = app_steal_sizes.find(appid);
          if (it != app_steal_sizes.end()) {
            app_steal_size = it->second;
          } 
          if (!syn)
          {
             size_t private_mem = use_percentage ? 
               (size_t)(memcachier_app_size[appid] * 
                       priv_mem_percentage) : min_mem_pct;         
             multi->add_app(appid,
                         private_mem ,
                         memcachier_app_size[appid],
                         app_steal_size,
                         mrc);
          }
          else
          {
             size_t private_mem = use_percentage ? 
               (size_t)(syn_app_size[appid] * 
                       priv_mem_percentage) : min_mem_pct;         
             multi->add_app(appid,
                         private_mem ,
                         syn_app_size[appid],
                         app_steal_size,
                         mrc);
          }
        }

        if (use_tax) {
          multi->set_tax(tax_rate);
        }
      }

      break;
    case MULTISLAB:
    {
      if (memcachier_classes) {
        sts.gfactor = 2.0;
      } else {
        sts.gfactor = gfactor;
      }
      sts.memcachier_classes = memcachier_classes;
      slab_multi* slmulti = new slab_multi(sts);
      policy.reset(slmulti);

      for (size_t appid : apps) {
          if (!syn)
            assert(memcachier_app_size[appid] > 0);
          else
            assert(syn_app_size[appid] > 0);
        slmulti->add_app(appid, min_mem_pct, memcachier_app_size[appid]);
      }
      break;
    }
    case RIPQ:
      sts.block_size = block_size;
      sts.flash_size = flash_size;
      sts.num_sections = num_sections;
      policy.reset(new ripq(sts, block_size, num_sections, flash_size));
      break;
    case RIPQ_SHIELD:
      sts.block_size = block_size;
      sts.flash_size = flash_size;
      sts.dram_size = dram_size;
      sts.num_sections = num_sections;
      sts.num_dsections = num_dsections;
      policy.reset(new ripq_shield(sts, block_size, num_sections,
                                   dram_size, num_dsections, flash_size));
      break;
    case RAMSHIELD:
      sts.threshold = threshold;
      sts.flash_size = flash_size;
      sts.dram_size = dram_size;
      policy.reset(new RamShield(sts, block_size));
      break;
    case RAMSHIELD_FIFO:
      sts.threshold = threshold;
      sts.block_size = block_size;
      sts.flash_size = flash_size;
      sts.dram_size = dram_size;
      policy.reset(new RamShield_fifo(sts, block_size));
      break;
    case RAMSHIELD_SEL:
      sts.threshold = threshold;
      sts.block_size = block_size;
      sts.flash_size = flash_size;
      sts.dram_size = dram_size;
      policy.reset(new RamShield_sel(sts, block_size));
      break;
    case REPLAY:
#ifdef NOREPLAY
      assert("Replay support not compiled in; adjust Makefile." && false);
#else
      policy.reset(new replay(sts));
#endif
      break;
    case FLASHSHIELD:
        FLASH_SHILD_APP_NUMBER = *std::begin(apps);
        FLASH_SHILD_TH = USER_SVM_TH;
        sts.threshold = threshold;
        sts.flash_size = flash_size;
        sts.dram_size = dram_size;
        policy.reset(new flashshield(sts));
        break;
  }

   if (!policy) {
    std::cerr << "No valid policy selected!" << std::endl;
    exit(-1);
  }


  // List input parameters
  std::cerr << "performing trace analysis on apps " << app_str << std::endl
            << "with app sizes " << app_n << std::endl
            << "with noisy neighbor " << noisyN << std::endl
            << "with steal weights of " << app_steal_sizes_str << std::endl
            << "policy " << policy_names[policy_type] << std::endl
            << "using trace file " << trace << std::endl
            << "rounding " << (roundup ? "on" : "off") << std::endl
            << "utilization rate " << lsm_util << std::endl
            << "start counting hits at t = " << hit_start_time << std::endl
            << "request limit " << request_limit << std::endl
            << "global mem " << global_mem << std::endl
            << "use tax " << use_tax << std::endl
            << "tax rate " << tax_rate << std::endl
            << "cleaning width " << cleaning_width << std::endl;
  if (policy_type == SHADOWSLAB) {
    if (memcachier_classes)
      std::cerr << "slab growth factor: memcachier" << std::endl;
    else
      std::cerr << "slab growth factor: " << gfactor << std::endl;
  }

  if (net)
  {
      //net we need to open up a tcp/ip stream in order to accept 
      //GET/SET from mutilate

      int listener;
      unsigned int length;
      int conn;
      struct sockaddr_in s1;

      listener = socket(AF_INET,SOCK_STREAM,0);
      memset( (char*)&s1, 0, sizeof(s1));
      s1.sin_family = (short) AF_INET;
      s1.sin_addr.s_addr = htonl(INADDR_ANY);
      s1.sin_port = htons(0);

      bind(listener, (struct sockaddr*)&s1,sizeof(s1));
      length = sizeof(s1);
      getsockname(listener,(struct sockaddr*)&s1,&length);

      std::cerr << "port: " << ntohs(s1.sin_port) << "\n";

      listen(listener,1);

      struct sockaddr_in s2;
      conn = accept(listener, (struct sockaddr*)&s2,&length);
    

      int i = 0;
      auto start = hrc::now();
      auto last_progress = start;
      size_t bytes = 0;
        
      //all reqs in net are 200 bytes
      const char *data = "Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Aenean commodo ligula eget dolor. Aenean massa. Cum sociis natoque penatibus et magnis dis parturient montes, nascetur ridiculus mus. Donec qu";

      char buf[1024];
      memset(buf,0,1024);
      while ( read(conn,&buf,1024) )
      {

        
        //parse buf
        //time == i
        //appid == first char of key
        //type == SET o GET
        //key_size length of key 9?
        //val_size == 200
        //kid = key

        char line[1024];
        int type = 2;
        if (!strncmp(buf,"get",3) || !strncmp(buf,"GET",3))
        {
                type = 1;
        }

        unsigned int appid = buf[4] - '0';
        char key[25];
        memcpy(key,&buf[4],9);

        long long kid = atol(key);

        sprintf(line,"%d,%u,%d,9,200,%lld",
                     i+1,appid,type,kid);
    
        //std::cerr << "request: " << line << "\n";

        //return a request
        request r{line};


        bytes += strlen(line);

        if (verbose && ((i & ((1 << 18) - 1)) == 0)) {
          auto now  = hrc::now();
          double seconds =
            ch::duration_cast<ch::nanoseconds>(now - last_progress).count() / 1e9;
          if (seconds > 1.0) {
            stats* stats = policy->get_stats();
            std::cerr << "Progress: " << std::setprecision(10) << r.time << " "
                      << "Rate: " << bytes / (1 << 20) / seconds << " MB/s "
                      << "Hit Rate: " << stats->get_hit_rate() * 100 << "% "
                      << "Evicted Items: " << stats->evicted_items << " "
                      << "Evicted Bytes: " << stats->evicted_bytes << " "
                      << "Utilization: " << stats->get_utilization() << " "
                      << std::endl;
            bytes = 0;
            last_progress = now;
          }
        }

        
        const bool in_apps =
          std::find(std::begin(apps), std::end(apps), r.appid) != std::end(apps);

        if (all_apps && !in_apps) {
          apps.insert(r.appid);
        } else if (!in_apps) {
          continue;
        }
        
        
        int status = policy->proc_net(&r, r.time < hit_start_time);
        //status == 2 means get was hit
        //status == 1 means okay (ack after set)
        //status == 0 means get was miss
        const char *end = "END\r\n";
        const char *stored = "STORED\r\n";
        if (status == 2)
        {
            /*
             *  Each item sent by the server looks like this:

                VALUE <key> <flags> <bytes> [<cas unique>]\r\n
                <data block>\r\n
             */
            char get_resp[1024];
            memset(get_resp,0,1024);

            sprintf(get_resp,"VALUE %lu %d %d %d\r\n%s\r\n",
                    r.kid,0,200,i,data);
            
            write(conn,get_resp,strlen(get_resp));
            write(conn,end,strlen(end));
            
        }
        else if (status == 1)
        {
            write(conn,stored,strlen(stored));
        }
        else if (status == 0)
        {
            write(conn,end,strlen(end));
        }

        memset(buf,0,1024);
        ++i;
    }
    auto stop = hrc::now();

    // Log curves for shadowlru, shadowslab, and partslab.
    if (policy_type == 0 || policy_type == 4 || policy_type == 5)
      policy->log_curves();
 
    // Dump stats for all policies. 
    policy->dump_stats();

    double seconds =
      ch::duration_cast<ch::milliseconds>(stop - start).count() / 1000.;
    std::cerr << "total execution time: " << seconds << std::endl;
          
      

    close(listener);
    close(conn);

  }
  else
  {
    //double avg_get_latency = 0;
    //double avg_set_latency = 0;
    // proc file line by line
    std::ifstream t_stream(trace);
    assert(t_stream.is_open());
    std::string line;

    int i = 0;
    auto start = hrc::now();
    auto last_progress = start;
    size_t bytes = 0;
    
    size_t time_hour = 1; 
    while (std::getline(t_stream, line) &&
           (request_limit == 0 || i < request_limit))
    {
      request r{line};


      bytes += line.size();

      if (verbose && ((i & ((1 << 18) - 1)) == 0)) {
        auto now  = hrc::now();
        double seconds =
          ch::duration_cast<ch::nanoseconds>(now - last_progress).count() / 1e9;
        if (seconds > 1.0) {
          stats* stats = policy->get_stats();
          std::cerr << "Progress: " << std::setprecision(10) << r.time << " "
                    << "Rate: " << bytes / (1 << 20) / seconds << " MB/s "
                    << "Hit Rate: " << stats->get_hit_rate() * 100 << "% "
                    << "Evicted Items: " << stats->evicted_items << " "
                    << "Evicted Bytes: " << stats->evicted_bytes << " "
                    << "Utilization: " << stats->get_utilization() << " "
                    //<< "Avg GET: " << avg_get_latency/stats->accesses << " "
                    //<< "Avg SET: " << avg_set_latency/stats->accesses << " "
                    << std::endl;
          bytes = 0;
          last_progress = now;
        }
      }

      // Only process requests for specified app, of type GET,
      // and values of size > 0, after time 'hit_start_time'.
      if (r.type != request::GET)
        continue;


      if (r.val_sz <= 0)
        continue;
      
      const bool in_apps =
        std::find(std::begin(apps), std::end(apps), r.appid) != std::end(apps);

      if (all_apps && !in_apps) {
        apps.insert(r.appid);
      } else if (!in_apps) {
        continue;
      }
      

      //clock_t start, end;
      //double cpu_time_used;
      //measure how long it takes
      //start = clock();
      //int status = 
      policy->proc(&r, r.time < hit_start_time);
      //end = clock();

      //cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;

      //get was hit
      //if (status == 1)
      //{
      //    avg_get_latency += cpu_time_used;
      //}
      ////we did a set
      //else
      //{
      //    avg_set_latency += cpu_time_used;
      //}

      if (verbose 
          && ( policy_type == FLASHSHIELD || policy_type == VICTIMCACHE || policy_type == RIPQ ) 
          && time_hour * 3600 < r.time) {
      	printf ("Dumping stats for FLASHSHIELD\n");
          policy->dump_stats();
          time_hour++;
      }
      ++i;
    }
    auto stop = hrc::now();

    // Log curves for shadowlru, shadowslab, and partslab.
    if (policy_type == 0 || policy_type == 4 || policy_type == 5)
      policy->log_curves();
 
    // Dump stats for all policies. 
    policy->dump_stats();

    double seconds =
      ch::duration_cast<ch::milliseconds>(stop - start).count() / 1000.;
    std::cerr << "total execution time: " << seconds << std::endl;

    return 0;
  }
}

