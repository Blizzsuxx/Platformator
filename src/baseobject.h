#pragma once

#include <limits>
#include <random>

class BaseObject
{
protected:
    BaseObject() : id(generateId())
    {
    }

    BaseObject(int id) : id(id)
    {
    }

public:
    int getId() const
    {
        return id;
    }

    void setId(int value)
    {
        id = value;
    }

protected:
    int id;

private:
    static int generateId()
    {
        thread_local std::mt19937_64 generator(std::random_device{}());
        std::uniform_int_distribution<int> distribution(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        return distribution(generator);
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
