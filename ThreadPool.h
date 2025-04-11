#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>

/**
 * @brief ThreadPool - A simple C++ thread pool implementation for parallel task execution
 *
 * A thread pool is a design pattern that maintains a group of worker threads that are ready
 * to execute tasks. Instead of creating a new thread for each task, which is expensive,
 * the thread pool reuses existing threads to execute multiple tasks.
 *
 * Key benefits:
 * - Reduces overhead of thread creation/destruction
 * - Limits the number of concurrent threads to avoid system overload
 * - Provides a simple interface for submitting tasks and retrieving results
 *
 * This implementation uses modern C++ features including:
 * - std::thread for managing worker threads
 * - std::mutex and std::condition_variable for thread synchronization
 * - std::future for returning results from asynchronous tasks
 * - std::function for type-erased task storage
 */

class ThreadPool {
public:
    /**
     * @brief Constructs a new ThreadPool with the specified number of worker threads
     *
     * @param numThreads The number of worker threads to create in the pool
     *
     * The constructor initializes the thread pool by calling the private start() method,
     * which creates the specified number of worker threads. Each thread will continuously
     * wait for and execute tasks from the shared task queue.
     */
    explicit ThreadPool(size_t numThreads) {
        start(numThreads);
    }

    /**
     * @brief Destroys the ThreadPool, stopping all worker threads
     *
     * The destructor calls the private stop() method, which:
     * 1. Sets the stop flag to signal threads to exit
     * 2. Wakes up all waiting threads
     * 3. Joins all threads to ensure they complete cleanly
     *
     * Any pending tasks in the queue will not be executed after destruction.
     */
    ~ThreadPool() {
        stop();
    }

    /**
     * @brief Submits a task to the thread pool and returns a future for the result
     *
     * @tparam F The type of the function to execute
     * @tparam Args The types of the arguments to pass to the function
     * @param f The function to execute
     * @param args The arguments to pass to the function
     * @return A std::future that will contain the result of the function call
     * @throws std::runtime_error if the thread pool has been stopped
     *
     * This method:
     * 1. Packages the function and its arguments into a task
     * 2. Creates a future to store the result
     * 3. Adds the task to the queue
     * 4. Notifies a waiting worker thread that a task is available
     *
     * The task will be executed by one of the worker threads when it becomes available.
     * The caller can use the returned future to wait for the result or check if it's ready.
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

        // Create a packaged task that binds the function with its arguments
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // Get the future from the packaged task before it's moved into the queue
        std::future<return_type> res = task->get_future();
        {
            // Lock the queue to safely add the new task
            std::unique_lock<std::mutex> lock(queue_mutex);

            // Don't allow enqueueing after stopping the pool
            if (stop_flag) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

private:
    // Need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // The task queue
    std::queue<std::function<void()>> tasks;

    // Synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop_flag;

    void start(size_t numThreads) {
        stop_flag = false;
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back(
                [this] {
                    while (true) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(this->queue_mutex);
                            this->condition.wait(lock,
                                [this] { return this->stop_flag || !this->tasks.empty(); });
                            if (this->stop_flag && this->tasks.empty())
                                return;
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }
                        task();
                    }
                }
            );
        }
    }

    void stop() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop_flag = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
    }
};