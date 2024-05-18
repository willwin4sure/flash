#ifndef TSDEQUE_HPP
#define TSDEQUE_HPP

/**
 * @file tsdeque.hpp
 * 
 * Thread-safe wrapper around standard double-ended queue,
 * e.g. for storing messages waiting to be processed.
*/

#include <deque>
#include <mutex>

namespace flash {

/**
 * Thread-safe double-ended queue for storing arbitrary data.
*/
template <typename T>
class ts_deque {
public:
    ts_deque() = default;


    // Don't want to be able to copy the mutex.

    ts_deque(const ts_deque<T>&) = delete;
    ts_deque& operator=(const ts_deque<T>&) = delete;


    /**
     * Returns if the queue is empty.
    */
    bool empty() const {
        std::scoped_lock lock { m_mutex };  // RAII lock of mutex.
        return m_deque.empty();
    }

    /**
     * Returns the size of the queue.
    */
    size_t size() const {
        std::scoped_lock lock { m_mutex };  // RAII lock of mutex.
        return m_deque.size();
    }

    /**
     * Clear the queue.
    */
    void clear() {
        std::scoped_lock lock { m_mutex };  // RAII lock of mutex.
        m_deque.clear();
    }

    /**
     * Retrieve a const reference to the back of the queue.
    */
    const T& back() {
        std::scoped_lock lock { m_mutex };  // RAII lock of mutex.
        return m_deque.back();
    }
    
    /**
     * Retrieve a const reference to the front of the queue.
    */
    const T& front() {
        std::scoped_lock lock { m_mutex };  // RAII lock of mutex.
        return m_deque.front();
    }

    /**
     * Moves and pushes an element to the back of the queue, extending it.
     * 
     * @note javidx9's version of the code passed in a const reference to T
     * and then tried to std::move it into the back of the queue.
     * But this is wrong since it just makes a copy instead, as the object is const:
     * https://stackoverflow.com/questions/28595117/why-can-we-use-stdmove-on-a-const-object.
     * I have edited the code to take in an r-value reference instead,
     * and the move constructor gets called now.
    */
    void push_back(T&& value) {
        std::scoped_lock lock { m_mutex };  // RAII lock of mutex.
        m_deque.emplace_back(std::move(value));
    }

    /**
     * Moves and pushes an element to the front of the queue, extending it.
    */
    void push_front(T&& value) {
        std::scoped_lock lock { m_mutex };  // RAII lock of mutex.
        m_deque.emplace_front(std::move(value));
    }    

    /**
     * Pop the front of the queue, and return it.
     * 
     * Note that if the queue is empty, results in undefined behavior.
    */
    T pop_front() {
        std::scoped_lock lock { m_mutex };  // RAII lock of mutex.
        auto value = std::move(m_deque.front());
        m_deque.pop_front();
        return value;
    }

    /**
     * Pop the back of the queue, and return it.
     * 
     * Note that if the queue is empty, results in undefined behavior.
    */
    T pop_back() {
        std::scoped_lock lock { m_mutex };  // RAII lock of mutex.
        auto value = std::move(m_deque.back());
        m_deque.pop_back();
        return value;
    }

protected:
    /// Mutex must be mutable to allow const methods to lock it.
    mutable std::mutex m_mutex;

    /// The underlying double-ended queue holding the data.
    std::deque<T> m_deque;
};

} // namespace flash

#endif