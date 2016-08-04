#include <iostream>
#include <math.h>
#include <fstream>
#include "flash_cache_lruk_clock.h"

size_t FC_K_LRU_CLK = 8;
size_t DRAM_SIZE_FC_KLRU_CLK = 51209600;
size_t FC_KLRU_QUEUE_SIZE_CLK = DRAM_SIZE_FC_KLRU_CLK / FC_K_LRU_CLK;
size_t FLASH_SIZE_FC_KLRU_CLK = 51209600;
size_t CLOCK_MAX_VALUE_KLRU = 7;

FlashCacheLrukClk::FlashCacheLrukClk(stats stat) :
	policy(stat),
	dram(FC_K_LRU_CLK),
        clockLru(),
	kLruSizes(FC_K_LRU_CLK, 0),
	flash(),
	allObjects(),
	dramSize(0),
	flashSize(0),
	clockIt(),
        firstEviction(false)
{
}

FlashCacheLrukClk::~FlashCacheLrukClk() {}

size_t FlashCacheLrukClk::get_bytes_cached() const {
	return dramSize + flashSize;
}

size_t FlashCacheLrukClk::proc(const request* r, bool warmup) {
	if (!warmup) {stat.accesses++;}

	bool updateWrites = true;
	uint32_t mfuKid=0;
	
	auto searchRKId = allObjects.find(r->kid);
	if (searchRKId != allObjects.end()) {
		// Object found

		FlashCacheLrukClk::Item& item = searchRKId->second;
		FlashCacheLrukClk::dramIt& clockItemIt = item.ClockIt;

		if (r->size() == item.size) {
			if (!warmup) {stat.hits++;}

			//reseting the clock
			clockItemIt->second = CLOCK_MAX_VALUE_KLRU;

			if (item.isInDram) {

				if (!warmup) {stat.hits_dram++;}

				size_t qN = item.queueNumber;

				dram[qN].erase(item.dramLocation);
				kLruSizes[qN] -= item.size;
				dramSize -= item.size;

				if ((qN + 1) != FC_K_LRU_CLK) {
						qN++;
				} else {
						updateWrites = false;
				}

				std::vector<uint32_t> objects{r->kid};
				dramAddandReorder(objects, r->size(),qN, updateWrites, warmup);

			} else {
				if (!warmup) {stat.hits_flash++;}
			}

			return 1;

		} else {// Size updated

			if (clockItemIt == clockIt) {
			// changing the clockIt since this item is being deleted
				clockIt++;
				if (clockIt == clockLru.end()) {
					clockIt = clockLru.begin();
				}
			}

			if(item.isInDram) {
				size_t qN = item.queueNumber;
				dram[qN].erase(item.dramLocation);
				kLruSizes[qN] -= item.size;
				dramSize -= item.size;

			} else {// in flash
				flash.erase(item.flashIt);
				flashSize -= item.size;
			}	

			clockLru.erase(clockItemIt);
			allObjects.erase(item.kId);
		}
	}

	/*
	* The request doesn't exist in the system or was updated. We always insert
	* new requests to the DRAM at the beginning of the last queue.
	*
	* 2. While (object not inserted to the DRAM)
	*	2.1  if (item.size() + dramSize <= DRAM_SIZE_FC_KLRU) -
	*		insert item to the dram and return	
	* 	2.2 if possible to move from DRAM to flash -
	*		by Dram LRUK
	*	2.4 remove item by clock method. back to 2
	*/

	FlashCacheLrukClk::Item newItem;
	newItem.kId = r->kid;
	newItem.size = r->size();
	newItem.isInDram = true;

	assert(((unsigned int) newItem.size) <= FC_KLRU_QUEUE_SIZE_CLK);


	while (true) {

		if (!firstEviction) { firstEviction = true;}
		bool isDeleted = false;
		dramIt tmpIt, startIt = clockIt;

		if (newItem.size + dramSize <= DRAM_SIZE_FC_KLRU_CLK) {
			// If we have place in the dram insert the new item to the beginning of the last queue.

			allObjects[newItem.kId] = newItem;
			
            std::vector<uint32_t> objects{r->kid};
            dramAdd(objects, r->size(),0, true, warmup, true);

			return PROC_MISS;
		}
		// If we don't have enough space in the dram, we can move MRU items to the flash
		// or to delete LRU items

		for (int i=FC_K_LRU_CLK-1; i >=0 ; i--)
		{// Find the MRU item
			if (kLruSizes[i] > 0){
				mfuKid = (dram[i]).front();
				break;
			}
		}

		FlashCacheLrukClk::Item& mfuItem = allObjects[mfuKid];
		size_t qN = mfuItem.queueNumber;

		assert(mfuItem.size > 0);	

		if (flashSize + mfuItem.size <= FLASH_SIZE_FC_KLRU_CLK) {
		// If we have enough space in the flash, we will insert the MRU item
		// to the flash

			mfuItem.isInDram = false;
			dram[qN].erase(mfuItem.dramLocation);
			flash.emplace_front(mfuKid);
			mfuItem.dramLocation = ((dram[0]).end());
			dramSize -= mfuItem.size;
			mfuItem.flashIt = flash.begin();
			kLruSizes[qN] -= mfuItem.size;
			flashSize += mfuItem.size;
			if (!warmup) {
				stat.writes_flash++;
				stat.flash_bytes_written += mfuItem.size;
			}
		}else {
		// If we don't have space in the flash, we will delete the first item with clock 0
		// and make room for the new item

			while(clockIt != clockLru.end()) {
				assert(clockIt->second <= CLOCK_MAX_VALUE_KLRU);
				if (clockIt->second == 0) {
					// This item need to be delete
					tmpIt = clockIt;
					tmpIt++;
					deleteItem(clockIt->first);
					isDeleted = true;
					break;
				} else {
					clockIt->second--;
				}
				clockIt++;
			}

			if (!isDeleted) {
				// No item was deleted moving to items on the other
				// half of the watch
				clockIt = clockLru.begin();
				assert(clockIt->second <= CLOCK_MAX_VALUE_KLRU);

				while (clockIt != startIt) {
					if (clockIt->second == 0) {
						tmpIt = clockIt;
						tmpIt++;
						deleteItem(clockIt->first);
						isDeleted = true;
						break;
					} else {
						clockIt->second--;
					}
					clockIt++;
				}
			}

			if (!isDeleted) {
				// After a full cycle no item was deleted
				// Delete the item you started with
				//if (!warmup) { noZeros++;}
				assert(clockLru.size() > 0);
				assert(clockIt != clockLru.end());
				tmpIt = clockIt;
				tmpIt++;
				deleteItem(clockIt->first);
			}
			//reseting the clockIt again
			clockIt = (tmpIt == clockLru.end()) ? clockLru.begin() : tmpIt;
		}
	}
	assert(false);	
	return PROC_MISS;
}

void FlashCacheLrukClk::dramAdd(std::vector<uint32_t>& objects,
		size_t sum,
		size_t k,
		bool updateWrites,
		bool warmup,
		bool NewItem) {

	    for (const uint32_t& elem : objects) {
				FlashCacheLrukClk::Item& item = allObjects[elem];
				dram[k].emplace_front(elem);
				item.dramLocation = dram[k].begin();
				item.queueNumber = k;
				dramSize += item.size;
				kLruSizes[k] += item.size;
				if (k != 0){ assert(kLruSizes[k] <= FC_KLRU_QUEUE_SIZE_CLK);}

				if (NewItem){ //New Item, need to insert to clocklru and update clock
					std::pair<uint32_t,size_t> it;
					it =  std::make_pair(item.kId, CLOCK_MAX_VALUE_KLRU);

					if (clockLru.size() == 0) {
						clockLru.emplace_front(it);
						clockIt = clockLru.begin();
						item.ClockIt = clockLru.begin();
					} else {
						clockLru.insert(clockIt,it);
						dramIt Clkit = clockIt;
						Clkit--;
						item.ClockIt = Clkit;
					}
				}
		}
}

void FlashCacheLrukClk::dramAddandReorder(std::vector<uint32_t>& objects,
		size_t sum,
		size_t k,
		bool updateWrites,
		bool warmup) {

	    assert(k < FC_K_LRU_CLK);

		std::vector<uint32_t> newObjects;

		size_t newSum = 0;

		if (k != 0)
		{
			while (sum + kLruSizes[k] > FC_KLRU_QUEUE_SIZE_CLK) {
				assert(kLruSizes[k] > 0);
				assert(dram[k].size() > 0);
				uint32_t elem = dram[k].back();
				FlashCacheLrukClk::Item& item = allObjects[elem];
				dram[k].pop_back();
				kLruSizes[k] -= item.size;
				dramSize -= item.size;
				//saving the extracted items in order to put them in lower queue
				newSum += item.size;
				newObjects.emplace_back(elem);
			}
		} else {
			while (sum + dramSize > DRAM_SIZE_FC_KLRU_CLK) {
				//shouldnt get to here
				assert(0);
				assert(kLruSizes[k] > 0);
				assert(dram[k].size() > 0);
				uint32_t elem = dram[k].back();
				FlashCacheLrukClk::Item& item = allObjects[elem];
				dram[k].pop_back();
				kLruSizes[k] -= item.size;
				dramSize -= item.size;
			}
		}

		if (!updateWrites) {
			assert(newObjects.size() == 0);
			assert(newSum == 0);
		}

		dramAdd(objects,sum,k,updateWrites,warmup, false);

		if (k > 0 && newObjects.size() > 0) {
			dramAddandReorder(newObjects, newSum, k-1, true, warmup);
		}
}

void FlashCacheLrukClk::deleteItem(uint32_t keyId) {

	auto searchRKId = allObjects.find(keyId);
	assert(searchRKId != allObjects.end());

	FlashCacheLrukClk::Item& item = searchRKId->second;
	clockLru.erase(item.ClockIt);

	if (item.isInDram) {
		size_t dGqN = item.queueNumber;
		dram[dGqN].erase(item.dramLocation);
		kLruSizes[dGqN] -= item.size;
		dramSize -= item.size;
	} else {
		flash.erase(item.flashIt);
		flashSize -= item.size;
	}

	allObjects.erase(keyId);
}


void FlashCacheLrukClk::dump_stats(void) {
	assert(stat.apps.size() == 1);
	uint32_t appId = 0;
	for(const auto& app : stat.apps) {appId = app;}
	std::string filename{stat.policy
			+ "-app" + std::to_string(appId)
			+ "-flash_mem" + std::to_string(FLASH_SIZE_FC_KLRU_CLK)
			+ "-dram_mem" + std::to_string(DRAM_SIZE_FC_KLRU_CLK)
			+ "-K" + std::to_string(FC_K_LRU_CLK)};
	std::ofstream out{filename};

	out << "dram size " << DRAM_SIZE_FC_KLRU_CLK << std::endl;
	out << "flash size " << FLASH_SIZE_FC_KLRU_CLK << std::endl;
	out << "#accesses "  << stat.accesses << std::endl;
	out << "#global hits " << stat.hits << std::endl;
	out << "#dram hits " << stat.hits_dram << std::endl;
	out << "#flash hits " << stat.hits_flash << std::endl;
	out << "hit rate " << double(stat.hits) / stat.accesses << std::endl;
	out << "#writes to flash " << stat.writes_flash << std::endl;
	out << "credit limit " << stat.credit_limit << std::endl; 
	out << "#bytes written to flash " << stat.flash_bytes_written << std::endl;
	out << std::endl << std::endl;

	out << "Amount of lru queues: " << FC_K_LRU_CLK << std::endl;
	for (size_t i =0 ; i < FC_K_LRU_CLK ; i++){
		out << "dram[" << i << "] has " << dram[i].size() << " items" << std::endl;
		out << "dram[" << i << "] size written " << kLruSizes[i] << std::endl;
	}
	out << "Total dram filled size " << dramSize << std::endl;
}

