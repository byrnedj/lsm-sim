#include <iostream>
#include <fstream>
#include <cassert>
#include <unordered_set>
#include <algorithm>

#include "lsc_multi.h"

lsc_multi::application::application(
    size_t appid,
    size_t min_mem_pct,
    size_t target_mem,
    size_t steal_size,
    bool ua_mrc )
  : appid{appid}
  , min_mem_pct{min_mem_pct}
  , target_mem{target_mem}
  , min_mem{size_t(target_mem * (min_mem_pct / 100.))}
  , steal_size{steal_size}
  , credit_bytes{}
  , bytes_in_use{}
  , live_items{}
  , accesses{}
  , lastmrc{}
  , hits{}
  , misses{}
  , w_accesses{}
  , w_hits{}
  , shadow_q_hits{}
  , survivor_items{}
  , survivor_bytes{}
  , evicted_items{}
  , evicted_bytes{}
  , u_mrc{ua_mrc}
  , AET{}
  , shadow_q{}
  , cleaning_q{}
  , cleaning_it{cleaning_q.end()}
{
  shadow_q.expand(10 * 1024 * 1024);
}

lsc_multi::application::~application() {}

lsc_multi::lsc_multi(stats stat, subpolicy eviction_policy)
  : policy{stat}
  , last_idle_check{0.}
  , last_dump{0.}
  , use_tax{}
  , tax_rate{}
  , eviction_policy{eviction_policy}
  , cleaner{eviction_policy == subpolicy::GREEDY ? cleaning_policy::RANDOM
                                                 : cleaning_policy::LOW_NEED}
  //, cleaner{cleaning_policy::LOW_NEED}
  , apps{}
  , map{}
  , head{nullptr}
  , segments{}
  , free_segments{}
{
  srand(0);

  if (stat.global_mem % stat.segment_size != 0) {
    std::cerr <<
      "WARNING: global_mem not a multiple of segment_size" << std::endl;
  }
  segments.resize(stat.global_mem / stat.segment_size);
  free_segments = segments.size();
  std::cerr << "global_mem " << stat.global_mem
            << " segment_size " << stat.segment_size
            << " segment_count " << segments.size() << std::endl;

  // Check for enough segments to sustain cleaning width and head segment.
  std::cerr << segments.size() << " > 2*" << stat.cleaning_width << "\n";
  assert(segments.size() > 2 * stat.cleaning_width);

  // Sets up head.
  rollover(0.);

}

lsc_multi::~lsc_multi() {}

void lsc_multi::add_app(size_t appid,
                        size_t min_mem_pct,
                        size_t target_memory,
                        size_t steal_size,
                        bool u_mrc)
{
  assert(head->filled_bytes == 0);
  assert(apps.find(appid) == apps.end());
  apps.emplace(appid, application{appid, min_mem_pct, target_memory, steal_size,u_mrc});
}

void lsc_multi::dump_app_stats(double time) {
  for (auto& p : apps) {
    application& app = p.second;
    app.dump_stats(time, eviction_policy);
  }
}

void lsc_multi::compute_idle_mem(double time) {
  std::unordered_map<int32_t, size_t> per_app_in_use{};
  std::unordered_map<int32_t, size_t> per_app_idle{};
  for (auto& segment : segments) {
    if (!segment)
      continue;
    for (item& item : segment->queue) {
      if (time - item.req.time > idle_mem_secs)
        per_app_idle[item.req.appid] += item.req.size();
      per_app_in_use[item.req.appid] += item.req.size();
    }
  }

  //std::cout << time << std::endl;
  for (const auto& pr : per_app_in_use) {
    const int32_t appid = pr.first;
    //const size_t total = pr.second;
    const size_t idle = per_app_idle[appid];
    auto it = apps.find(appid);
    assert(it != apps.end());
    application& app = it->second;

    const double active_fraction = 1 - (double(idle) / app.bytes_in_use);
    const double tax = (1 - active_fraction * tax_rate) / (1 - tax_rate);

    const ssize_t new_target = app.min_mem / tax;

    //const size_t old_target = app.target_mem + app.credit_bytes;

    app.credit_bytes = (new_target - app.target_mem);

    /*
    std::cout << "app " << pr.first
              << " idle " << idle << "/" << total
              << " active_fraction " << active_fraction
              << " new_share " << new_target
              << " (" << app.target_mem + app.credit_bytes << ")"
              << " old_share " << old_target
              << std::endl;
    */
  }
  //std::cout << std::endl;
}

size_t lsc_multi::proc(const request *r, bool warmup) {
  assert(r->size() > 0);
  if (r->size() > int32_t(stat.segment_size)) {
    std::cerr << "Can't process large request of size " << r->size()
              << std::endl;
    return 1;
  }

  assert(apps.find(r->appid) != apps.end());

  if (!warmup && use_tax &&
      ((last_idle_check == 0.) ||
      (r->time - last_idle_check > idle_mem_secs)))
  {
    compute_idle_mem(r->time);
    if (last_idle_check == 0.)
      last_idle_check = r->time;
    last_idle_check += idle_mem_secs;
  }

  if (!warmup && ((last_dump == 0.) || (r->time - last_dump > 100000.))) {
    if (last_dump == 0.)
      application::dump_stats_header();
    dump_app_stats(r->time);
    if (last_dump == 0.)
      last_dump = r->time;
    last_dump += 100000.0;
  }

  auto appit = apps.find(r->appid);
  assert(appit != apps.end());
  application& app = appit->second;

  if (!warmup) {
    ++stat.accesses;
    ++app.accesses;
    ++app.w_accesses;
  }

  //if we are doing mrc, the take a sample
  if (app.u_mrc)
      app.AET.sample(r->kid);

  auto it = map.find(r->kid);
  if (it != map.end()) {
    auto list_it = it->second;
    segment* old_segment = list_it->seg;
    int32_t old_request_size = list_it->req.size();
    if (old_request_size  == r->size()) {
      // Promote this item to the front.
      old_segment->queue.erase(list_it);
      old_segment->queue.emplace_front(old_segment, *r);
      map[r->kid] = old_segment->queue.begin();

      ++old_segment->access_count;

      if (!warmup) {
        ++stat.hits;
        ++app.w_hits;
        ++app.hits;
      }

      return 1;
    } else {
      // If the size changed, then we have to put it in head. Just
      // falling through to the code below handles that. We still
      // count this record as a single hit, even though some miss
      // would have had to have happened to shootdown the old stale
      // sized value. This is to be fair to other policies like gLRU
      // that don't detect these size changes.
      //if (!warmup) {
      //  ++stat.hits;
      //  ++app.w_hits;
      //  ++app.hits;
      //}
    }
  } else {
    // If miss in hash table, then count a miss. This get will count
    // as a second access that hits below.
    if (!warmup) {
      ++app.misses;
    }
  }

  if (head->filled_bytes + r->size() > stat.segment_size)
    rollover(r->time);
    //assert(head->filled_bytes + r->size() <= stat.segment_size);

  // Add the new request.
  head->queue.emplace_front(head, *r);
  map[r->kid] = head->queue.begin();
  head->filled_bytes += r->size();
  auto ait = head->app_bytes.find(app.appid);
  if (ait == head->app_bytes.end()) {
    head->app_bytes.emplace(app.appid, r->size());
  } else {
    ait->second = ait->second + r->size();
  }

  ++head->access_count;

  // Bill the correct app for its use of log space.
  app.bytes_in_use += r->size();
  app.live_items += 1;

  std::vector<size_t> appids{};
  for (size_t appid : *stat.apps)
    appids.push_back(appid);


  if (!app.u_mrc)
  {

    // Check to see if the access would have hit in the app's shadow Q.
    if (app.would_hit(r)) {
      ++app.shadow_q_hits;
      // Hit in shadow Q! We get to steal!
      size_t steal_size = 0;
      if (!use_tax && eviction_policy == subpolicy::NORMAL)
          steal_size = app.steal_size ;
      for (int i = 0; i < 10; ++i) {
        size_t victim = appids.at(rand() % appids.size());
        auto it = apps.find(victim);
        assert(it != apps.end());
        application& other_app = it->second;
        if (app.try_steal_from(other_app, steal_size))
        {
          
          //std::cerr << app.appid << " stole from " 
          //          << other_app.appid << "\n";
          break;
        }
      }
      // May not have been able to steal, but oh well.
    }
  }
  else
  {
      if (  1 )
      {
        if ( app.would_hit(r) )
            ++app.shadow_q_hits;

        if (app.accesses-app.lastmrc >= 1000000  && 
            !warmup  && app.evicted_items > 0)
        {
            for (int i = 0; i < 10; ++i) {
              size_t victim = appids.at(rand() % appids.size());
              auto it = apps.find(victim);
              assert(it != apps.end());
              application& other_app = it->second;

              if (other_app.appid == app.appid)
                  continue;
              //1. solve mrc for both apps
              //2. steal size is in bytes, so n objects
              //   is steal size/OSIZE
              //3.1 do current app mrc(current_items) = cmr1
              //    do steal app mrc(current_items) = cmr2
              //
              //3.2 do current app mrc(steal_size+current) mr1
              //      steal app mrc(steal_size-current) = mr2
              //   if ((mr1 + mr2)/2) > ((cmr1 + cmr2)/2))
              //        => WE GET TO STEAL!
              //4. don't forget to free cache_size_index and miss
              //   rate arrays
              
              long long *app_cache_size_index;
              long long app_max;
              double *app_miss_rate;
              double appN = app.AET.getN();

              //std::cerr << app.appid << " N " << appN << "\n";

              app_cache_size_index = (long long*)malloc(
                                         sizeof(long long)*(int)appN);
              app_miss_rate = (double*)malloc(sizeof(double)*(int)appN);

              app.AET.solve(app_cache_size_index,
                            app_miss_rate,
                            &app_max);
    
              //make sure we got a result
              if (!app_cache_size_index || !app_miss_rate )
              {

                  std::cerr << app.appid << " no go mrc\n ";
                  break;
              }
                
              //std::cerr << app.appid << "  got mrc\n ";
              

              long long *other_cache_size_index;
              double *other_miss_rate;
              long long other_max;
              double otherN = other_app.AET.getN();

              //std::cerr << other_app.appid << " N " << otherN << "\n";

              
              other_cache_size_index = (long long*)malloc(
                                         sizeof(long long)*(int)otherN);
              other_miss_rate = (double*)malloc(sizeof(double)*(int)otherN);
              
              other_app.AET.solve(other_cache_size_index,
                                   other_miss_rate,
                                   &other_max);
              
              //make sure we got a result
              if (!other_cache_size_index || !other_miss_rate )
                  break;

              
              //get current cache size with allocated memory
              size_t app_alloc = (app.target_mem + app.credit_bytes)/OSIZE;
              size_t app_c_size = (app_alloc/PGAP)*PGAP;
              
              size_t other_alloc = (other_app.target_mem + 
                                    other_app.credit_bytes) /OSIZE;
              size_t other_c_size = (other_alloc/PGAP)*PGAP;


              //need to find minimum between the two
              //MRCs
              //1. make sure MRCs are equal sized. need 
              //   to extend their "tails"
              //   the max it could be is total memory.
              //2. calculate reserved
              //3. calculate "max adjust" for shadow queue
              //4. lower bound = max(actual-max_adjust,reserved)
              //5. upper bound = min(actual+max_adjust,total_memory)

              int n = 0;
              while (app_cache_size_index[n] != app_max) n++;
              double app_max_mr = app_miss_rate[n];
              n++;
              
              int m = 0;
              while (other_cache_size_index[m] != other_max) m++;
              double other_max_mr = other_miss_rate[m];
              m++;

              long long max_objects = ((stat.global_mem/OSIZE)/PGAP)*PGAP;
              long long app_add = (max_objects - app_c_size)/PGAP;
              long long other_add = (max_objects - other_c_size)/PGAP;

              //grow our mrcs by add amount
              long long *t_app_idx = (long long*) realloc(app_cache_size_index,
                                                          sizeof(long long) * 
                                                          (int)(app_add + appN));
              double *t_app_mrc = (double*)realloc(app_miss_rate,
                                                   sizeof(double) * 
                                                   (int)(app_add + appN));

              if (t_app_idx && t_app_mrc)
              {
                  app_cache_size_index = t_app_idx;
                  app_miss_rate = t_app_mrc;
              }
              else
              {
                  break;
              }
              
              long long *t_other_idx = (long long*) realloc(other_cache_size_index,
                                                            sizeof(long long) * 
                                                            (int)(other_add + otherN));
              double *t_other_mrc = (double*)realloc(other_miss_rate,
                                                     sizeof(double) * 
                                                     (int)(other_add + otherN));

              if (t_other_idx && t_other_mrc)
              {
                  other_cache_size_index = t_other_idx;
                  other_miss_rate = t_other_mrc;
              }
              else
              {
                  break;
              }

              //from n (or m) to max_objects, set the miss rate to
              //max_mr
              long long i_size = app_max + PGAP;
              while (i_size < max_objects) 
              {
                  app_cache_size_index[n] = i_size;
                  app_miss_rate[n] = app_max_mr;
                  i_size += PGAP;
                  n++;
              }
              
              i_size = other_max + PGAP;
              while (i_size < max_objects) 
              {
                  other_cache_size_index[m] = i_size;
                  other_miss_rate[m] = other_max_mr;
                  i_size += PGAP;
                  m++;
              }


              //2. reserverd
              double app_pct = (double)app.min_mem_pct;
              app_pct = app_pct/100;
              size_t app_reserved = (app.target_mem/OSIZE) * app_pct;
              
              double other_pct = (double)other_app.min_mem_pct;
              other_pct = other_pct/100;
              size_t other_reserved = (other_app.target_mem/OSIZE) * other_pct;

              //3. "max adjust"
              // shadow queue size is 10MB = 10000000 bytes 
              //                           = OSIZE0000000 obj
              // hmmm lets just skip this for now 

              //4. lower bound = reserved
              size_t app_lower = app_reserved;
              size_t other_lower = other_reserved;

              //5. upper bound = total_memory
              size_t app_upper = max_objects;
              size_t other_upper = max_objects;

              if (1)
              {
                long long app_m;
                long long other_m;
                double min1;
                double min2;
                app.AET.balance(app_cache_size_index, app_miss_rate, 
                                app_c_size, app_upper,
                                app_lower, 
                                other_cache_size_index, other_miss_rate,
                                other_c_size, other_upper,
                                other_lower,
                                &app_m, &other_m, &min1, &min2);
              
                std::cerr << "app " << app.appid << ", other app " << other_app.appid << "\n";
                std::cerr << "app size " << app_c_size << ", other app size " << other_c_size << "\n";
                if (min2 < min1)
                {
                    std::cerr << "min2 < min1" << "\n";
                    size_t b_steal = app_c_size - app_m;
                    std::cerr << other_app.appid << " steal " <<  b_steal
                              << " from " << app.appid << "\n";

                    if (other_app.try_steal_from(app, b_steal*OSIZE))
                    {
                       
                       std::cerr << other_app.appid << " stole from " 
                                 << app.appid << "\n";
                       break;
                    }

                }
                else
                {

                    size_t b_steal = other_c_size - other_m;
                    std::cerr << app.appid << " steal " <<  b_steal
                              << " from " << other_app.appid << "\n";

                    if (app.try_steal_from(other_app, b_steal*OSIZE))
                    {
                       
                       std::cerr << app.appid << " stole from " 
                                 << other_app.appid << "\n";
                       break;
                    }
                }


              }
              else
              {

                if (app_c_size > (size_t)app_max)
                    app_c_size = (size_t)app_max;
                if (other_c_size > (size_t)other_max)
                    other_c_size = (size_t)other_max;


                int j = 0;
                while (app_cache_size_index[j] != (long long)app_c_size) 
                {
                    //reached the end of mrc ,,not much we can
                    //do
                    if (app_cache_size_index[j] == app_max)
                        break;
                    j++;
                }
                int app_c_index = j;
                
                j = 0;
                while (other_cache_size_index[j] != (long long)other_c_size) 
                {
                    if (other_cache_size_index[j] == other_max)
                        break;
                    j++;
                }
                int other_c_index = j;

                double app_curr_miss_rate = app_miss_rate[
                                                        app_c_index];
                double other_curr_miss_rate = other_miss_rate[
                                                        other_c_index];
            
                double avg_curr = (app_curr_miss_rate + 
                                   other_curr_miss_rate)/2;


                
                //make sure we actually have more space to allocate...
                if (app_c_size + 100*PGAP > (size_t)app_max)
                    break;
                if (other_c_size > (size_t) other_max)
                    break;
                

                double app_s_miss_rate = app_miss_rate[
                                                    app_c_index+100];
                double other_s_miss_rate = other_miss_rate[
                                                    other_c_index-100];
                
                double avg_s = (app_s_miss_rate + other_s_miss_rate)/2;

                std::cerr << app.appid 
                          << " miss rate " << app_curr_miss_rate
                          << " size " << app_c_size 
                          << " s miss rate " << app_s_miss_rate
                          << " s size " << (app_c_size + 100*PGAP)
                          << " max size " << app_max << "\n";
                std::cerr << other_app.appid 
                          << " miss rate " << other_curr_miss_rate
                          << " size " << other_c_size 
                          << " s miss rate " << other_s_miss_rate
                          << " s size " << (other_c_size - 100*PGAP)
                          << " max size " << other_max << "\n";

                free(other_cache_size_index);
                free(other_miss_rate);
                free(app_cache_size_index);
                free(app_miss_rate);

                if (avg_s < avg_curr)
                {
                    std::cerr << app.appid << " try steal from " 
                                 << other_app.appid << "\n";
                    if (app.try_steal_from(other_app, 100*PGAP*OSIZE))
                    {
                       
                       std::cerr << app.appid << " stole from " 
                                 << other_app.appid << "\n";
                       break;
                    }
                }

              }


            }
            app.lastmrc = app.accesses;
        }
      }

  }
 
  return PROC_MISS;
}

size_t lsc_multi::get_bytes_cached() const
{
  return 0;
}

void lsc_multi::dump_util(const std::string& filename) {}

void lsc_multi::rollover(double timestamp)
{
  bool rolled = false;
  for (auto& segment : segments) {
    if (segment)
      continue;

    segment.emplace();
    --free_segments;
    head = &segment.value();
    head->low_timestamp = timestamp;
    rolled = true;
    break;
  }
  assert(rolled);

  if (free_segments < stat.cleaning_width)
    clean();
}

void lsc_multi::dump_usage()
{
  std::cerr << "[";
  for (auto& segment : segments) 
    std::cerr << (bool(segment) ? "X" : "_");
  std::cerr << "]" << std::endl;
}

double lsc_multi::get_running_hit_rate() {
  return double(stat.hits) / stat.accesses;
}


auto lsc_multi::choose_cleaning_sources() -> std::vector<segment*>
{
  switch (cleaner) {
    case cleaning_policy::OLDEST_ITEM:
      return choose_cleaning_sources_oldest_item();
    case cleaning_policy::ROUND_ROBIN:
      return choose_cleaning_sources_round_robin();
    case cleaning_policy::RUMBLE:
      return choose_cleaning_sources_rumble();
    case cleaning_policy::LOW_NEED:
      return choose_cleaning_sources_low_need();
    case cleaning_policy::RANDOM:
    default:
      return choose_cleaning_sources_random();
  }
}

auto lsc_multi::choose_cleaning_sources_random() -> std::vector<segment*>
{
  std::vector<segment*> srcs{};

  for (size_t i = 0; i < stat.cleaning_width; ++i) {
    for (size_t j = 0; j < 1000000; ++j) {
      int r = rand() % segments.size();
      auto& segment = segments.at(r);

      // Don't pick free segments.
      if (!segment)
        continue;

      // Don't pick the head segment.
      if (&segment.value() == head)
        continue;

      // Don't pick any segment we've already picked!
      bool already_picked = false;
      for (auto& already_in : srcs)
        already_picked |= (already_in == &segment.value());
      if (already_picked)
        continue;

      srcs.emplace_back(&segment.value());
      break;
    }
    // Check for enough in use segments during cleaning; if not then a bug!
    assert(srcs.size() == i + 1);
  }

  return srcs;
}

auto lsc_multi::choose_cleaning_sources_oldest_item() -> std::vector<segment*>
{
  std::vector<segment*> srcs{};

  for (auto& segment : segments) {
    // Don't pick free segments.
    if (!segment)
      continue;

    // Don't pick the head segment.
    if (&segment.value() == head)
      continue;

    srcs.emplace_back(&segment.value());
  }

  std::sort(srcs.begin(), srcs.end(),
      [](const segment* left, const segment* right) {
        return left->low_timestamp < right->low_timestamp;
      });
  //std::cout << "Sorted list" << std::endl;
  //for (segment* src : srcs)
    //std::cout << src->low_timestamp << std::endl;
  srcs.resize(stat.cleaning_width);
  //std::cout << "Selected list" << std::endl;
  //for (segment* src : srcs)
    //std::cout << src->low_timestamp << std::endl;
  //std::cout << "==============" << std::endl;

  for (auto& segment : segments) {
    // Don't pick free segments.
    if (!segment)
      continue;
    segment->access_count = 0;
  }

  // Check for enough in use segments during cleaning; if not then a bug!
  assert(srcs.size() == stat.cleaning_width);

  return srcs;
}

auto lsc_multi::choose_cleaning_sources_rumble() -> std::vector<segment*>
{
  std::vector<segment*> srcs{};

  for (auto& segment : segments) {
    // Don't pick free segments.
    if (!segment)
      continue;

    // Don't pick the head segment.
    if (&segment.value() == head)
      continue;

    srcs.emplace_back(&segment.value());
  }

  std::sort(srcs.begin(), srcs.end(),
      [](const segment* left, const segment* right) {
        return left->access_count < right->access_count;
      });
  //std::cout << "Sorted list" << std::endl;
  //for (segment* src : srcs)
    //std::cout << src->access_count << std::endl;
  srcs.resize(stat.cleaning_width);
  //std::cout << "Selected list" << std::endl;
  //for (segment* src : srcs)
    //std::cout << src->access_count << std::endl;
  //std::cout << "==============" << std::endl;

  for (auto& segment : segments) {
    // Don't pick free segments.
    if (!segment)
      continue;
    segment->access_count = 0;
  }

  // Check for enough in use segments during cleaning; if not then a bug!
  assert(srcs.size() == stat.cleaning_width);

  return srcs;
}

auto lsc_multi::choose_cleaning_sources_round_robin() -> std::vector<segment*>
{
  static size_t next = 0;

  std::vector<segment*> srcs{};

  while (srcs.size() < stat.cleaning_width) {
    auto& segment = segments.at(next);

    if (!segment)
      continue;

    // Don't pick the head segment.
    if (&segment.value() == head)
      continue;

    srcs.emplace_back(&segment.value());

    ++next;
    next %= segments.size();
  }

  return srcs;
}

auto lsc_multi::choose_cleaning_sources_low_need() -> std::vector<segment*>
{
  application* low_need_app = nullptr;

  // Choose app most in need.
  // Those at/below min_mem get priority.
  for (auto& p : apps) {
    application& app = p.second;
    if (app.cleaning_it == app.cleaning_q.end())
      continue;
    if (low_need_app == nullptr || app.need() < low_need_app->need())
      low_need_app = &app;
  }

  std::vector<std::pair<size_t, segment*>> candidates{};
  for (auto& seg : segments) {
    if (!seg)
      continue;

    // Don't pick the head segment.
    if (&seg.value() == head)
      continue;

    size_t bytes = 0;
    auto it = seg->app_bytes.find(low_need_app->appid);
    if (it != seg->app_bytes.end())
      bytes = it->second;

    candidates.emplace_back(std::make_pair(bytes, &seg.value()));
  }

  std::sort(candidates.begin(), candidates.end());

  const size_t to_get_low_need = stat.cleaning_width / 2;

  std::vector<segment*> r{};
  auto rit = candidates.rbegin();
  for (size_t i = 0; i < to_get_low_need; ++i) {
    if (rit == candidates.rend())
      break;
    r.emplace_back(rit->second);
    ++rit;
  }

  for (size_t i = to_get_low_need; i < stat.cleaning_width; ++i) {
    for (size_t j = 0; j < 1000000; ++j) {
      int rd = rand() % segments.size();
      auto& segment = segments.at(rd);

      // Don't pick free segments.
      if (!segment)
        continue;

      // Don't pick the head segment.
      if (&segment.value() == head)
        continue;

      // Don't pick any segment we've already picked!
      bool already_picked = false;
      for (auto& already_in : r)
        already_picked |= (already_in == &segment.value());
      if (already_picked)
        continue;

      r.emplace_back(&segment.value());
      break;
    }
  }

  assert(r.size() == stat.cleaning_width);

  return r;
}

auto lsc_multi::choose_cleaning_destination() -> segment* {
  for (auto& segment : segments) {
    // Don't pick a used segment. (This also guarantees we don't pick
    // the same free segment more than once.)
    if (segment)
      continue;

    // Construct, clean empty segment here.
    segment.emplace();
    --free_segments;

    return &segment.value();
  }

  assert(false);
}
 
void lsc_multi::dump_cleaning_plan(std::vector<segment*> srcs,
                             std::vector<segment*> dsts)
{
  std::cerr << "Cleaning Plan [";
  for (auto& segment : segments) {
    if (segment) {
      for (auto& src : srcs) {
        if (src == &segment.value()) {
          std::cerr << "S";
          goto next;
        }
      }
      for (auto& dst: dsts) {
        if (dst == &segment.value()) {
          std::cerr << "D";
          goto next;
        }
      }
      std::cerr << "X";
      goto next;
    }
    std::cerr << "-";
next:
    continue;
  }
  std::cerr << "]" << std::endl;

  size_t seg_num = 0;
  for (auto& segment : srcs) {
    std::cerr << "Source Segment " << seg_num++ << " ";

    std::unordered_map<int32_t, size_t> per_app_in_use{};
    assert(segment);
    for (item& item : segment->queue)
      per_app_in_use[item.req.appid] += item.req.size();
    for (auto& pr : per_app_in_use) {
      int32_t appid = pr.first;
      size_t bytes_in_srcs = pr.second;
      std::cerr << "App " << appid << " " << bytes_in_srcs << " ";
    }
    std::cerr << std::endl;
  }
} 

auto lsc_multi::choose_survivor() -> application* {
  application* r = nullptr;

  if (eviction_policy == subpolicy::NORMAL) {
    // Choose app most in need.
    // Those at/below min_mem get priority.
    for (auto& p : apps) {
      application& app = p.second;
      if (app.cleaning_it == app.cleaning_q.end())
        continue;
      if (app.target_mem + app.credit_bytes > app.min_mem)
        continue;
      if (r == nullptr || app.need() > r->need())
        r = &app;
    }
    if (r)
      return r;
    // Then apps above min_mem, if needed.
    for (auto& p : apps) {
      application& app = p.second;
      if (app.cleaning_it == app.cleaning_q.end())
        continue;
      if (r == nullptr || app.need() > r->need())
        r = &app;
    }
    return r;
  } else if (eviction_policy == subpolicy::GREEDY) {
    // Choose app with newest uncleaned item.
    double newest = 0.;
    for (auto& p : apps) {
      application& app = p.second;
      if (app.cleaning_it == app.cleaning_q.end())
        continue;
      if (r == nullptr || (*app.cleaning_it)->req.time > newest) {
        newest = (*app.cleaning_it)->req.time;
        r = &app;
      }
    }
  } else if (eviction_policy == subpolicy::STATIC) {
    for (auto& p : apps) {
      application& app = p.second;
      if (app.cleaning_it == app.cleaning_q.end())
        continue;
      if (r == nullptr || app.need() > r->need())
        r = &app;
    }
    return r;
  } else {
    assert(false);
  }

  return r;
}

void lsc_multi::clean()
{
  /*
  const char* spinner = "|/-\\";
  static uint8_t last = 0;
  std::cerr << spinner[last++ & 0x03] << '\r';
  */

  const bool debug = false;
  if (debug) {
    std::cerr << "Before cleaning need ";
    for (auto& pr : apps) {
      int32_t appid = pr.first;
      auto& app = pr.second;
      std::cerr << "App " << appid << " " << app.need() << " ";
    }
    std::cerr << std::endl;

    // Check to see if we counting per-app memory correctly.
    std::unordered_map<int32_t, size_t> per_app_in_use{};
    for (auto& segment : segments) {
      if (!segment)
        continue;
      for (item& item : segment->queue)
        per_app_in_use[item.req.appid] += item.req.size();
    }
    for (auto& pr : per_app_in_use) {
      int32_t appid = pr.first;
      size_t discovered_in_use = pr.second;
      auto appit = apps.find(appid);
      assert(appit != apps.end());
      std::cerr << "discovered in use: " << discovered_in_use << " app "
                << appid << " bytes in use: " << appit->second.bytes_in_use << std::endl;
      //assert(discovered_in_use == appit->second.bytes_in_use);
    }
  }

  std::vector<segment*> src_segments = choose_cleaning_sources();

  // Push all items into the app-specific cleaning queues.
  for (segment* segment : src_segments) {
    if (segment->filled_bytes == 0) {
      std::cerr << "Weird segment to clean" << std::endl;
    }
    assert(segment->filled_bytes == 0 || !segment->queue.empty());
    for (auto& item : segment->queue) {
      auto appit = apps.find(item.req.appid);
      if (appit == apps.end()) {
        std::cerr << "Couldn't find " << item.req.appid << " in apps" << std::endl;
        item.req.dump();
      }
      assert(appit != apps.end());
      application& app = appit->second;

      // Remove all objects in the segments to clean from the apps
      // memory budget. Will get re-added for each object that gets
      // 'salvaged'.
      app.bytes_in_use -= item.req.size();
      app.live_items -= 1;

      // Check to see if this version is still needed.
      auto it = map.find(item.req.kid);
      // If not in the hash table, just drop it.
      if (it == map.end())
        continue;
      // If hash table pointer refers to a different version, drop this one.
      if (&*it->second != &item)
        continue;

      app.add_to_cleaning_queue(&item);
    }
  }

  if (debug) {
    std::cerr << "After discounting srcs ";
    for (auto& pr : apps) {
      int32_t appid = pr.first;
      auto& app = pr.second;
      std::cerr << "App " << appid << " " << app.need() << " ";
    }
    std::cerr << std::endl;
  }

  // Sort each app's queue and reset its cleaning
  // iterator.
  for (auto& p : apps) {
    application& app = p.second;
    app.sort_cleaning_queue();
  }

  segment* dst = nullptr;
  size_t dst_index = 0;
  std::vector<segment*> dst_segments{};

  while (true) {
    application* selected_app = choose_survivor();

    // All done with all its.
    if (!selected_app) {
      stat.cleaned_ext_frag_bytes += (stat.segment_size - dst->filled_bytes);
      ++stat.cleaned_generated_segs;

      break;
    }

    item* item = *selected_app->cleaning_it;

    auto it = map.find(item->req.kid);

    // Should exist else we would have thrown it out in the first
    // pass constructing the per-app lists.
    if (it == map.end()) {
      std::cerr << "Found dead object during cleaning" << std::endl;
      item->req.dump();
    }
    assert(it != map.end());
    // If hash table pointer refers to a different version, drop this one.
    if (&*it->second != item) {
      ++selected_app->cleaning_it;
      continue;
    }

    // Check to see if there is room for the item in the dst.
    if (!dst || dst->filled_bytes + item->req.size() > stat.segment_size) {
      //std::cerr << "Couldn't put in item of size "
                //<< item->req.size() << " bytes" << std::endl;
      if (dst)
        stat.cleaned_ext_frag_bytes += (stat.segment_size - dst->filled_bytes);
      ++stat.cleaned_generated_segs;

      // Break out of relocation if we are out of free space our dst segs.
      if (dst_index == stat.cleaning_width - 1)
        break;
      ++dst_index;

      // Rollover to new dst.
      dst = choose_cleaning_destination();
      dst_segments.push_back(dst);
    }
    if (dst->filled_bytes + item->req.size() > stat.segment_size) {
      std::cerr << "Weird object while cleaning!" << std::endl
                << "filled_bytes " << dst->filled_bytes << std::endl
                << "item size " << item->req.size() << std::endl
                << "segment_size " << stat.segment_size << std::endl;
      item->req.dump();
    }
    assert(dst->filled_bytes + item->req.size() <= stat.segment_size);
    ++selected_app->cleaning_it;

    // Relocate *to the back* to retain timestamp sort order.
    dst->queue.emplace_back(dst, item->req);
    map[item->req.kid] = (--dst->queue.end());
    dst->filled_bytes += item->req.size();
    auto ait = dst->app_bytes.find(item->req.appid);
    if (ait == dst->app_bytes.end()) {
      dst->app_bytes.emplace(item->req.appid, item->req.size());
    } else {
      ait->second = ait->second + item->req.size();
    }
    dst->low_timestamp = item->req.time;

    // Bill the app that hosts it for the space use for preserving this.
    selected_app->bytes_in_use += item->req.size();
    selected_app->live_items += 1;
    ++selected_app->survivor_items;
    selected_app->survivor_bytes += item->req.size();
  }


  //size_t test_kid;
  // Clear items that are going to get thrown on the floor from the hashtable.
  // And push them into the app's shadow queue.
  for (auto& p : apps) {
    application& app = p.second;
    while (app.cleaning_it != app.cleaning_q.end()) {
    
        
      // Need to do a double check here. The item we are evicting may
      // still exist in the hash table but it may point to a newer version
      // of this object. In that case, skip the erase from the hash table.
      auto hash_it = map.find((*app.cleaning_it)->req.kid);
      if (hash_it != map.end()) {
        item* from_list = *app.cleaning_it;
        item* from_hash = &*hash_it->second;

        //DJB: CHECK IF THIS IS TRUE? IT IS.
        if (from_list == from_hash) {
          app.shadow_q.remove(&from_list->req);
          app.shadow_q.try_add_tail(&from_list->req);
          //test_kid = (*app.cleaning_it)->req.kid;
          //std::cerr << "evicted " << test_kid << "\n";

          map.erase((*app.cleaning_it)->req.kid);
          ++stat.evicted_items;
          stat.evicted_bytes += from_list->req.size();
          ++app.evicted_items;
          app.evicted_bytes += from_list->req.size();
        }
      }

      ++app.cleaning_it;
    }
    assert(app.cleaning_it == app.cleaning_q.end());
    app.cleaning_q.clear();
  }

  //std::cerr << "test kid evicted " << test_kid << "\n";

  //std::cerr << "look up test kid evicted " << map[test_kid]->req.kid << "\n";

  if (debug) {
    // Sanity check - none of the items left in the hash table should point
    // into a src_segment.
    for (auto& entry : map) {
      item& item = *entry.second;
      for (segment* src : src_segments)
        assert(item.seg != src);
    }

    dump_cleaning_plan(src_segments, dst_segments);
  }

  // Reset each src segment as free for reuse.
  for (auto* src : src_segments) {
    for (auto& segment : segments) {
      if (src == &*segment) {
        segment = nullopt;
        ++free_segments;
      }
    }
  }

  if (debug) {
    // Sanity check - none of the segments should contain more data than their
    // rated capacity.
    size_t stored_in_whole_cache = 0;
    for (auto& segment : segments) {
      if (!segment)
        continue;

      size_t bytes = 0;
      for (const auto& item : segment->queue) {
        assert(item.seg == &segment.value());
        bytes += item.req.size();
      }
      assert(bytes <= stat.segment_size);
      assert(bytes == segment->filled_bytes);
      stored_in_whole_cache += bytes;
    }

    // Sanity check - the sum of all of the requests active in the hash table
    // should not be greater than the combined space in all of the in use
    // segments.
    size_t reachable_from_map = 0;
    std::unordered_set<int32_t> seen{};
    for (auto& entry : map) {
      item& item = *entry.second;
      // HT key had better only point to objects with the same kid.
      if (entry.first != item.req.kid) {
        std::cerr << "Mismatch! map entry "
                  << entry.first << " != " << item.req.kid << std::endl;
        item.req.dump();
      }
      assert(entry.first == item.req.kid);
      // Better not see the same kid twice among the objects in the HT.
      assert(seen.find(item.req.kid) == seen.end());
      reachable_from_map += item.req.size();
      seen.insert(item.req.kid);
    }
    if (reachable_from_map > stored_in_whole_cache) {
      std::cerr << "reachable_from_map: " << reachable_from_map << std::endl
                << "stored_in_whole_cache: " << stored_in_whole_cache
                << std::endl;
    }
    assert(reachable_from_map <= stored_in_whole_cache);

    size_t seg_num = 0;
    for (auto& segment : dst_segments) {
      if (!segment)
        continue;

      std::cerr << "Destintation Segment " << seg_num++ << " ";

      std::unordered_map<int32_t, size_t> per_app_in_use{};
      for (item& item : segment->queue)
        per_app_in_use[item.req.appid] += item.req.size();
      for (auto& pr : per_app_in_use) {
        int32_t appid = pr.first;
        size_t bytes_in_srcs = pr.second;
        std::cerr << "App " << appid << " " << bytes_in_srcs << " ";
      }
      std::cerr << std::endl;
    }

    std::cerr << "After cleaning ";
    for (auto& pr : apps) {
      int32_t appid = pr.first;
      auto& app = pr.second;
      std::cerr << "App " << appid << " " << app.need() << " ";
    }
    std::cerr << std::endl;
  } 
}

