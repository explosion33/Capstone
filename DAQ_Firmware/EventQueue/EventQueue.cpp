#include "EventQueue.h"
#include <cstdio>

MyEventQueue::MyEventQueue() {
    empty = new EventList {
        nullptr,
        10,
        nullptr,
        nullptr,
    };

    EventList* curr = empty;
    for (int i = 0; i < 4; i++) {
        curr->next = new EventList {
            nullptr,
            10,
            nullptr,
            curr,
        };
        curr = curr->next;
    }

    this->len = 5;
}
MyEventQueue::~MyEventQueue() {}

bool MyEventQueue::post(Callback<void ()> fn, int priority) {
    //core_util_critical_section_enter();
    
    if (this->len <= 1) {
        //core_util_critical_section_exit();
        return false;
    }

    EventList* node = this->empty;
    this->empty = this->empty->next;
    this->len --;

    node->fn = fn;
    node->priority = priority;
    node->prev = this->back;
    node->next = nullptr;

    if (this->head == nullptr) {
        this->head = node;
        this->back = head;
    }
    else {
        this->back->next = node;
        this->back = node;
    }
    //core_util_critical_section_exit();

    return true;
}

EventList* MyEventQueue::next() {
    //core_util_critical_section_enter();
    if (this->head == nullptr) {
        return nullptr;
    }

    EventList* curr = this->head->next;
    EventList* low = this->head;

    while (curr != nullptr) {
        if (curr->priority < low->priority) {
            low = curr;
        }
        curr = curr->next;
    }

    if (low->prev != nullptr) {
        low->prev->next = low->next;
    } else {
        this->head = low->next; // low was head
    }

    if (low->next != nullptr) {
        low->next->prev = low->prev;
    } else {
        this->back = low->prev; // low was back
    }

    //core_util_critical_section_exit();

    return low;
}

void MyEventQueue::run() {
    this->running = true;
    while (this->running) {
        EventList* next = this->next();

        if (next == nullptr) {
            ThisThread::yield();
            continue;
        }

        if (next->fn != nullptr) {
            next->fn();
        }

        next->prev = nullptr;
        next->next = this->empty;
        next->fn = nullptr;
        this->empty->prev = next;
        this->empty = next;
        this->len ++;

        ThisThread::yield();
    }
}

void MyEventQueue::stop() {
    core_util_critical_section_enter();
    this->running = false;
    core_util_critical_section_exit();
}