#include "SensorEventQueue.h"

SensorEventQueue::SensorEventQueue() {

}
void SensorEventQueue::run() {
    t.start();

    while (true) {
        if (this->events.size() == 0) {
            ThisThread::yield();
            continue;
        }

        int curr_time = t.read_ms();

        // get event with largest elapsed time;
        int next = 0;
        int next_diff = curr_time - this->events[0].next_exec_ms;
        for (int i = 1; i<this->events.size(); i++) {         
            int diff = curr_time - this->events[i].next_exec_ms;
            if (diff > next_diff) {
                next = i;
                next_diff = diff;
            }
        }

        //printf_nb("%d, %d, %d\n", curr_time, next, next_diff);

        if (next_diff >= 0) {
            this->events[next].fn();
            this->events[next].next_exec_ms = curr_time + this->events[next].frequency_ms;
        }

        ThisThread::yield();
    }
}
void SensorEventQueue::queue(Callback<void()> fn, int frequency_ms) {
    this->data_mutex.lock();
    this->events.push_back({
        fn,
        this->t.read_ms() + frequency_ms,
        frequency_ms,
    });
    this->data_mutex.unlock();
}