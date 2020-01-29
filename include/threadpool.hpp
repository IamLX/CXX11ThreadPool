#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>

#include "blockingqueue.hpp"
#include "runnable.hpp"

/// @brief TimeUnit
enum class TimeUnit {
    nanoseconds = 0,
    microseconds = 1,
    milliseconds = 2,
    seconds = 3,
    minutes = 4,
    hours = 5,
    days = 6
};

using days = std::chrono::duration<int, std::ratio<3600 * 24>>;

/// @brief TimeUnitArray store TimeUnit
///
/// @param std::chrono::nanoseconds nanoseconds
/// @param std::chrono::microseconds microseconds
/// @param std::chrono::milliseconds milliseconds
/// @param std::chrono::seconds seconds
/// @param days days
///
/// @return null
std::tuple<> TimeUnitArray(
    std::chrono::nanoseconds,
    std::chrono::microseconds,
    std::chrono::milliseconds,
    std::chrono::seconds,
    std::chrono::hours
    days
);

class RejectedExecutionHandler {
    public:
        RejectedExecutionHandler()          = default;
        virtual ~RejectedExecutionHandler() {}

    public:
        virtual void rejectedExecution(const Runnable& r) {}
};

class ThreadPoolExecutor {
    public:
        explicit ThreadPoolExecutor(int32_t corePoolSize,
                                    int32_t maxPoolSize,
                                    long keepAliveTime,
                                    TimeUnit unit,
                                    BlockingQueue<Runnable> workQueue,
                                    RejectedExecutionHandler handler);

        explicit ThreadPoolExecutor(int32_t corePoolSize,
                                    int32_t maxPoolSize,
                                    long keepAliveTime,
                                    TimeUnit unit,
                                    BlockingQueue<Runnable>* workQueue);
        virtual ~ThreadPoolExecutor ();

    public:
        /**
         * @brief allowsCoreThreadTimeOut 判断是否允许core thread超时并且在没有任务时终止
         *
         * @return ture - 允许core thread超时并且在没有任务时终止
         */
        bool allowsCoreThreadTimeOut() const;

        /**
         * @brief allowCoreThreadTimeOut 设置是否允许core thread超时并且在没有任务时终止
         *
         * @param value ture或false
         */
        void allowCoreThreadTimeOut(bool value);

        /**
         * @brief interruptIdleWorkers 终止所有线程,不管是否空闲
         */
        void interruptIdleWorkers();

        /**
         * @brief interruptIdleWorkers 中断等待任务(空闲)线程
         *
         * @param onlyOne 是否至终止一个空闲线程
         */
        void interruptIdleWorkers(bool onlyOne);

        /**
         * @brief execute 在将来某个时候执行给定的任务,无返回值,
         *                任务可以在新线程或现有的合并的线程中执行,
         *                向任务队列提交的是任务副本
         *
         * @param command 要执行的任务(Runnable或函数或lambda)
         *
         * @return          true - 添加成功
         */
        bool execute(const Runnable command);

        /**
         * @brief submit 在将来某个时候执行给定的任务,
         *               任务可以在新线程或现有的合并的线程中执行,
         *               可以有返回值,向任务队列提交的是任务副本
         *
         * @param f 要提交的任务(Runnable或函数或lambda)
         *
         * @return res 任务返回值的future
         */
        template<typename F>
        std::future<typename std::result_of<F()>::type>
        submit(F f, bool core = false) {
            for(;;) {
                int c = _ctl.load();
                int rs = runStateOf(c);

                if (rs >= SHUTDOWN)
                    throw std::logic_error("thread pool is SHUTDOWN");

                int wc = workerCountOf(c);
                if (wc >= CAPACITY or wc >= (core ? _corePoolSize : _maxPoolSize)) {
                    using result_type = typename std::result_of<F()>::type;
                    std::packaged_task<result_type()> task(std::move(f));
                    std::future<result_type> res(task.get_future());
                    _workQueue->put(std::move(task));
                    return res;
                } else {
                    c = _ctl.load();  // Re-read ctl
                    if(runStateOf(c) != rs)
                        continue;

                    rs = runStateOf(c);
                    std::lock_guard<std::mutex> lock(_mutex);
                    if(rs < SHUTDOWN) {
                        if(!compareAndIncrementWorkerCount(c))
                            continue;
                        _threads.push_back(std::thread(&ThreadPoolExecutor::workerThread, this));
                        using result_type = typename std::result_of<F()>::type;
                        std::packaged_task<result_type()> task(std::move(f));
                        std::future<result_type> res(task.get_future());
                        _workQueue->put(std::move(task));
                        return res;
                    }
                }
            }
        }

        /**
         * @brief toString 返回标识此池的字符串及其状态，包括运行状态和估计的Worker和任务计数的指示
         *
         * @return 一个标识这个池的字符串，以及它的状态
         */
        std::string toString() const;

        /**
         * @brief getActiveCount 返回正在执行任务的线程的大概数量
         *
         * @return 线程数
         */
        int getActiveCount() const;

        /**
         * @brief setKeepAliveTime 设置线程在终止之前可能保持空闲的时间限制。 如果存在超过当前在池中的线程核心数量，则在等待这段时间而不处理任务之后，多余的线程将被终止。 这将覆盖在构造函数中设置的任何值
         *
         * @param time 等待的时间
         * @param unit time参数的时间单位
         */
        void setKeepAliveTime(long time, TimeUnit unit);

        /**
         * @brief setMaxPoolSize 设置允许的最大线程数。 这将覆盖在构造函数中设置的任何值。 如果新值小于当前值，则过多的现有线程在下一个空闲时将被终止
         *
         * @param maxPoolSize 新的最大值
         */
        void setMaxPoolSize(int maxPoolSize);


        /**
         * @brief getPoolSize 返回池中当前的线程数
         *
         * @return 线程数
         */
        int getPoolSize() const;

        /**
         * @brief prestartAllCoreThreads 启动所有核心线程，导致他们等待工作。 这将覆盖仅在执行新任务时启动核心线程的默认策略
         *
         * @return 线程数已启动
         */
        int prestartAllCoreThreads();

        /**
         * @brief getCorePoolSize 返回核心线程数
         *
         * @return 核心线程数
         */
        int getCorePoolSize() const;

        /**
         * @brief setCorePoolSize 设置核心线程数。 这将覆盖在构造函数中设置的任何值。 如果新值小于当前值，则过多的现有线程在下一个空闲时将被终止。 如果更大，则如果需要，新线程将被启动以执行任何排队的任务。
         *
         * @return void
         */
        void setCorePoolSize(int32_t corePoolSize);

        /**
         * @brief shutdown 不在接受新任务,并且在所有任务执行完后终止线程池
         */
        void shutdown();

        /**
         * @brief isShutDown 判断线程池是否shutdown
         *
         * @return
         */
        bool isShutDown();

        /**
         * @brief isTerminated 判断线程池是否terminated
         *
         * @return
         */
        bool isTerminated();

        //打包和拆包ctl
        inline static int runStateOf(int32_t c)     {
            return c & ~CAPACITY;
        }
        inline static int workerCountOf(int32_t c)  {
            return c & CAPACITY;
        }
        static int32_t ctlOf(int32_t rs, int32_t wc) {
            return rs | wc;
        }

    private :
        /**
         * @brief addWorker 将任务添加到队列
         *
         * @param firstTask 任务
         * @param core      是否使用核心线程
         *
         * @return          true - 添加成功
         */
        bool addWorker(const Runnable firstTask, bool core);

        /**
         * @brief advanceRunState 改变线程池状态
         *
         * @param targetState 目标状态
         */
        void advanceRunState(int32_t targetState);

        /**
         * @brief start 开启线程池
         */
        void start();

        /**
         * @brief workerThread 线程池主循环
         */
        void workerThread();

        /**
         * @brief reject 将任务抛弃
         *
         * @param command 要抛弃的任务
         */
        inline void reject(const Runnable & command) {
            _handler->rejectedExecution(command);
        }

        static bool runStateLessThan(int c, int s) {
            return c < s;
        }

        static bool runStateAtLeast(int c, int s) {
            return c >= s;
        }

        static bool isRunning(int c) {
            return c < SHUTDOWN;
        }

        /*
         *  @brief Attempts to CAS-increment the workerCount field of ctl.
         *
        */
        bool compareAndIncrementWorkerCount(int expect) {
            return _ctl.compare_exchange_strong(expect, expect + 1);
        }

        /*
         *   @brief Attempts to CAS-decrement the workerCount field of ctl.
        */
        bool compareAndDecrementWorkerCount(int expect) {
            return _ctl.compare_exchange_strong(expect, expect - 1);
        }

        /**
         *   @brief Decrements the workerCount field of ctl. This is called only on
         *    abrupt termination of a thread (see processWorkerExit). Other
         *    decrements are performed within getTask.
         *
        */
        void decrementWorkerCount() {
            do {

            } while (! compareAndDecrementWorkerCount(_ctl.load()));
        }

    private:
        int											  _corePoolSize;
        int											  _maxPoolSize;
        long										  _keepAliveTime;
        size_t										  _threadNum;
        TimeUnit									  _unit;
        mutable std::mutex                                    _mutex;
        volatile bool                                 _allowCoreThreadTimeOut = false;
        std::atomic_int32_t                           _ctl;
        std::vector<std::thread>                      _threads;
        std::unique_ptr<BlockingQueue<Runnable>>	  _workQueue;
        std::unique_ptr<RejectedExecutionHandler>	  _handler;
        /// runState is stored in the high-order bits
        /// 我们可以看出有5种runState状态，证明至少需要3位来表示runState状态
        /// 所以高三位就是表示runState了
        static const int32_t						  COUNT_BITS;
        static const int32_t						  CAPACITY;
        static const int32_t                          RUNNING;        ///运行状态
        static const int32_t                          SHUTDOWN;       ///不再接受新任务
        static const int32_t                          STOP;           ///不再接受新任务,直接(不再执行)销毁旧任务
        static const int32_t                          TIDYING;        ///所有任务已经终止,任务队列为空
        static const int32_t                          TERMINATED;     ///线程池关闭
};

const int32_t ThreadPoolExecutor::COUNT_BITS = sizeof(COUNT_BITS) * 8 - 3;
const int32_t ThreadPoolExecutor::CAPACITY   = (1 << COUNT_BITS) - 1;
const int32_t ThreadPoolExecutor::RUNNING    = -1 << COUNT_BITS;
const int32_t ThreadPoolExecutor::SHUTDOWN   = 0 << COUNT_BITS;
const int32_t ThreadPoolExecutor::STOP       = 1 << COUNT_BITS;
const int32_t ThreadPoolExecutor::TIDYING    =  2 << COUNT_BITS;
const int32_t ThreadPoolExecutor::TERMINATED = 3 << COUNT_BITS;

#endif /* THREADPOOL_H */
