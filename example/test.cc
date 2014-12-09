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

int main(int argc, char** argv) {
    size_t total_items  = 1<<20;

    // Create a cuckoo filter where each item is of type size_t and
    // use 12 bits for each item:
    //    CuckooFilter<size_t, 12> filter(total_items);
    // To enable semi-sorting, define the storage of cuckoo filter to be
    // PackedTable, accepting keys of size_t type and making 13 bits
    // for each key:
    //   CuckooFilter<size_t, 13, PackedTable> filter(total_items);
    CuckooFilter<size_t, 8> filter(total_items);

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

    std::cout << "Construction throughput (MOPS): " << num_inserted/time_diff(start, end) << std::endl;

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
    
    std::cout << "Positive query throughput (MOPS): " << num_inserted/time_diff(start, end) << std::endl;

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
    std::cout << "false positive rate is "
              << 100.0 * false_queries / total_queries
              << "%\n";

    std::cout << filter.Info();

    return 0;
 }
