#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <string>
#include <mutex>

static std::mutex print_mutex;

// Convert vector to string matching the os_label operator<< format
static std::string vector_to_string(const std::vector<uint8_t>& vec) {
    std::string s = "[";
    for (size_t i = 0; i < vec.size(); i++) {
        s += std::to_string(vec[i]);
        if (i != vec.size() - 1) s += ", ";
    }
    s += "]";
    return s;
}

// Verification utility
int verify_label(std::vector<uint8_t> expected, const char* context) {
    __cilkrts_os_label os_l = __cilkrts_get_os_label();
    std::vector<uint8_t> actual = os_l.label.to_vector();
    
    if (actual != expected) {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cerr << "WARNING: Label mismatch in " << context << "!\n"
                  << "  Expected: " << vector_to_string(expected) << "\n"
                  << "  Actual:   " << vector_to_string(actual) << "\n";
        return 1;
    }
    usleep(500);
    return 0;
}

std::vector<uint8_t> append_label(std::vector<uint8_t> base, std::vector<uint8_t> ext) {
    base.insert(base.end(), ext.begin(), ext.end());
    return base;
}

std::vector<uint8_t> sync_label(std::vector<uint8_t> base, int /*conts*/) {
    // cilk_sync drops all continuation zeros added by spawns in this function,
    // effectively returning to the base label, and then increments the last element by 2.
    if (!base.empty()) {
        base.back() += 2;
    }
    return base;
}

// ---------------------------------------------------------
// Test Cases
// ---------------------------------------------------------

int child_func(std::vector<uint8_t> base) {
    // Child gets '1' appended
    return verify_label(append_label(base, {1}), "child_func");
}

int test_single_spawn(std::vector<uint8_t> base) {
    int fails = verify_label(base, "test_single_spawn entry");
    
    int child_fails = cilk_spawn child_func(base);
    
    // Continuation gets '0' appended
    fails += verify_label(append_label(base, {0}), "test_single_spawn continuation");
    
    cilk_sync;
    
    // After sync, conts=1, drops 1 level and adds 2 to parent
    fails += verify_label(sync_label(base, 1), "test_single_spawn after sync");
    return fails + child_fails;
}

int nested_child(std::vector<uint8_t> base) {
    return verify_label(append_label(base, {1}), "nested_child");
}

int outer_child(std::vector<uint8_t> base) {
    int fails = verify_label(append_label(base, {1}), "outer_child entry");
    
    int child_fails = cilk_spawn nested_child(append_label(base, {1}));
    
    fails += verify_label(append_label(base, {1, 0}), "outer_child continuation");
    
    cilk_sync;
    
    fails += verify_label(sync_label(append_label(base, {1}), 1), "outer_child after sync");
    return fails + child_fails;
}

int test_nested_spawn(std::vector<uint8_t> base) {
    int fails = verify_label(base, "test_nested_spawn entry");
    
    int child_fails = cilk_spawn outer_child(base);
    
    fails += verify_label(append_label(base, {0}), "test_nested_spawn continuation");
    
    cilk_sync;
    
    fails += verify_label(sync_label(base, 1), "test_nested_spawn after sync");
    return fails + child_fails;
}

// Wrapper for checking label to avoid complex macro expansion in cilk_spawn
int check_loop_child(std::vector<uint8_t> base, int i) {
    std::string msg = "loop child " + std::to_string(i);
    // Continuation '0' is appended i times, and then child '1' is appended
    std::vector<uint8_t> expected = base;
    for (int j = 0; j < i; j++) expected.push_back(0);
    expected.push_back(1);
    return verify_label(expected, msg.c_str());
}

int test_loop_spawn(std::vector<uint8_t> base) {
    int fails = verify_label(base, "test_loop_spawn entry");
    int child_fails[3] = {0};
    
    // Test multiple spawns from the same parent context
    for (int i = 0; i < 3; i++) {
        child_fails[i] = cilk_spawn check_loop_child(base, i);
        // Continuation gets another '0'
        std::vector<uint8_t> expected = base;
        for (int j = 0; j <= i; j++) expected.push_back(0);
        
        std::string msg2 = "loop continuation " + std::to_string(i);
        fails += verify_label(expected, msg2.c_str());
    }
    
    cilk_sync; // Wait for children
    
    for (int i = 0; i < 3; i++) fails += child_fails[i];
    
    fails += verify_label(sync_label(base, 3), "test_loop_spawn after sync");
    return fails;
}

int test_multiple_syncs(std::vector<uint8_t> base) {
    int fails = verify_label(base, "test_multiple_syncs entry");
    
    int cf1 = cilk_spawn verify_label(append_label(base, {1}), "test_multiple_syncs first child");
    fails += verify_label(append_label(base, {0}), "test_multiple_syncs first continuation");
    cilk_sync;
    fails += cf1;
    
    std::vector<uint8_t> after_sync_1 = sync_label(base, 1);
    fails += verify_label(after_sync_1, "test_multiple_syncs after first sync");
    
    int cf2 = cilk_spawn verify_label(append_label(after_sync_1, {1}), "test_multiple_syncs second child");
    fails += verify_label(append_label(after_sync_1, {0}), "test_multiple_syncs second continuation");
    cilk_sync;
    fails += cf2;
    
    std::vector<uint8_t> after_sync_2 = sync_label(after_sync_1, 1);
    fails += verify_label(after_sync_2, "test_multiple_syncs after second sync");
    
    return fails;
}

int main() {
    std::cerr << "Starting Cilk OS Label Determinism Tests...\n";
    int total_fails = 0;
    
    // Main starts with base label [0]
    std::vector<uint8_t> base = {0};
    total_fails += verify_label(base, "main entry");
    
    // Each test is spawned and synced to keep the label clean for the next test
    int cf1 = cilk_spawn test_single_spawn(append_label(base, {1}));
    total_fails += verify_label(append_label(base, {0}), "main after test_single_spawn continuation");
    cilk_sync;
    base = sync_label(base, 1);
    total_fails += cf1 + verify_label(base, "main after test_single_spawn");
    
    int cf2 = cilk_spawn test_nested_spawn(append_label(base, {1}));
    total_fails += verify_label(append_label(base, {0}), "main after test_nested_spawn continuation");
    cilk_sync;
    base = sync_label(base, 1);
    total_fails += cf2 + verify_label(base, "main after test_nested_spawn");
    
    int cf3 = cilk_spawn test_loop_spawn(append_label(base, {1}));
    total_fails += verify_label(append_label(base, {0}), "main after test_loop_spawn continuation");
    cilk_sync;
    base = sync_label(base, 1);
    total_fails += cf3 + verify_label(base, "main after test_loop_spawn");
    
    int cf4 = cilk_spawn test_multiple_syncs(append_label(base, {1}));
    total_fails += verify_label(append_label(base, {0}), "main after test_multiple_syncs continuation");
    cilk_sync;
    base = sync_label(base, 1);
    total_fails += cf4 + verify_label(base, "main after test_multiple_syncs");
    
    std::cerr << "-------------------------------------------\n";
    std::cerr << "Test Summary:\n";
    std::cerr << "  Incorrect: " << total_fails << "\n";
    
    if (total_fails == 0) {
        std::cerr << "SUCCESS! All labels matched expectations.\n";
    } else {
        std::cerr << "FAILURE! Some labels did not match expectations.\n";
    }
    
    return total_fails > 0 ? 1 : 0;
}
