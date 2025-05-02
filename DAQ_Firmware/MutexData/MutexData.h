#include "mbed.h"

#ifndef _MutexData_H_
#define _MutexData_H_

template <typename F>
class MutexGuard;

template <typename F>
class MutexData {
    private:
        float f;
        volatile bool locked;
    public:
        MutexData(float f) {
            this->f = f;
            this->locked = false;
        }
        MutexGuard<F> lock() {
            while (true) {
                core_util_critical_section_enter();
                if (!this->locked) {
                    this->locked = true;
                    core_util_critical_section_exit();
                    break;
                }
                core_util_critical_section_exit();
                ThisThread::yield();
            }

            return MutexGuard<F>(this, &this->f);
        }
        void unlock() {
            core_util_critical_section_enter();
            this->locked = false;
            core_util_critical_section_exit();
        }
};

template <typename F>
class MutexGuard {
    private:
        MutexData<F>* data;
        F* ptr;
    public:
        MutexGuard(MutexData<F>* data, float* f) {
            this->ptr = f;
            this->data = data;
        }
        ~MutexGuard() { data->unlock(); }
        void unlock() { data->unlock(); ptr = nullptr; data = nullptr; }
        F& operator*()  { return *ptr; }
};

#endif // _MutexData_H_