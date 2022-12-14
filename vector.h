#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>

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
    using iterator = T *;
    using const_iterator = const T *;

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
            if (rhs.size_ >= size_)
            {
                auto diff = rhs.size_ - size_;
                std::copy_n(rhs.data_.GetAddress(), size_, data_.GetAddress());
                std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, diff, data_.GetAddress() + size_);
            }
            // у нас меньше или столько же элементов, ничего не надо затирать
            else
            {
                auto diff = size_ - rhs.size_;
                std::copy_n(rhs.data_.GetAddress(), rhs.size_, data_.GetAddress());
                std::destroy_n(data_.GetAddress() + rhs.size_, diff);
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

    iterator begin() noexcept
    {
        return data_.GetAddress();
    }
    iterator end() noexcept
    {
        return data_.GetAddress() + size_;
    }
    const_iterator begin() const noexcept
    {
        return data_.GetAddress();
    }
    const_iterator end() const noexcept
    {
        return data_.GetAddress() + size_;
    }
    const_iterator cbegin() const noexcept
    {
        return data_.GetAddress();
    }
    const_iterator cend() const noexcept
    {
        return data_.GetAddress() + size_;
    }

    void Resize(size_t new_size)
    {
        if (new_size == size_)
        {
            return;
        }

        if (new_size < size_)
        {
            auto diff = size_ - new_size;
            std::destroy_n(data_.GetAddress() + new_size, diff);
            size_ = new_size;
            return;
        }
        
        auto diff = new_size - size_;
        Reserve(new_size);
        std::uninitialized_value_construct_n(data_.GetAddress() + size_, diff);
        size_ = new_size;
    }

    template <typename... Args>
    T &EmplaceBack(Args &&...args)
    {
        if (size_ == data_.Capacity())
        {
            RawMemory<T> new_data((size_ == 0) ? 1 : 2 * size_);

            new (new_data.GetAddress() + size_) T(std::forward<Args>(args)...);
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)
            {
                std::uninitialized_move_n(begin(), size_, new_data.GetAddress());
            }
            else
            {
                std::uninitialized_copy_n(begin(), size_, new_data.GetAddress());
            }

            std::destroy_n(begin(), size_);
            data_.Swap(new_data);
        }
        else
        {
            new (begin() + size_) T(std::forward<Args>(args)...);
        }

        ++size_;
        return *(begin() + size_ - 1);
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args)
    {
        std::size_t index = std::distance(cbegin(), pos);

        if (size_ == data_.Capacity())
        {
            std::size_t index_to_end = std::distance(pos, cend());
            RawMemory<T> new_data((size_ == 0) ? 1 : 2 * size_);
            new(new_data.GetAddress() + index)T(std::forward<Args>(args)...);
            if constexpr (std::is_nothrow_move_constructible_v<T> || 
                            !std::is_copy_constructible_v<T>) 
            {
                try
                {
                    std::uninitialized_move_n(begin(), 
                                                index, 
                                                new_data.GetAddress());
                    // throw;
                }
                catch(...)
                {
                    std::destroy_n(begin(), index);
                }

                try
                {
                    std::uninitialized_move_n(begin() + index, 
                                                index_to_end, 
                                                new_data.GetAddress() + index + 1);
                    // throw;
                }
                catch(...)
                {
                    std::destroy_n(begin() + index, index_to_end);
                }
            }
            else 
            {
                try
                {
                    std::uninitialized_copy_n(begin(), 
                                                index, 
                                                new_data.GetAddress());
                    // throw;
                }
                catch(...)
                {
                    std::destroy_n(begin(), index);
                }

                try
                {
                    std::uninitialized_copy_n(begin() + index, 
                                                index_to_end, 
                                                new_data.GetAddress() + index + 1);
                    // throw;
                }
                catch(...)
                {
                    std::destroy_n(begin() + index, index_to_end);
                }
            }


            std::destroy_n(begin(), size_);
            data_.Swap(new_data);
            
        }
        else
        {
            if (size_ != index) {
                new (end()) T(std::forward<T>(*(end() - 1)));
                std::move_backward(begin() + index, end() - 1, end());
                data_[index] = T(std::forward<Args>(args)...);
            } else {
                new (begin() + index) T(std::forward<Args>(args)...);
            }
        }
        ++size_;
        return begin() + index;
    }

    iterator Erase(const_iterator pos)
    {
        auto index = std::distance(cbegin(), pos);
        std::move(begin() + index + 1, end(), begin() + index);
        std::destroy_n(end()-1, 1);
        --size_;

        return begin() + index;
    }

    iterator Insert(const_iterator pos, const T& value)
    {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value)
    {
        return Emplace(pos, std::move(value));
    }

    void PushBack(const T &value)
    {
        if (size_ == Capacity())
        {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data + size_) T(value);
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
        else
        {
            new (data_ + size_) T(value);
        }
        ++size_;
    }

    void PushBack(T &&value)
    {
        if (size_ == Capacity())
        {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data + size_) T(std::move(value));
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
        else
        {
            new (data_ + size_) T(std::move(value));
        }
        ++size_;
    }

    void PopBack()
    {
        data_[size_ - 1].~T();
        size_--;
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
