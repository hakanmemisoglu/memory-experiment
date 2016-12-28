#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <vector>

#include <pthread.h>
#include <sched.h>

constexpr std::size_t SIZE_ARRAY = 1ul << 30;
constexpr std::uint64_t VALUE_UPPER_LIMIT = 100ul;

struct Barrier {
  Barrier(const std::size_t num_threads)
    : m_count(num_threads) {
  }

  void wait() {
    std::unique_lock<std::mutex> lock(m_mutex);
    --m_count;
    if (m_count == 0) {
      m_cond.notify_all();
    }
    else {
      m_cond.wait(lock,
                  [this] {
                    return m_count == 0;
                  });
    }
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_cond;
  std::size_t m_count;
};

template <typename T>
struct BenchmarkArg {
  T result;
  std::size_t num_elements;
  std::size_t step_size;
  std::size_t thread_bandwidth;
  Barrier *barrier;
};

/// @brief Allocates area and initialize the values for the given array.
template <typename T>
void initialize_array(T **array) {
  // Be defensive in case of SIZE_ARRAY is not exact multiple of size of element.
  constexpr std::size_t elements_needed = SIZE_ARRAY / sizeof(T);
  std::fprintf(stdout, "Elements needed: %lu\n", elements_needed);

  *array = reinterpret_cast<T*>(std::malloc(elements_needed * sizeof(T)));

  for (std::size_t i = 0; i < elements_needed; ++i) {
    (*array)[i] = std::rand() % VALUE_UPPER_LIMIT;
  }
}

template <typename T>
void free_array(T *array) {
  std::free(array);
}

template <typename T>
void* benchmark_function(void *arg) {
  BenchmarkArg<T>* benchmark_arg = reinterpret_cast<BenchmarkArg<T>*>(arg);
  Barrier *barrier = benchmark_arg->barrier;
  std::size_t num_elements = benchmark_arg->num_elements;
  std::size_t step_size = benchmark_arg->step_size;
  // Construct the array and wait for others.
  T* array;
  initialize_array(&array);
  barrier->wait();

  // Array processing part.
  T result = T();
  // Start timer.
  auto begin_time = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < num_elements; i += step_size) {
    result += array[i];
  }
  // End timer.
  auto end_time = std::chrono::steady_clock::now();

  std::chrono::nanoseconds elapsed_ns = end_time - begin_time;
  double elapsed_seconds = static_cast<double>(elapsed_ns.count()) / std::pow(10, 9);

  std::size_t bytes_touched = num_elements * sizeof(T);
  double memory_bandwidth = bytes_touched / elapsed_seconds;

  benchmark_arg->result = result;
  benchmark_arg->thread_bandwidth = memory_bandwidth;
  free_array(array);
  return nullptr;
}

/// @brief benchmark routine.
template <typename T>
void benchmark(const std::size_t num_thread,
               const std::size_t jump_in_byte) {
  T sum = T();
  double total_memory_bandwidth = 0.0;

  const std::size_t elements_needed = SIZE_ARRAY / sizeof(T);
  std::fprintf(stdout, "Elements: %lu\n", elements_needed);

  const std::size_t elements_jumped = jump_in_byte / sizeof(T);
  std::fprintf(stdout, "Elements jumped: %lu\n", elements_jumped);

  // Pthread configuration
  std::vector<pthread_attr_t> thread_attributes(num_thread);
  std::vector<pthread_t> threads(num_thread);

  cpu_set_t cpu_set;

  for (std::size_t t = 0; t < num_thread; ++t) {
    CPU_ZERO(&cpu_set);
    CPU_SET(t, &cpu_set);
    pthread_attr_init(&thread_attributes[t]);
    pthread_attr_setaffinity_np(&thread_attributes[t], sizeof(cpu_set), &cpu_set);
  }

  // Construct barrier
  Barrier barrier(num_thread);

  std::vector<BenchmarkArg<T>> thread_args(num_thread);
  for (std::size_t t = 0; t < num_thread; ++t) {
    thread_args[t].num_elements = elements_needed;
    thread_args[t].step_size = elements_jumped;
    thread_args[t].barrier = &barrier;
  }

  for (std::size_t t = 0; t < num_thread; ++t) {
    pthread_create(&threads[t], &thread_attributes[t], benchmark_function<T>, &thread_args[t]);
  }

  for (std::size_t t = 0; t < num_thread; ++t) {
    pthread_join(threads[t], nullptr);
  }

  for (std::size_t t = 0; t < num_thread; ++t) {
    sum += thread_args[t].result;
    total_memory_bandwidth += thread_args[t].thread_bandwidth;
  }

  std::fprintf(stdout, "Dummy sum: %lu\n", static_cast<std::uint64_t>(sum));
  std::fprintf(stdout, "Memory bandwidth: %f GBps\n", total_memory_bandwidth);
}

int main(int argc, char *argv[]) {
  std::fprintf(stdout, "Size of array %lu bytes.\n", SIZE_ARRAY);
  std::srand(std::time(nullptr));

  std::size_t word_size = -1;

  if (argc != 2) {
    std::fprintf(stdout, "Wrong argument!\n");
    std::exit(1);
  }
  else {
    word_size = static_cast<std::size_t>(std::atol(argv[1]));
  }

  std::vector<std::size_t> thread_numbers = {1, 2, 4, 8, 10, 20, 30, 40};

  std::vector<std::size_t> jump_in_bytes = { 8, 16, 32, 80, 160, 320, 640,
                                             1280, 2560, 5120, 10240, 20480,
                                             40960, 81920, 163840, 327680,
                                             655360, 1310720, 2621440, 5242880,
                                             10485760, 20971520, 41943040};

  for (const auto &thread_number : thread_numbers) {
    std::fprintf(stdout, "Thread number: %lu\n", thread_number);

    for (const auto &jump : jump_in_bytes) {

      switch (word_size) {
      case 1: benchmark<std::uint8_t>(thread_number, jump); break;
      case 2: benchmark<std::uint16_t>(thread_number, jump); break;
      case 3: benchmark<std::uint32_t>(thread_number, jump); break;
      case 4: benchmark<std::uint64_t>(thread_number, jump); break;
      default: fprintf(stdout, "Wrong argument!\n"); break;
      }
    }
  }
}
