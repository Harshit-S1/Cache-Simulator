#include <iostream>
#include <vector>

int main() {
    // 512KB array (16 strides of 32KB)
    const int SIZE = 131072; 
    std::vector<int> arr(SIZE, 1);
    long long sum = 0;

    // Stride exactly 32KB to target the same cache set
    const int STRIDE = 8192; 

    // Massive loop to ensure conflict misses dominate the trace
    for (int iter = 0; iter < 500000; ++iter) {
        for (int i = 0; i < 16; ++i) {
            arr[i * STRIDE] += 1;  
            sum += arr[i * STRIDE];
        }
    }

    std::cout << "Conflict Thrashing Complete. Sum: " << sum << std::endl;
    return 0;
}

/* Simulation Guide:
 * - Block Size: 64 Bytes
 * - L1d Size: 32768 Bytes (32 KB)
 * - L1d Associativity: 8
 * - Max Accesses: 2000000
 * What to Expect:
 * The L1d Hit Rate will plummet as 16 addresses compete for 8 slots. 
 * Watch how the Write Buffer mitigates this by absorbing redundant write traffic.
 */