#include "segmentedintervallist.h"

SegmentedIntervalList::SegmentedIntervalList()
    : size(0), arrays()
{
}

SegmentedIntervalList::~SegmentedIntervalList()
{
    clear();
}

void SegmentedIntervalList::add(Collider *element, size_t index)
{
    // find the index of the element using binary search
    BoundingRadiusProjection *lowerProjection = &element->getProjection(index);
    size_t chunkWhereItWasInserted = add(lowerProjection);

    BoundingRadiusProjection *upperProjection = &element->getProjection(index + 1);
    size_t chunkWhereItWasInserted2 = add(upperProjection);

    for (size_t i = chunkWhereItWasInserted + 1; i < chunkWhereItWasInserted2; i++)
    {
        arrays[i]->addCheckpoint(element);
    }
}

void SegmentedIntervalList::remove(Collider *element, size_t index)
{
    // find the index of the element using binary search
    BoundingRadiusProjection *lowerProjection = &element->getProjection(index);
    size_t chunkWhereItWasRemoved = remove(lowerProjection);

    BoundingRadiusProjection *upperProjection = &element->getProjection(index + 1);
    size_t chunkWhereItWasRemoved2 = remove(upperProjection);

    for (size_t i = chunkWhereItWasRemoved + 1; i < chunkWhereItWasRemoved2; i++)
    {
        arrays[i]->removeCheckpoint(element);
    }
}

void SegmentedIntervalList::clear()
{
    for (size_t i = 0; i < size; i++)
    {
        delete arrays[i];
    }
    arrays.clear();
    size = 0;
}

void SegmentedIntervalList::sort()
{
    // TODO: parallelize this
    for (size_t i = 0; i < size; i++)
    {
        arrays[i]->sort();
    }
}

void SegmentedIntervalList::sort(size_t index)
{
    arrays[index]->sort();
}

size_t SegmentedIntervalList::binarySearch(BoundingRadiusProjection *element)
{
    size_t low = 0;
    size_t high = size - 1;
    size_t mid = 0;

    BoundingRadiusProjection value = *element;

    while (low <= high)
    {
        mid = (low + high) / 2;

        if (value <= *arrays[mid]->getMax() && (mid == 0 || value >= *arrays[mid - 1]->getMax()))
        {
            return mid;
        }
        else if (value >= *arrays[mid]->getMax())
        {
            low = mid + 1;
        }
        else if (value <= *arrays[mid]->getMax())
        {
            high = mid - 1;
        }
    }

    return -1;
}

size_t SegmentedIntervalList::add(BoundingRadiusProjection *element)
{
    // find the index of the element using binary search
    size_t index = binarySearch(element);

    // insert the element at the index
    if (!arrays[index]->add(element))
    {
        // if the array is full, split it
        LocalSortArray *newArray = new LocalSortArray();
        for (size_t i = MAX_SIZE / 2; i < MAX_SIZE; i++)
        {
            newArray->addWithoutSort(arrays[index]->pop());
        }
        arrays.insert(arrays.begin() + index + 1, newArray);
        size++;
    }

    return index;
}

size_t SegmentedIntervalList::remove(BoundingRadiusProjection *element)
{
    // find the index of the element using binary search
    size_t index = binarySearch(element);

    arrays[index]->remove(element);

    // if the array is empty, remove it
    if (arrays[index]->getSize() == 0)
    {
        delete arrays[index];
        arrays.erase(arrays.begin() + index);
        size--;
    }

    return index;
}