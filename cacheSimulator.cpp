#include <iostream>
#include <fstream>
#include <vector>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <memory>
#include <climits>
#include <iomanip>
#include <string>
#include <cstdlib>
#include <ctime>

using namespace std;
typedef unsigned long long ull;

// the available replacement and write policies
enum ReplacementPolicy { LRU = 0, LFU = 1, FIFO = 2, RANDOM = 3, SRRIP = 4, NRU = 5, PLRU = 6, OPT = 7 };
enum WritePolicy { WRITE_THROUGH = 0, WRITE_BACK = 1 };

// struct to group the results of a cache eviction
struct EvictionResult {
    bool valid = false; 
    ull block_addr = 0;
    bool dirty = false;
};

// Base class for a cache set. 
// All the specific replacement policies inherit from this and implement the virtual functions.
class CacheSet {
protected:
    int associativity;
public:
    CacheSet(int assoc) : associativity(assoc) {}
    virtual ~CacheSet() {}
    
    virtual bool check_hit(ull block_addr, bool is_write) = 0;
    virtual EvictionResult allocate(ull block_addr, bool is_write) = 0;
    virtual bool invalidate(ull block_addr, bool& was_dirty) = 0;
    virtual void mark_dirty(ull block_addr) = 0;
};

// buffer to hold recent write requests to hide write latencies from the CPU
class WriteBuffer {
    int capacity;
    list<ull> buffer_queue; 
    unordered_set<ull> buffer_set; 

public:
    int hits = 0; 

    WriteBuffer(int cap) : capacity(cap) {}

    // checking if the data we are trying to read happens to be sitting in the write buffer
    bool check_read(ull block_addr) {
        if (capacity > 0 && buffer_set.find(block_addr) != buffer_set.end()) {
            hits++;
            return true;
        }
        return false;
    }

    // adding a new write to the buffer and if it gets full, flush the oldest entry.
    EvictionResult push_write(ull block_addr) {
        EvictionResult flush;
        if (capacity == 0) {
            flush.valid = true;
            flush.block_addr = block_addr;
            flush.dirty = true;
            return flush;
        }
        
        if (buffer_set.find(block_addr) == buffer_set.end()) {
            if (buffer_queue.size() >= capacity) {
                flush.valid = true;
                flush.block_addr = buffer_queue.front();
                flush.dirty = true; 
                
                buffer_set.erase(flush.block_addr);
                buffer_queue.pop_front();
            }
            buffer_queue.push_back(block_addr);
            buffer_set.insert(block_addr);
        }
        return flush;
    }
};

// simple next-line prefetcher which loads data into L2
class Prefetcher {
public:
    int prefetches_issued = 0;
    ull get_prefetch_target(ull current_block_addr) {
        prefetches_issued++;
        return current_block_addr + 1; 
    }
};

// Least Recently Used - tracks usage using a doubly-linked list
class LRUSet : public CacheSet {
    list<ull> lru_list; 
    unordered_map<ull, list<ull>::iterator> cache_map;
    unordered_map<ull, bool> dirty_map;

public:
    LRUSet(int assoc) : CacheSet(assoc) {}

    bool check_hit(ull block_addr, bool is_write) override {
        auto it = cache_map.find(block_addr);
        if (it != cache_map.end()) {
            lru_list.erase(it->second);
            lru_list.push_front(block_addr);
            cache_map[block_addr] = lru_list.begin();
            if (is_write) dirty_map[block_addr] = true;
            return true;
        }
        return false;
    }

    EvictionResult allocate(ull block_addr, bool is_write) override {
        EvictionResult res;
        if (cache_map.size() >= associativity) {
            res.valid = true;
            res.block_addr = lru_list.back();
            res.dirty = dirty_map[res.block_addr];
            lru_list.pop_back();
            cache_map.erase(res.block_addr);
            dirty_map.erase(res.block_addr);
        }
        lru_list.push_front(block_addr);
        cache_map[block_addr] = lru_list.begin();
        dirty_map[block_addr] = is_write;
        return res;
    }

    bool invalidate(ull block_addr, bool& was_dirty) override {
        auto it = cache_map.find(block_addr);
        if (it != cache_map.end()) {
            was_dirty = dirty_map[block_addr];
            lru_list.erase(it->second);
            cache_map.erase(it);
            dirty_map.erase(block_addr);
            return true;
        }
        was_dirty = false;
        return false;
    }

    void mark_dirty(ull block_addr) override {
        if (cache_map.find(block_addr) != cache_map.end()) dirty_map[block_addr] = true;
    }
};

// First-In First-Out: evicts the oldest loaded block regardless of recent usage
class FIFOSet : public CacheSet {
    list<ull> fifo_list;
    unordered_map<ull, list<ull>::iterator> cache_map;
    unordered_map<ull, bool> dirty_map;

public:
    FIFOSet(int assoc) : CacheSet(assoc) {}
    bool check_hit(ull block_addr, bool is_write) override {
        if (cache_map.find(block_addr) != cache_map.end()) {
            if (is_write) dirty_map[block_addr] = true;
            return true; 
        }
        return false;
    }
    EvictionResult allocate(ull block_addr, bool is_write) override {
        EvictionResult res;
        if (cache_map.size() >= associativity) {
            res.valid = true;
            res.block_addr = fifo_list.back();
            res.dirty = dirty_map[res.block_addr];
            fifo_list.pop_back();
            cache_map.erase(res.block_addr);
            dirty_map.erase(res.block_addr);
        }
        fifo_list.push_front(block_addr);
        cache_map[block_addr] = fifo_list.begin();
        dirty_map[block_addr] = is_write;
        return res;
    }
    bool invalidate(ull block_addr, bool& was_dirty) override {
        auto it = cache_map.find(block_addr);
        if (it != cache_map.end()) {
            was_dirty = dirty_map[block_addr];
            fifo_list.erase(it->second);
            cache_map.erase(it);
            dirty_map.erase(block_addr);
            return true;
        }
        was_dirty = false;
        return false;
    }
    void mark_dirty(ull block_addr) override {
        if (cache_map.find(block_addr) != cache_map.end()) dirty_map[block_addr] = true;
    }
};

// Random Replacement: Just picks an eviction target randomly
class RandomSet : public CacheSet {
    struct Block { ull block_addr; bool dirty; };
    vector<Block> blocks;

public:
    RandomSet(int assoc) : CacheSet(assoc) {}
    bool check_hit(ull block_addr, bool is_write) override {
        for (auto& b : blocks) {
            if (b.block_addr == block_addr) {
                if (is_write) b.dirty = true;
                return true; 
            }
        }
        return false;
    }
    EvictionResult allocate(ull block_addr, bool is_write) override {
        EvictionResult res;
        if (blocks.size() < associativity) {
            blocks.push_back({block_addr, is_write});
        } else {
            int evict_idx = rand() % associativity;
            res.valid = true;
            res.block_addr = blocks[evict_idx].block_addr;
            res.dirty = blocks[evict_idx].dirty;
            blocks[evict_idx] = {block_addr, is_write};
        }
        return res;
    }
    bool invalidate(ull block_addr, bool& was_dirty) override {
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->block_addr == block_addr) {
                was_dirty = it->dirty;
                blocks.erase(it);
                return true;
            }
        }
        was_dirty = false;
        return false;
    }
    void mark_dirty(ull block_addr) override {
        for (auto& b : blocks) if (b.block_addr == block_addr) b.dirty = true;
    }
};

// Least Frequently Used: Keeps a active record of access counts per block
class LFUSet : public CacheSet {
    struct BlockMeta { int frequency; bool dirty; list<ull>::iterator list_it; };
    unordered_map<ull, BlockMeta> cache_map;
    unordered_map<int, list<ull>> freq_map;
    int min_freq;

public:
    LFUSet(int assoc, ull& time_ref) : CacheSet(assoc), min_freq(0) {}
    bool check_hit(ull block_addr, bool is_write) override {
        auto it = cache_map.find(block_addr);
        if (it != cache_map.end()) {
            int freq = it->second.frequency;
            freq_map[freq].erase(it->second.list_it);
            if (freq_map[freq].empty()) {
                freq_map.erase(freq);
                if (min_freq == freq) min_freq++;
            }
            it->second.frequency++;
            int new_freq = it->second.frequency;
            freq_map[new_freq].push_front(block_addr);
            it->second.list_it = freq_map[new_freq].begin();
            if (is_write) it->second.dirty = true;
            return true; 
        }
        return false;
    }
    EvictionResult allocate(ull block_addr, bool is_write) override {
        EvictionResult res;
        if (cache_map.size() >= associativity) {
            ull evict_addr = freq_map[min_freq].back();
            res.valid = true;
            res.block_addr = evict_addr;
            res.dirty = cache_map[evict_addr].dirty;
            freq_map[min_freq].pop_back();
            if (freq_map[min_freq].empty()) freq_map.erase(min_freq);
            cache_map.erase(evict_addr);
        }
        min_freq = 1; 
        freq_map[1].push_front(block_addr);
        cache_map[block_addr] = {1, is_write, freq_map[1].begin()};
        return res;
    }
    bool invalidate(ull block_addr, bool& was_dirty) override {
        auto it = cache_map.find(block_addr);
        if (it != cache_map.end()) {
            was_dirty = it->second.dirty;
            int freq = it->second.frequency;
            freq_map[freq].erase(it->second.list_it);
            if (freq_map[freq].empty()) {
                freq_map.erase(freq);
                if (min_freq == freq && !cache_map.empty()) {
                    min_freq = INT_MAX;
                    for (const auto& pair : freq_map) {
                        if (pair.first < min_freq) min_freq = pair.first;
                    }
                }
            }
            cache_map.erase(it);
            return true;
        }
        was_dirty = false;
        return false;
    }
    void mark_dirty(ull block_addr) override {
        auto it = cache_map.find(block_addr);
        if (it != cache_map.end()) it->second.dirty = true;
    }
};

// Not Recently Used: Uses a single reference bit per block to approximate LRU
class NRUSet : public CacheSet {
    struct Block { ull block_addr; bool ref_bit; bool dirty; };
    vector<Block> blocks;

public:
    NRUSet(int assoc) : CacheSet(assoc) {}
    bool check_hit(ull block_addr, bool is_write) override {
        for (auto& b : blocks) {
            if (b.block_addr == block_addr) {
                b.ref_bit = true;
                if (is_write) b.dirty = true;
                return true;
            }
        }
        return false;
    }
    EvictionResult allocate(ull block_addr, bool is_write) override {
        EvictionResult res;
        if (blocks.size() < associativity) {
            blocks.push_back({block_addr, true, is_write});
            return res;
        }
        while (true) {
            for (int i = 0; i < associativity; ++i) {
                if (!blocks[i].ref_bit) {
                    res.valid = true;
                    res.block_addr = blocks[i].block_addr;
                    res.dirty = blocks[i].dirty;
                    blocks[i] = {block_addr, true, is_write};
                    return res;
                }
            }
            for (auto& b : blocks) b.ref_bit = false;
        }
    }
    bool invalidate(ull block_addr, bool& was_dirty) override {
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->block_addr == block_addr) {
                was_dirty = it->dirty;
                blocks.erase(it);
                return true;
            }
        }
        was_dirty = false;
        return false;
    }
    void mark_dirty(ull block_addr) override {
        for (auto& b : blocks) if (b.block_addr == block_addr) b.dirty = true;
    }
};

// Static Re-Reference Interval Prediction (SRRIP)
class SRRIPSet : public CacheSet {
    struct Block { ull block_addr; int rrpv; bool dirty; };
    vector<Block> blocks;
    const int MAX_RRPV = 3;      
    const int INSERT_RRPV = 2;   

public:
    SRRIPSet(int assoc) : CacheSet(assoc) {}
    bool check_hit(ull block_addr, bool is_write) override {
        for (auto& b : blocks) {
            if (b.block_addr == block_addr) {
                b.rrpv = 0; 
                if (is_write) b.dirty = true;
                return true;
            }
        }
        return false;
    }
    EvictionResult allocate(ull block_addr, bool is_write) override {
        EvictionResult res;
        if (blocks.size() < associativity) {
            blocks.push_back({block_addr, INSERT_RRPV, is_write});
            return res;
        }
        while (true) {
            for (int i = 0; i < associativity; ++i) {
                if (blocks[i].rrpv == MAX_RRPV) {
                    res.valid = true;
                    res.block_addr = blocks[i].block_addr;
                    res.dirty = blocks[i].dirty;
                    blocks[i] = {block_addr, INSERT_RRPV, is_write};
                    return res;
                }
            }
            for (auto& b : blocks) b.rrpv++;
        }
    }
    bool invalidate(ull block_addr, bool& was_dirty) override {
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->block_addr == block_addr) {
                was_dirty = it->dirty;
                blocks.erase(it);
                return true;
            }
        }
        was_dirty = false;
        return false;
    }
    void mark_dirty(ull block_addr) override {
        for (auto& b : blocks) if (b.block_addr == block_addr) b.dirty = true;
    }
};

// Tree-PLRU (Pseudo-LRU)
// Uses a binary tree of directional bits to point away from recently used blocks,
// providing near-LRU performance with much less hardware overhead (much more practical than LRU)
class PLRUSet : public CacheSet {
    struct Block { ull block_addr; bool dirty; bool valid; };
    vector<Block> blocks;
    vector<bool> tree; // Binary tree storing the directional bits

public:
    PLRUSet(int assoc) : CacheSet(assoc), blocks(assoc, {0, false, false}), 
                         tree(assoc > 1 ? assoc - 1 : 1, false) {}

    // Flip the bits to point away from the most recently accessed leaf
    void update_tree(int leaf_idx) {
        if (associativity == 1) return;
        int node = 0;
        int left_bound = 0;
        int right_bound = associativity - 1;
        while (node < associativity - 1) {
            int mid = left_bound + (right_bound - left_bound) / 2;
            if (leaf_idx <= mid) {
                tree[node] = 1; // 1 means point right (away from left)
                node = 2 * node + 1;
                right_bound = mid;
            } else {
                tree[node] = 0; // 0 means point left (away from right)
                node = 2 * node + 2;
                left_bound = mid + 1;
            }
        }
    }

    bool check_hit(ull block_addr, bool is_write) override {
        for (int i = 0; i < associativity; ++i) {
            if (blocks[i].valid && blocks[i].block_addr == block_addr) {
                if (is_write) blocks[i].dirty = true;
                update_tree(i);
                return true;
            }
        }
        return false;
    }

    EvictionResult allocate(ull block_addr, bool is_write) override {
        EvictionResult res;
        
        // empty lines are filled first before evicting
        int evict_idx = -1;
        for (int i = 0; i < associativity; ++i) {
            if (!blocks[i].valid) {
                evict_idx = i;
                break;
            }
        }

        // If full, tree is traversed to find the pseudo-oldest block
        if (evict_idx == -1 && associativity > 1) {
            int node = 0;
            int left_bound = 0;
            int right_bound = associativity - 1;
            while (node < associativity - 1) {
                int mid = left_bound + (right_bound - left_bound) / 2;
                if (tree[node] == 0) {
                    node = 2 * node + 1;
                    right_bound = mid;
                } else {
                    node = 2 * node + 2;
                    left_bound = mid + 1;
                }
            }
            evict_idx = left_bound;
        } else if (evict_idx == -1 && associativity == 1) {
            evict_idx = 0;
        }

        if (blocks[evict_idx].valid) {
            res.valid = true;
            res.block_addr = blocks[evict_idx].block_addr;
            res.dirty = blocks[evict_idx].dirty;
        }

        blocks[evict_idx] = {block_addr, is_write, true};
        update_tree(evict_idx);
        return res;
    }

    bool invalidate(ull block_addr, bool& was_dirty) override {
        for (int i = 0; i < associativity; ++i) {
            if (blocks[i].valid && blocks[i].block_addr == block_addr) {
                was_dirty = blocks[i].dirty;
                blocks[i].valid = false;
                return true;
            }
        }
        was_dirty = false;
        return false;
    }

    void mark_dirty(ull block_addr) override {
        for (int i = 0; i < associativity; ++i) {
            if (blocks[i].valid && blocks[i].block_addr == block_addr) {
                blocks[i].dirty = true;
                break;
            }
        }
    }
};

// Bélády's Optimal
// Peeks into the future reference queue to mathematically determine the best
// possible eviction target, Used purely to establish a benchmark ceiling (not practical)
class OPTSet : public CacheSet {
    struct Block { ull block_addr; bool dirty; bool valid; };
    vector<Block> blocks;
    unordered_map<ull, queue<ull>>* future_refs;
    ull* global_time;

public:
    OPTSet(int assoc, unordered_map<ull, queue<ull>>* refs, ull* time_ptr) 
        : CacheSet(assoc), blocks(assoc, {0, false, false}), future_refs(refs), global_time(time_ptr) {}

    bool check_hit(ull block_addr, bool is_write) override {
        for (int i = 0; i < associativity; ++i) {
            if (blocks[i].valid && blocks[i].block_addr == block_addr) {
                if (is_write) blocks[i].dirty = true;
                return true;
            }
        }
        return false;
    }

    EvictionResult allocate(ull block_addr, bool is_write) override {
        EvictionResult res;
        
        // empty slots are grabbed first
        for (int i = 0; i < associativity; ++i) {
            if (!blocks[i].valid) {
                blocks[i] = {block_addr, is_write, true};
                return res;
            }
        }

        int evict_idx = 0;
        ull max_future_time = 0;

        // greedily searching for the block needed furthest in the future
        for (int i = 0; i < associativity; ++i) {
            ull b_addr = blocks[i].block_addr;
            
            // cleaning up stale past accesses ensuring we only look forward
            while (!(*future_refs)[b_addr].empty() && (*future_refs)[b_addr].front() <= *global_time) {
                (*future_refs)[b_addr].pop();
            }

            if ((*future_refs)[b_addr].empty()) {
                evict_idx = i;
                break; // if never accessed again then perfect eviction target.
            }
            
            ull next_time = (*future_refs)[b_addr].front();
            if (next_time > max_future_time) {
                max_future_time = next_time;
                evict_idx = i;
            }
        }

        res.valid = true;
        res.block_addr = blocks[evict_idx].block_addr;
        res.dirty = blocks[evict_idx].dirty;
        
        blocks[evict_idx] = {block_addr, is_write, true};
        return res;
    }

    bool invalidate(ull block_addr, bool& was_dirty) override {
        for (int i = 0; i < associativity; ++i) {
            if (blocks[i].valid && blocks[i].block_addr == block_addr) {
                was_dirty = blocks[i].dirty;
                blocks[i].valid = false;
                return true;
            }
        }
        was_dirty = false;
        return false;
    }

    void mark_dirty(ull block_addr) override {
        for (int i = 0; i < associativity; ++i) {
            if (blocks[i].valid && blocks[i].block_addr == block_addr) {
                blocks[i].dirty = true;
                break;
            }
        }
    }
};

// Represents a single level in the cache hierarchy (e.g., L1, L2)
// Manages multiple CacheSets derived from the requested size and associativity
class CacheLevel {
    int num_sets;
    int block_size;
    vector<unique_ptr<CacheSet>> sets; 

public:
    ull hits = 0;
    ull misses = 0;

    // future_refs parameter added to pass down to the OPT sets if active
    CacheLevel(int cache_size_bytes, int block_size_bytes, int assoc, ReplacementPolicy rep_policy, 
               ull& global_time, unordered_map<ull, queue<ull>>* future_refs = nullptr) {
        block_size = block_size_bytes;
        num_sets = cache_size_bytes / (block_size * assoc);
        
        for (int i = 0; i < num_sets; ++i) {
            if (rep_policy == LRU) sets.push_back(make_unique<LRUSet>(assoc));
            else if (rep_policy == FIFO) sets.push_back(make_unique<FIFOSet>(assoc));
            else if (rep_policy == RANDOM) sets.push_back(make_unique<RandomSet>(assoc));
            else if (rep_policy == LFU) sets.push_back(make_unique<LFUSet>(assoc, global_time));
            else if (rep_policy == SRRIP) sets.push_back(make_unique<SRRIPSet>(assoc));
            else if (rep_policy == NRU) sets.push_back(make_unique<NRUSet>(assoc));
            else if (rep_policy == PLRU) sets.push_back(make_unique<PLRUSet>(assoc));
            else if (rep_policy == OPT) sets.push_back(make_unique<OPTSet>(assoc, future_refs, &global_time));
        }
    }

    // functions to map a block address to its corresponding set
    bool check_and_update(ull block_addr, bool is_write) {
        return sets[block_addr % num_sets]->check_hit(block_addr, is_write);
    }
    EvictionResult allocate(ull block_addr, bool is_write) {
        return sets[block_addr % num_sets]->allocate(block_addr, is_write);
    }
    bool invalidate(ull block_addr, bool& was_dirty) {
        return sets[block_addr % num_sets]->invalidate(block_addr, was_dirty);
    }
    void mark_dirty(ull block_addr) {
        sets[block_addr % num_sets]->mark_dirty(block_addr);
    }
};

// The main system containing all cache levels and microarchitectural additions
class CacheHierarchy {
    int block_size;
    ReplacementPolicy rep_policy;
    WritePolicy write_policy;
    
    unique_ptr<WriteBuffer> wb;
    unique_ptr<Prefetcher> prefetcher;
    unique_ptr<CacheLevel> victim_cache; 

    unique_ptr<CacheLevel> l1i;
    unique_ptr<CacheLevel> l1d;
    unique_ptr<CacheLevel> l2;
    unique_ptr<CacheLevel> l3;

    ull global_time = 0; 
    ull total_accesses = 0;
    ull mem_reads = 0;
    ull mem_writes = 0;

public:
    // all levels of the cache hierarchy are setup here
    CacheHierarchy(int b_size, int l1i_sz, int l1i_as, int l1d_sz, int l1d_as, 
                   int l2_sz, int l2_as, int l3_sz, int l3_as, 
                   int wb_entries, int vc_entries,
                   ReplacementPolicy r_pol, WritePolicy w_pol,
                   unordered_map<ull, queue<ull>>* future_refs = nullptr) {
        
        block_size = b_size;
        rep_policy = r_pol;
        write_policy = w_pol;

        if (rep_policy == RANDOM) srand(time(NULL));

        l1i = make_unique<CacheLevel>(l1i_sz, block_size, l1i_as, rep_policy, global_time, future_refs);
        l1d = make_unique<CacheLevel>(l1d_sz, block_size, l1d_as, rep_policy, global_time, future_refs);
        l2 = make_unique<CacheLevel>(l2_sz, block_size, l2_as, rep_policy, global_time, future_refs);
        l3 = make_unique<CacheLevel>(l3_sz, block_size, l3_as, rep_policy, global_time, future_refs);

        wb = make_unique<WriteBuffer>(wb_entries); 
        prefetcher = make_unique<Prefetcher>();
        
        if (vc_entries > 0) {
            victim_cache = make_unique<CacheLevel>(vc_entries * block_size, block_size, vc_entries, LRU, global_time);
        }
    }

    // handles the flow of the memory request
    void access_memory(char op, ull address) {
        global_time++;
        total_accesses++;
        
        bool is_write = (op == 'W');
        bool is_inst = (op == 'I');
        ull block_addr = address / block_size;

        // the access is either to the Instruction or to the Data L1 cache
        CacheLevel* target_l1 = is_inst ? l1i.get() : l1d.get();

        // 1. Write Buffer is checked first for recent data reads
        if (!is_write && !is_inst) { 
            if (wb->check_read(block_addr)) return; 
        }

        // 2. handles writes into the buffer
        if (is_write) {
            EvictionResult flushed = wb->push_write(block_addr);
            if (!flushed.valid) return; 
            
            block_addr = flushed.block_addr; 
            is_write = true; 
        }

        // 3. checking L1 Hits
        if (target_l1->check_and_update(block_addr, is_write)) {
            target_l1->hits++;
            return;
        }
        target_l1->misses++;

        // 4. try the victim Cache (if it's a data request and VC is enabled)
        if (!is_inst && victim_cache && victim_cache->check_and_update(block_addr, is_write)) {
            victim_cache->hits++;
            EvictionResult l1_evict = target_l1->allocate(block_addr, is_write);
            if (l1_evict.valid) victim_cache->allocate(l1_evict.block_addr, l1_evict.dirty);
            return;
        }
        if (!is_inst && victim_cache) victim_cache->misses++;
      
        // 5. Cascade Misses down to L2 and L3, managing inclusions and write-backs along the way
        if (l2->check_and_update(block_addr, false)) { 
            l2->hits++;
        } else {
            l2->misses++;
            if (l3->check_and_update(block_addr, false)) {
                l3->hits++;
            } else {
                l3->misses++;
                mem_reads++; // main Memory access required if not found in any

                EvictionResult e3 = l3->allocate(block_addr, false);
                if (e3.valid) {
                    bool dirty_l2 = false, dirty_l1d = false, dirty_l1i = false;
                    l2->invalidate(e3.block_addr, dirty_l2); 
                    l1d->invalidate(e3.block_addr, dirty_l1d); 
                    l1i->invalidate(e3.block_addr, dirty_l1i); 
                    if (write_policy == WRITE_BACK && (e3.dirty || dirty_l2 || dirty_l1d || dirty_l1i)) {
                        mem_writes++; 
                    }
                }
            }
            
            EvictionResult e2 = l2->allocate(block_addr, false);
            if (e2.valid) {
                bool dirty_l1d = false, dirty_l1i = false;
                l1d->invalidate(e2.block_addr, dirty_l1d); 
                l1i->invalidate(e2.block_addr, dirty_l1i); 
                if (write_policy == WRITE_BACK && (e2.dirty || dirty_l1d || dirty_l1i)) {
                    l3->mark_dirty(e2.block_addr); 
                }
            }
        }

        // 6. Final allocations and cleanups at the L1 level
        EvictionResult e1 = target_l1->allocate(block_addr, is_write);
        if (e1.valid) {
            if (!is_inst && victim_cache) {
                EvictionResult vc_evict = victim_cache->allocate(e1.block_addr, e1.dirty);
                if (vc_evict.valid && write_policy == WRITE_BACK && vc_evict.dirty) {
                    l2->mark_dirty(vc_evict.block_addr); 
                }
            } else if (write_policy == WRITE_BACK && e1.dirty) {
                l2->mark_dirty(e1.block_addr); 
            }
        }

        if (is_write && write_policy == WRITE_THROUGH) mem_writes++;

        // 7. the prefetcher loads adjacent data
        ull prefetch_addr = prefetcher->get_prefetch_target(block_addr);
        if (!l2->check_and_update(prefetch_addr, false)) l2->allocate(prefetch_addr, false);
    }

    // printing all the collected metrics to the terminal
    void print_stats() {
        string p_name;
        switch(rep_policy) {
            case LRU: p_name = "LRU"; break;
            case LFU: p_name = "LFU"; break;
            case FIFO: p_name = "FIFO"; break;
            case RANDOM: p_name = "Random"; break;
            case SRRIP: p_name = "SRRIP"; break;
            case NRU: p_name = "NRU"; break;
            case PLRU: p_name = "Tree-PLRU"; break;
            case OPT: p_name = "Bélády (OPT)"; break;
        }

        cout << endl << "3-Level Inclusive Cache Simulation (Split L1)" << endl;
        cout << "Policy:          " << p_name << endl;
        cout << "Write Strategy:  " << ((write_policy == WRITE_BACK) ? "Write-Back" : "Write-Through") << endl;
        cout << "Total Accesses:  " << total_accesses << endl;
        cout << endl;
        
        cout << "L1i Hits:        " << l1i->hits << endl;
        cout << "L1i Misses:      " << l1i->misses << endl;
        ull total_l1i = l1i->hits + l1i->misses;
        if (total_l1i > 0) cout << "L1i Hit Rate:    " << fixed << setprecision(2) << ((double)l1i->hits / total_l1i * 100.0) << "%\n";
        cout << endl;

        cout << "L1d Hits:        " << l1d->hits << endl;
        cout << "L1d Misses:      " << l1d->misses << endl;
        ull total_l1d = l1d->hits + l1d->misses;
        if (total_l1d > 0) cout << "L1d Hit Rate:    " << fixed << setprecision(2) << ((double)l1d->hits / total_l1d * 100.0) << "%\n";
        cout << endl;

        cout << "L2 Hits:         " << l2->hits << endl;
        cout << "L2 Misses:       " << l2->misses << endl;
        ull l1_total_misses = l1i->misses + l1d->misses;
        if (l1_total_misses > 0) cout << "L2 Hit Rate:     " << fixed << setprecision(2) << ((double)l2->hits / l1_total_misses * 100.0) << "%\n";
        cout << endl;
        
        cout << "L3 Hits:         " << l3->hits << endl;
        cout << "L3 Misses:       " << l3->misses << endl;
        if (l2->misses > 0) cout << "L3 Hit Rate:     " << fixed << setprecision(2) << ((double)l3->hits / l2->misses * 100.0) << "%\n";
        cout << endl;

        cout << "Write Buffer Fwds: " << wb->hits << endl;
        if (victim_cache) cout << "Victim Cache Hits: " << victim_cache->hits << endl;
        cout << "Memory Reads (Bus Traffic):  " << mem_reads << " block fetches\n";
        cout << "Memory Writes (Bus Traffic): " << mem_writes << " block evictions\n";
        cout << endl << endl;
    }
};

int main(int argc, char* argv[]) {
    // basic argument parsing and validation, if not correct then prints the format to the terminal
    if (argc != 15) {
        cerr << "Usage: " << argv[0] << " <block_size> "
             << "<L1i_size> <L1i_assoc> "
             << "<L1d_size> <L1d_assoc> "
             << "<L2_size> <L2_assoc> "
             << "<L3_size> <L3_assoc> "
             << "<wb_entries> <vc_entries> "
             << "<replace_pol: 0=LRU, 1=LFU, 2=FIFO, 3=Random, 4=SRRIP, 5=NRU, 6=PLRU, 7=OPT> "
             << "<write_pol: 0=WT, 1=WB> <trace_file.out>\n";
        return 1;
    }
    int block_size = stoi(argv[1]);
    int l1i_size = stoi(argv[2]);
    int l1i_assoc = stoi(argv[3]);
    int l1d_size = stoi(argv[4]);
    int l1d_assoc = stoi(argv[5]);
    int l2_size = stoi(argv[6]);
    int l2_assoc = stoi(argv[7]);
    int l3_size = stoi(argv[8]);
    int l3_assoc = stoi(argv[9]);
    int wb_entries = stoi(argv[10]);
    int vc_entries = stoi(argv[11]);
    ReplacementPolicy r_policy = static_cast<ReplacementPolicy>(stoi(argv[12]));
    WritePolicy w_policy = static_cast<WritePolicy>(stoi(argv[13]));
    string trace_file = argv[14];

    ifstream infile(trace_file);
    if (!infile.is_open()) {
        cerr << "Error: Could not open trace file " << trace_file << "\n";
        return 1;
    }
    // The Two Pass Architecture
    // Required to establish the future timeline for Bélády's Optimal Policy
    vector<pair<char, ull>> trace;
    unordered_map<ull, queue<ull>> future_refs;
    char op;
    ull address;
    ull seq = 1;
    // pass 1: pre-computing the future
    // scanning the trace file entirely to build a timeline of when each address will be needed next
    while (infile >> op >> hex >> address) {
        if (op == 'R' || op == 'W' || op == 'I') {
            trace.push_back({op, address});
            if (r_policy == OPT) {
                // recording the exact sequence number this block address is accessed
                future_refs[address / block_size].push(seq);
            }
            seq++;
        }
    }
    infile.close();
    // the cache hierarchy established with our extracted arguments
    CacheHierarchy hierarchy(block_size, l1i_size, l1i_assoc, l1d_size, l1d_assoc, 
                             l2_size, l2_assoc, l3_size, l3_assoc, 
                             wb_entries, vc_entries, r_policy, w_policy, &future_refs);

    // Pass 2: Running Simulation
    // feeding the stored operations through our initialized memory architecture
    for (const auto& t : trace) {
        hierarchy.access_memory(t.first, t.second);
    }
    hierarchy.print_stats();
    return 0;
}