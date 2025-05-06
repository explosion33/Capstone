#include "mbed.h"
#include <vector>

#ifndef _SENSOR_EVENT_QUEUE_H_
#define _SENSOR_EVENT_QUEUE_H_



class SensorEventQueue {
    public:
        // SensorEventQueue | creates a new, empty, SensorEventQueue object
        SensorEventQueue();
        
        /* run | runs event queue, blocks forever
        */
        void run();

        /* queue | queues a function permenantly, to run periodically. Thread safe, can be used while queue is run()ing
         *     fn           (Callback<void()>) | function to be called in event
         *     frequency_ms (int)              | period to run function at, every X ms
        */
        void queue(Callback<void()> fn, int frequency_ms);
    private:
        struct SensorEvent {
            Callback<void()> fn;
            int next_exec_ms;
            int frequency_ms;
        };

        vector<SensorEvent> events;
        Mutex data_mutex;
        Timer t;
};

#endif //#ifndef _SENSOR_EVENT_QUEUE_H_