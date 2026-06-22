#include <iostream>
#include <vector>

int main() {
    // 64 integers = 256 Bytes. Fits easily in four 64-Byte cache blocks.
    const int SIZE = 64; 
    std::vector<int> arr(SIZE, 0);
    long long sum = 0;
    
    // Loop thousands of times over the exact same tiny array
    for (int iter = 0; iter < 50000; ++iter) {
        for (int i = 0; i < SIZE; ++i) {
            arr[i] += 1;  
            sum += arr[i];
        }
    }

    std::cout << "L1 Resident Loop Complete. Sum: " << sum << std::endl;
    return 0;
}

/* * Simulation Guide:
 * - Write Buffer Entries: 16
 * - Max Accesses: 500000
 * * What to Expect:
 * Expect a high L1d Hit Rate. Because the array is tiny (256B), 
 * the Write Buffer and L1 cache will hold everything locally. 
 * Main Memory Bus Traffic will be effectively zero.
 */