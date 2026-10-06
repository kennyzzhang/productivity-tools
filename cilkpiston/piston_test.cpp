#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include "piston.h"

int main() {
  piston_t test;
  std::vector<piston_t::iterator> iterators = {test.end()};
  std::vector<int> arr = {0};
//  std::mt19937_64 rng{std::random_device{}()};
  std::mt19937_64 rng{};
  std::uniform_int_distribution dist(1, 99);

  const auto check = [&]() {
    int runningmin = std::numeric_limits<int>::max();
    for (int i = arr.size(); --i >= 0; ) {
      runningmin = std::min(runningmin, arr[i]);
      assert(runningmin == test.rmq(iterators[i]));
    }
  };

  for (int i = 1; i < 10; i++) {
    if (dist(rng) <= 50) {
	test.append_inc();
	iterators.push_back(test.end());
	arr.push_back(arr.back() + 1);
    } else {
	test.append_dec();
	iterators.push_back(test.end());
	arr.push_back(arr.back() - 1);
    }
  }

  std::cout << "finished generating" << std::endl;

  int ret = 0;
  auto beg = std::chrono::steady_clock::now();
  int iters = 100000000;
  for (int j = 0; j < iters; j++) {
    for (int i = 0; i < arr.size(); i++) {
      ret += test.rmq(iterators[i]);
    }
  }
  auto end = std::chrono::steady_clock::now();
  std::cout << std::chrono::duration<long double>(end - beg).count() << " for " << iters << "*" << arr.size() << std::endl;
  std::cout << ret << std::endl;

  /*
  for (int j = 0; j < arr.size(); j++) {
    std::cout << test.rmq(iterators[j]) << " ";
  }
  std::cout << std::endl;
  */

//  check();

  /*
    if (i % 1 == 0) {
      std::cout << i << ": ";

    }
    */


}
