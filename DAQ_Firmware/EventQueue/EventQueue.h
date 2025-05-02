#include "mbed.h"

#ifndef _EVENT_QUEUE_H_
#define _EVENT_QUEUE_H_

struct EventList {
    Callback<void()> fn;
    int priority;
    EventList* next;
    EventList* prev;
};

class MyEventQueue {
    private:
        bool running;
        EventList* head;
        EventList* back;

        int len;
        EventList* empty;

        EventList* next();
    public:
        MyEventQueue();
        ~MyEventQueue();

        bool post(Callback<void()> fn, int priority);
        void run();
        void stop();
};

#endif // _EVENT_QUEUE_H_