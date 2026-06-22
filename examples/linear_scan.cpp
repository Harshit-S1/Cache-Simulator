#include <iostream>
#include <vector>

int main() {
    // 8MB array - larger than L1 and L2 caches
    const int SIZE = 2097152; 
    std::vector<int> arr(SIZE, 0);
    long long sum = 0;

    // Linear sequential access
    for (int iter = 0; iter < 10; ++iter) {
        for (int i = 0; i < SIZE; ++i) {
            arr[i] += 2;
            sum += arr[i];
        }
    }

    std::cout << "Linear Scan Complete. Sum: " << sum << std::endl;
    return 0;
}

/* * Simulation Guide:
 * - Block Size: 64 Bytes
 * - Max Accesses: 0 (Unlimited)
 * * What to Expect:
 * Even though the 8MB array is much larger than the cache, the L1d Hit Rate 
 * will remain high. This demonstrates perfect spatial locality: 
 * one miss per 64-byte block, followed by 15 hits for the subsequent integers.
 */