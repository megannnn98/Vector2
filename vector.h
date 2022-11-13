#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>

template <typename T>
class RawMemory
{
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity)), capacity_(capacity)
    {
    }

    RawMemory(const RawMemory &) = delete;
    RawMemory &operator=(const RawMemory &rhs) = delete;

    RawMemory(RawMemory &&other) noexcept
        : buffer_{std::exchange(other.buffer_, nullptr)},
          capacity_{std::exchange(other.capacity_, 0)}
    {
    }

    RawMemory &operator=(RawMemory &&rhs) noexcept
    {
        if (this != &rhs)
        {
            Swap(rhs);
            Deallocate(rhs.buffer_);
            rhs.capacity_ = 0;
        }
        return *this;
    }

    ~RawMemory()
    {
        Deallocate(buffer_);
    }

    T *operator+(size_t offset) noexcept
    {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T *operator+(size_t offset) const noexcept
    {
        return const_cast<RawMemory &>(*this) + offset;
    }

    const T &operator[](size_t index) const noexcept
    {
        return const_cast<RawMemory &>(*this)[index];
    }

    T &operator[](size_t index) noexcept
    {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory &other) noexcept
    {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T *GetAddress() const noexcept
    {
        return buffer_;
    }

    T *GetAddress() noexcept
    {
        return buffer_;
    }

    size_t Capacity() const
    {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T *Allocate(size_t n)
    {
        return n != 0 ? static_cast<T *>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T *buf) noexcept
    {
        operator delete(buf);
    }

    T *buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector
{
public:
    Vector() = default;

    explicit Vector(size_t size)
        : data_(size), size_(size) //
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector &other)
        : data_(other.size_), size_(other.size_) //
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector &&other) noexcept
        : data_{std::move(other.data_)}, size_{other.size_}
    {
        other.size_ = 0;
    }
// TestCopyAssignment_copy_ctor fail: Assertion failed: 16 != 8 hint: C::copy_ctor != SIZE, /tmp/tmppd2csah2/main.cpp:565
// подсказка: Вы неверно реализовали оператор присваивания класса Vector. Возможно вы вызываете больше или меньше конструкторов копирования.



    Vector &operator=(const Vector &rhs)
    {
        if (this == &rhs)
            return (*this);

        if (rhs.size_ > data_.Capacity())
        {
            Vector rhs_copy(rhs);
            Swap(rhs_copy);
        }
        else
        {
            // у нас больше элементов, надо часть наших затереть
            if (rhs.size_ < size_)
            {
                auto diff = size_ - rhs.size_;
                std::uninitialized_copy_n(rhs.data_.GetAddress(), rhs.size_, data_.GetAddress());
                std::destroy_n(data_.GetAddress() + rhs.size_, diff);
            }
            // у нас меньше или столько же элементов, ничего не надо затирать
            else
            {
                std::uninitialized_copy_n(rhs.data_.GetAddress(), rhs.size_, data_.GetAddress());
            }

            size_ = rhs.size_;
        }

        return *this;
    }

    Vector &operator=(Vector &&rhs) noexcept
    {
        if (this == &rhs)
            return (*this);

        data_.Swap(rhs.data_);
        size_ = rhs.size_;
        rhs.size_ = 0;
        return *this;
    }

    void Swap(Vector &other) noexcept
    {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    size_t Size() const noexcept
    {
        return size_;
    }

    size_t Capacity() const noexcept
    {
        return data_.Capacity();
    }

    const T &operator[](size_t index) const noexcept
    {
        return const_cast<Vector &>(*this)[index];
    }

    T &operator[](size_t index) noexcept
    {
        assert(index < size_);
        return data_[index];
    }

    void Reserve(size_t new_capacity)
    {
        if (new_capacity <= data_.Capacity())
        {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
        {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else
        {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    ~Vector()
    {
        std::destroy_n(data_.GetAddress(), size_);
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T *Allocate(size_t n)
    {
        return n != 0 ? static_cast<T *>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T *buf) noexcept
    {
        operator delete(buf);
    }

    // Создаёт копию объекта elem в сырой памяти по адресу buf
    static void CopyConstruct(T *buf, const T &elem)
    {
        new (buf) T(elem);
    }

    // Вызывает деструктор объекта по адресу buf
    static void Destroy(T *buf) noexcept
    {
        buf->~T();
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};
