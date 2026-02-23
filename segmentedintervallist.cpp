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

// void SegmentedIntervalList::add(Collider *element)
// {
//     // find the index of the element using binary search
//     BoundingRadiusProjectionAxis *xProjections = element->getXProjections();
//     BoundingRadiusProjectionAxis *yProjections = element->getYProjections();

//     add(xProjections);
//     add(yProjections);
// }

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

// void SegmentedIntervalList::remove(Collider *element)
// {
//     BoundingRadiusProjectionAxis *xProjections = element->getXProjections();
//     BoundingRadiusProjectionAxis *yProjections = element->getYProjections();

//     remove(xProjections);
//     remove(yProjections);
// }

void SegmentedIntervalList::remove(BoundingRadiusProjectionAxis *axis)
{
    // find the index of the element using binary search
    BoundingRadiusProjection *lowerProjection = axis->getMin();
    size_t chunkWhereItWasRemoved = remove(lowerProjection);

    BoundingRadiusProjection *upperProjection = axis->getMax();
    size_t chunkWhereItWasRemoved2 = remove(upperProjection);

    for (size_t i = chunkWhereItWasRemoved; i < chunkWhereItWasRemoved2; i++)
    {
        chunks[i]->removeCheckpoint(lowerProjection->getCollider());
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
    for (size_t i = 1; i < size; i++)
    {
        while (*chunks[i - 1]->getMax() > *chunks[i]->getMin())
        {
            // find the first chunk with a max value greater than the min value of the current chunk, counting backwards
            // example: let sorted max values of chunks be 1, 3, 4, 6. Let the current chunks min value be be 2.
            // The first chunk with a max value greater than 2 is 3.

            size_t j = i - 1;
            while (j > 0 && *chunks[j - 1]->getMax() <= *chunks[i]->getMin())
            {
                j--;
            }
            BoundingRadiusProjection *element = chunks[i]->remove(0UL);

            size_t indexWhereItWasAdded = add(element, j);
            size_t indexWhereItWasRemoved = i + indexWhereItWasAdded - j; // in case the chunk split
            updateCheckpoint(indexWhereItWasAdded, element, indexWhereItWasRemoved);
        }
    }
}

void SegmentedIntervalList::sort(size_t index)
{
    chunks[index]->sort();
}

size_t SegmentedIntervalList::binarySearch(BoundingRadiusProjection *element)
{
    if (size == 0)
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

        if (value <= *chunks[mid]->getMax() && (mid == 0 || value >= *chunks[mid - 1]->getMax()))
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

size_t SegmentedIntervalList::remove(BoundingRadiusProjection *element)
{
    // find the index of the element using binary search
    size_t index = binarySearch(element);
    size_t index2 = chunks[index]->binarySearch(element);

    remove(index, index2);

    return index;
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

void SegmentedIntervalList::swap(BoundingRadiusProjection *element1, BoundingRadiusProjection *element2)
{
    int chunkIndex1 = binarySearch(element1);
    int arrayIndex1 = chunks[chunkIndex1]->binarySearch(element1);

    int chunkIndex2 = binarySearch(element2);
    int arrayIndex2 = chunks[chunkIndex2]->binarySearch(element2);

    swap(chunkIndex1, arrayIndex1, chunkIndex2, arrayIndex2);
}

void SegmentedIntervalList::swap(size_t chunkIndex1, size_t arrayIndex1, size_t chunkIndex2, size_t arrayIndex2)
{
    chunks[chunkIndex1]->swap(arrayIndex1, chunks[chunkIndex2], arrayIndex2);

    updateCheckpoint(chunkIndex1, arrayIndex1, chunkIndex2);
    updateCheckpoint(chunkIndex2, arrayIndex2, chunkIndex1);
}

void SegmentedIntervalList::updateCheckpoint(size_t chunkIndex1, size_t arrayIndex1, size_t chunkIndex2)
{
    // chunkIndex1 is the chunk where the element was inserted
    // arrayIndex1 is the index of the element in the chunk
    // chunkIndex2 is the chunk where the element was removed
    // chunkIndex2 is always greater than chunkIndex1

    BoundingRadiusProjection *element = chunks[chunkIndex1]->get(arrayIndex1);
    updateCheckpoint(chunkIndex1, element, chunkIndex2);
}

void SegmentedIntervalList::updateCheckpoint(size_t chunkIndex, BoundingRadiusProjection *element, size_t chunkIndex2)
{
    // chunkIndex is the chunk where the element was inserted
    // chunkIndex2 is the chunk where the element was removed

    if (element->getIsEnd())
    {
        for (size_t i = chunkIndex; i <= chunkIndex2; i++)
        {
            chunks[i]->removeCheckpoint(element->getCollider());
        }
    }
    else
    {
        for (size_t i = chunkIndex; i <= chunkIndex2; i++)
        {
            chunks[i]->addCheckpoint(element->getCollider());
        }
    }
}