#include "segmentedintervallist.h"

SegmentedIntervalList::SegmentedIntervalList()
    : chunks(), size(1)
{
    chunks.push_back(new LocalSortArray());
}

SegmentedIntervalList::~SegmentedIntervalList()
{
    clear();
}

void SegmentedIntervalList::add(BoundingRadiusProjectionAxis *axis)
{
    BoundingRadiusProjection *lowerProjection = axis->getMin();
    size_t chunkWhereItWasInserted = add(lowerProjection);

    BoundingRadiusProjection *upperProjection = axis->getMax();
    size_t chunkWhereItWasInserted2 = add(upperProjection);

    for (size_t i = chunkWhereItWasInserted; i < chunkWhereItWasInserted2; i++)
    {
        chunks[i]->addCheckpoint(lowerProjection->getCollider());
    }
}

void SegmentedIntervalList::remove(BoundingRadiusProjectionAxis *axis)
{
    BoundingRadiusProjection *lowerProjection = axis->getMin();
    LocalSortArray *chunkWhereItWasRemoved = remove(lowerProjection);

    BoundingRadiusProjection *upperProjection = axis->getMax();
    LocalSortArray *chunkWhereItWasRemoved2 = remove(upperProjection);

    size_t chunkWhereItWasRemovedIndex = binarySearch(chunkWhereItWasRemoved);
    LocalSortArray *currentChunk = chunkWhereItWasRemoved;

    while (currentChunk != chunkWhereItWasRemoved2)
    {
        currentChunk->removeCheckpoint(lowerProjection->getCollider());
        chunkWhereItWasRemovedIndex++;
        currentChunk = chunks[chunkWhereItWasRemovedIndex];
    }
}

size_t SegmentedIntervalList::getSize() const
{
    return size;
}

std::vector<LocalSortArray *> *SegmentedIntervalList::getChunks()
{
    return &chunks;
}

void SegmentedIntervalList::clear()
{
    for (size_t i = 0; i < size; i++)
    {
        delete chunks[i];
    }
    chunks.clear();
    size = 0;
}

void SegmentedIntervalList::sort()
{
    // TODO: parallelize this
    // TODO: probably don't need this
    for (size_t i = 0; i < size; i++)
    {
        chunks[i]->sort();
    }

    // check if the chunks are sorted
    // find the first chunk with a max value greater than the min value of the current chunk, counting backwards
    // example: let sorted max values of chunks be 1, 3, 4, 6. Let the current chunks min value be be 2.
    // The first chunk with a max value greater than 2 is 3.
    for (size_t i = 1; i < size; i++)
    {
        while (chunks[i]->getSize() > 0 && *chunks[i - 1]->getMax() > *chunks[i]->getMin())
        {

            size_t j = i - 1;
            while (j > 0 && *chunks[j - 1]->getMax() > *chunks[i]->getMin())
            {
                j--;
            }
            BoundingRadiusProjection *element = remove(i, 0);

            size_t prevSize = size;
            size_t indexWhereItWasAdded = add(element, j);
            size_t shift = size - prevSize; // 1 if split occurred, 0 otherwise
            size_t indexWhereItWasRemoved = i + shift;

            updateCheckpoint(indexWhereItWasAdded, indexWhereItWasRemoved, element);
        }
    }
}

size_t SegmentedIntervalList::binarySearch(BoundingRadiusProjection *element)
{
    if (size == 0 || chunks[0]->getSize() == 0)
    {
        return 0;
    }

    size_t low = 0;
    size_t high = size - 1;
    size_t mid = 0;
    BoundingRadiusProjection value = *element;

    while (low <= high)
    {
        mid = (low + high) / 2;

        if (mid == 0)
        {
            return mid;
        }
        if (value <= *chunks[mid]->getMax() && value >= *chunks[mid]->getMin())
        {
            return mid;
        }
        else if (value >= *chunks[mid]->getMax())
        {
            low = mid + 1;
        }
        else if (value < *chunks[mid]->getMax())
        {
            high = mid - 1;
        }
    }

    return mid;
}

size_t SegmentedIntervalList::binarySearch(LocalSortArray *array)
{
    if (size == 0 || chunks[0]->getSize() == 0)
    {
        return 0;
    }

    size_t low = 0;
    size_t high = size - 1;
    size_t mid = 0;

    while (low <= high)
    {
        mid = (low + high) / 2;

        if (chunks[mid] == array)
        {
            return mid;
        }
        else if (*chunks[mid]->getMax() < *array->getMin())
        {
            low = mid + 1;
        }
        else if (*chunks[mid]->getMin() > *array->getMax())
        {
            high = mid - 1;
        }
    }

    return mid;
}

size_t SegmentedIntervalList::add(BoundingRadiusProjection *element)
{
    // find the index of the element using binary search
    size_t index = binarySearch(element);

    // insert the element at the index
    return add(element, index);
}

size_t SegmentedIntervalList::add(BoundingRadiusProjection *element, size_t chunkIndex)
{
    // insert the element at the index
    if (!chunks[chunkIndex]->add(element))
    {
        // if the array is full, split it
        LocalSortArray *newArray = new LocalSortArray(chunks[chunkIndex]);

        // insert the new array after the current one
        chunks.insert(chunks.begin() + chunkIndex + 1, newArray);
        size++;

        // check if the element should be inserted in the new array or the old one
        if (*element > *chunks[chunkIndex]->getMax())
        {
            chunkIndex++;
        }

        // insert the element in the correct array
        chunks[chunkIndex]->add(element);
    }

    return chunkIndex;
}

LocalSortArray *SegmentedIntervalList::remove(BoundingRadiusProjection *element)
{
    LocalSortArray *array = element->getChunk();
    array->remove(element);

    return array;
}

BoundingRadiusProjection *SegmentedIntervalList::remove(size_t chunkIndex, size_t arrayIndex)
{
    BoundingRadiusProjection *removedElement = chunks[chunkIndex]->remove(arrayIndex);

    // if the array is empty, remove it
    if (chunks[chunkIndex]->getSize() == 0)
    {
        delete chunks[chunkIndex];
        chunks.erase(chunks.begin() + chunkIndex);
        size--;
    }

    return removedElement;
}

void SegmentedIntervalList::updateCheckpoint(size_t indexWhereItWasAdded, size_t indexWhereItWasRemoved, BoundingRadiusProjection *element)
{

    if (element->getIsEnd())
    {
        for (size_t i = indexWhereItWasAdded; i <= indexWhereItWasRemoved; i++)
        {
            chunks[i]->removeCheckpoint(element->getCollider());
        }
    }
    else
    {
        for (size_t i = indexWhereItWasAdded; i <= indexWhereItWasRemoved; i++)
        {
            chunks[i]->addCheckpoint(element->getCollider());
        }
    }
}
