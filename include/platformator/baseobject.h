#pragma once

#include <atomic>
#include <algorithm>

class BaseObject
{
protected:
    BaseObject() : id(next_id++)
    {
    }

    BaseObject(int loaded_id) : id(loaded_id)
    {
        updateCounter(loaded_id);
    }

public:
    int getId() const
    {
        return id;
    }

    void setId(int value)
    {
        id = value;
        updateCounter(value); // Also protect manually changed IDs
    }

protected:
    int id;

private:
    static inline std::atomic<int> next_id{1}; 

    static void updateCounter(int loaded_id)
    {
        int current = next_id.load();
        
        // Loop ensures safety if multiple threads load data simultaneously
        while (loaded_id >= current)
        {
            if (next_id.compare_exchange_weak(current, loaded_id + 1))
            {
                break; // Successfully updated!
            }
        }
    }
};

class Asset
{
protected:
    Asset(std::string filePath) : filePath(std::move(filePath))
    {
    }

    Asset() : filePath("")
    {
    }

public:
    const std::string &getFilePath() const
    {
        return filePath;
    }

    void setFilePath(const std::string &newFilePath)
    {
        filePath = newFilePath;
    }

private:
    std::string filePath;
};