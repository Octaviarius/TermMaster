#pragma once

template <typename T>
class Singleton
{
public:
    static T& instance()
    {
        if (!_instance)
        {
            _instance = new T();
        }

        return *_instance;
    }

protected:
    Singleton()                            = default;
    Singleton(const Singleton&)            = delete;
    Singleton(Singleton&&)                 = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton& operator=(Singleton&&)      = delete;
    ~Singleton()                           = default;

private:
    static T* _instance;
};

template <typename T>
T* Singleton<T>::_instance = nullptr;
