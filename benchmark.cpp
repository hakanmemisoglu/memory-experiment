#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <condition_variable>
#include <mutex>

#include <pthread.h>
#include <sched.h>

constexpr std::size_t SIZE_ARRAY = 1ul << 30;
constexpr std::int64_t VALUE_UPPER_LIMIT = 100;

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

  // Construct the array and wait for others.
  T* array;
  initialize_array(&array);
  barrier->wait();

  // Array processing part.
  T result = T();
  for (std::size_t i = 0; i < num_elements; ++i) {
    result += array[i];
  }

  benchmark_arg->result = result;
  return nullptr;
}

/// @brief benchmark routine.
template <typename T>
void benchmark(const std::size_t num_thread,
               const std::size_t jump_in_byte) {
  T sum = T();

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
  }

  std::fprintf(stdout, "Dummy sum: %lu\n", reinterpret_cast<std::uint64_t>(sum));
}

int main(int argc, char *argv[]) {
  std::fprintf(stdout, "Size of array %lu bytes.\n", SIZE_ARRAY);
  std::srand(std::time(nullptr));

  std::vector<std::size_t> thread_numbers = {1, 2, 4, 8, 10, 20, 30, 40};

  std::vector<std::size_t> jump_in_bytes = { 8, 16, 32, 80, 160, 320, 640,
                                             1280, 2560, 5120, 10240, 20480,
                                             40960, 81920, 163840, 327680,
                                             655360, 1310720, 2621440, 5242880,
                                             10485760, 20971520, 41943040};

  for (const auto &thread_number : thread_numbers) {
    for (const auto &jump : jump_in_bytes) {
      benchmark<std::uint8_t>(thread_number, jump);
      benchmark<std::uint16_t>(thread_number, jump);
      benchmark<std::uint32_t>(thread_number, jump);
      benchmark<std::uint64_t>(thread_number, jump);
    }
  }
}