/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "cuckoofilter.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <sys/resource.h>

using cuckoofilter::CuckooFilter;

double time_diff(struct rusage& start, struct rusage& end) {
    return (end.ru_utime.tv_usec + end.ru_stime.tv_usec -
        start.ru_utime.tv_usec - start.ru_stime.tv_usec) +
    1000000 * (end.ru_utime.tv_sec + end.ru_stime.tv_sec -
        start.ru_utime.tv_sec - start.ru_stime.tv_sec);
}

template <int fingerprint_size>
int test(int total_items) {
    CuckooFilter<size_t, fingerprint_size> filter(total_items);
    std::cout << "fingerprint_size="<<fingerprint_size<<","<<"m="<<total_items<<std::endl;

    // Insert items to this cuckoo filter
    size_t num_inserted = 0;
    struct rusage start;
    struct rusage end;

    getrusage(RUSAGE_SELF, &start);
    for (size_t i = 0; i < total_items; i++, num_inserted++) {
        if (filter.Add(i*i) != cuckoofilter::Ok) {
            break;
        }
    }
    getrusage(RUSAGE_SELF, &end);

    std::cout << "Construction throughput (MOPS): " << num_inserted/time_diff(start, end) << ",";

    // Check if previously inserted items are in the filter, expected
    // true for all items
    size_t counter = 0;

    getrusage(RUSAGE_SELF, &start);
    for (size_t i = 0; i < num_inserted; i++) {
        if(filter.Contain(i*i) == cuckoofilter::Ok) {
            counter++;
        }
    }
    getrusage(RUSAGE_SELF, &end);
    
    std::cout << "Positive query throughput (MOPS): " << num_inserted/time_diff(start, end) << ",";

    if( counter != num_inserted ) {
        std::cout << "Counter is " << counter << "while actual is " << num_inserted << std::endl;
    }

    // Check non-existing items, a few false positives expected
    size_t total_queries = 0;
    size_t false_queries = 0;

    getrusage(RUSAGE_SELF, &start);
    for (size_t i = 0; i < total_items; i++) {
        if (filter.Contain(i*i+2) == cuckoofilter::Ok) {
            false_queries++;
        }
        total_queries++;
    }
    getrusage(RUSAGE_SELF, &end);

    std::cout << "Negative query throughput (MOPS): " << num_inserted/time_diff(start, end) << std::endl;

    // Output the measured false positive rate
    std::cout << "false_positive_rate="<< ((double) false_queries) / total_queries<< ",";
    std::cout << "load_factor="<<filter.LoadFactor() << ",";
    std::cout << "bits_per_item="<<filter.BitsPerItem() << std::endl;
}

int main(int argc, char** argv) {
    size_t total_items  = 1<<20;

    test<2>(total_items);
    test<4>(total_items);
    test<8>(total_items);
    test<12>(total_items);
    test<16>(total_items);
    test<32>(total_items);

    return 0;
 }
