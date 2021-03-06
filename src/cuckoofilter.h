/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef _CUCKOO_FILTER_H_
#define _CUCKOO_FILTER_H_

#include "debug.h"
#include "hashutil.h"
#include "packedtable.h"
#include "printutil.h"
#include "singletable.h"

#include <cassert>

namespace cuckoofilter {
    // status returned by a cuckoo filter operation
    enum Status {
        Ok = 0,
        NotFound = 1,
        NotEnoughSpace = 2,
        NotSupported = 3,
    };

    // maximum number of cuckoo kicks before claiming failure
    const size_t kMaxCuckooCount = 500;

    // A cuckoo filter class exposes a Bloomier filter interface,
    // providing methods of Add, Delete, Contain. It takes three
    // template parameters:
    //   ItemType:  the type of item you want to insert
    //   bits_per_item: how many bits each item is hashed into
    //   TableType: the storage of table, SingleTable by default, and
    // PackedTable to enable semi-sorting 
    template <typename ItemType,
              size_t bits_per_item,
              template<size_t> class TableType = SingleTable>
    class CuckooFilter {
        // Storage of items
        TableType<bits_per_item> *table_;

        // Number of items stored
        size_t  num_items_;

        typedef struct {
            size_t index;
            uint32_t tag;
            bool used;
        } VictimCache;

        VictimCache victim_;

        inline size_t IndexHash(uint32_t hv) const {
            return hv % table_->num_buckets;
        }

        inline uint32_t TagHash(uint32_t hv) const {
            uint32_t tag;
            tag = hv & ((1ULL << bits_per_item) - 1);
            tag += (tag == 0);
            return tag;
        }

        inline void GenerateIndexTagHash(const ItemType &item,
                                         size_t* index,
                                         uint32_t* tag) const {

            std::string hashed_key = HashUtil::SHA1Hash((const char*) &item,
                                                   sizeof(item));
            uint64_t hv = *((uint64_t*) hashed_key.c_str());

            *index = IndexHash((uint32_t) (hv >> 32));
            *tag   = TagHash((uint32_t) (hv & 0xFFFFFFFF));
        }

        inline size_t AltIndex(const size_t index, const uint32_t tag, int which) const {
            // NOTE(binfan): originally we use:
            // index ^ HashUtil::BobHash((const void*) (&tag), 4)) & table_->INDEXMASK;
            // now doing a quick-n-dirty way:
            // 0x5bd1e995 is the hash constant from MurmurHash2
            switch (which) {
                case 0:
                    return index;
                case 1:
                    return IndexHash((uint32_t) (index ^ (tag * 0x5bd1e995)));
                case 2:
                    //return IndexHash((uint32_t) (index ^ (tag * 0x18777B15 )));
		    return IndexHash((uint32_t) (index ^ HashUtil::BobHash((const void*) (&tag), 4)));
                case 3:
                    //return IndexHash((uint32_t) (index ^ (tag * 0x5bd1e995 ) ^ (tag * 0x18777B15 )));
		    return IndexHash((uint32_t) (index ^ (tag * 0x5bd1e995) ^ HashUtil::BobHash((const void*) (&tag), 4)));
            }
            return 0;
        }

        Status AddImpl(const size_t i, const uint32_t tag);

    public:

        // load factor is the fraction of occupancy
        double LoadFactor() const {
            return 1.0 * Size()  / table_->SizeInTags();
        }

        double BitsPerItem() const {
            return table_->BitsPerTag() / LoadFactor();
        }

        explicit CuckooFilter(const size_t max_num_keys): num_items_(0) {
            size_t assoc = 1;
            size_t num_buckets = upperpower2(max_num_keys / assoc);
            /*
            double frac = (double) max_num_keys / num_buckets / assoc;
            if (frac > 0.96) {
                num_buckets <<= 1;
            }
            */
            victim_.used = false;
            table_  = new TableType<bits_per_item>(num_buckets);
        }

        ~CuckooFilter() {
            delete table_;
        }


        // Add an item to the filter.
        Status Add(const ItemType& item);

        // Report if the item is inserted, with false positive rate.
        Status Contain(const ItemType& item) const;

        // Delete an key from the filter
        Status Delete(const ItemType& item);

        /* methods for providing stats  */
        // summary infomation
        std::string Info() const;
        void print() const {
	    table_->print();
	}
        // number of current inserted items;
        size_t Size() const { return num_items_; }

        // size of the filter in bytes.
        size_t SizeInBytes() const { return table_->SizeInBytes(); }
    };


    template <typename ItemType, size_t bits_per_item,
              template<size_t> class TableType>
    Status
    CuckooFilter<ItemType, bits_per_item, TableType>::Add(
            const ItemType& item) {
        size_t i;
        uint32_t tag;

        if (victim_.used) {
            return NotEnoughSpace;
        }

        GenerateIndexTagHash(item, &i, &tag);
        return AddImpl(i, tag);
    }

    template <typename ItemType, size_t bits_per_item,
              template<size_t> class TableType>
    Status
    CuckooFilter<ItemType, bits_per_item, TableType>::AddImpl(
        const size_t i, const uint32_t tag) {
        size_t curindex = i;
        uint32_t curtag = tag;
        uint32_t oldtag;

        for (uint32_t count = 0; count < kMaxCuckooCount; count++) {
            oldtag = 0;
            int seq[4] = {0, 1, 2, 3};
            int x, tmp;
            x = rand() % 4;
            tmp = seq[x];
            seq[x] = seq[3];
            seq[3] = tmp;
            x = rand() % 3;
            tmp = seq[x];
            seq[x] = seq[2];
            seq[2] = tmp;
            x = rand() % 2;
            tmp = seq[x];
            seq[x] = seq[1];
            seq[1] = tmp;
            for (int i=0; i<4; i++) {
                bool kickout = (i == 3);
                if (table_->InsertTagToBucket(AltIndex(curindex, curtag, seq[i]), curtag, kickout, oldtag)) {
		    ////std::cout << "inserting ind " << curindex << "tag " << curtag << "into " << AltIndex(curindex, curtag, i) << std::endl;
		    num_items_++;
                    return Ok;
                }
            }

            curindex = AltIndex(curindex, curtag, seq[3]);
            curtag = oldtag;

        }
	victim_.index = curindex;
	victim_.tag = curtag;
	victim_.used = true;
        return Ok;
    }

    template <typename ItemType,
              size_t bits_per_item,
              template<size_t> class TableType>
    Status
    CuckooFilter<ItemType, bits_per_item, TableType>::Contain(
            const ItemType& key) const {
        bool found = false;
        size_t inds[4];
        uint32_t tag;

        GenerateIndexTagHash(key, &inds[0], &tag);
	for(int i = 1; i < 4; ++i) {
	     inds[i] = AltIndex(inds[0], tag, i);
	}
	bool victim_matches = false;
	for(int i = 0; i < 4; ++i) {
	     if(inds[i] == victim_.index) {
		victim_matches = true;
		break;	     
	     }
	}
	
        found = victim_.used && (tag == victim_.tag) && victim_matches;

        if (found) {
            return Ok;
        } else {
            for (int i=0; i<4; i++) {
                //std::cout << "Searching for tag " << tag << " in bucket " << inds[i] << std::endl;
		if (table_->FindTagInBucket(inds[i], tag)) {
                    return Ok;
                }
            }
            return NotFound;
        }
    }

    template <typename ItemType,
              size_t bits_per_item,
              template<size_t> class TableType>
    Status
    CuckooFilter<ItemType, bits_per_item, TableType>::Delete(
            const ItemType& key) {
        size_t i1, i2;
        uint32_t tag;

        GenerateIndexTagHash(key, &i1, &tag);
        // TODO: Doesn't support
        i2 = AltIndex(i1, tag, 0);

        if (table_->DeleteTagFromBucket(i1, tag))  {
            num_items_--;
            goto TryEliminateVictim;
        } else if (table_->DeleteTagFromBucket(i2, tag))  {
            num_items_--;
            goto TryEliminateVictim;
        } else if (victim_.used && tag == victim_.tag &&
                 (i1 == victim_.index || i2 == victim_.index)) {
            //num_items_--;
            victim_.used = false;
            return Ok;
        } else {
            return NotFound;
        }
    TryEliminateVictim:
        if (victim_.used) {
            victim_.used = false;
            size_t i = victim_.index;
            uint32_t tag = victim_.tag;
            AddImpl(i, tag);
        }
        return Ok;
    }

    template <typename ItemType,
              size_t bits_per_item,
              template<size_t> class TableType>
    std::string CuckooFilter<ItemType, bits_per_item, TableType>::Info() const {
        std::stringstream ss;
        ss << "CuckooFilter Status:\n"
#ifdef QUICK_N_DIRTY_HASHING
           << "\t\tQuick hashing used\n"
#else
           << "\t\tBob hashing used\n"
#endif
           << "\t\t" << table_->Info() << "\n"
           << "\t\tKeys stored: " << Size() << "\n"
           << "\t\tLoad facotr: " << LoadFactor() << "\n"
           << "\t\tHashtable size: " << (table_->SizeInBytes() >> 10)
           << " KB\n";
        if (Size() > 0) {
            ss << "\t\tbit/key:   " << BitsPerItem() << "\n";
        } else {
            ss << "\t\tbit/key:   N/A\n";
        }
        return ss.str();
    }
}  // namespace cuckoofilter

#endif // #ifndef _CUCKOO_FILTER_H_
