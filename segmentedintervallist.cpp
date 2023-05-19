#include "segmentedintervallist.h"

SegmentedIntervalList::SegmentedIntervalList()
    : size(0), chunks()
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
        chunks[i]->addCheckpoint(element);
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
        chunks[i]->removeCheckpoint(element);
    }
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
            size_t j = i - 1;
            while (j > 0 && *chunks[j - 1]->getMax() <= *chunks[i]->getMin())
            {
                j--;
            }

            size_t indexWhereItWasAdded = add(chunks[i]->remove(0UL), j);
        }
    }
}

void SegmentedIntervalList::sort(size_t index)
{
    chunks[index]->sort();
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

        if (value <= *chunks[mid]->getMax() && (mid == 0 || value >= *chunks[mid - 1]->getMax()))
        {
            return mid;
        }
        else if (value >= *chunks[mid]->getMax())
        {
            low = mid + 1;
        }
        else if (value <= *chunks[mid]->getMax())
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
    return add(element, index);
}

size_t SegmentedIntervalList::add(BoundingRadiusProjection *element, size_t chunkIndex)
{
    // insert the element at the index
    if (!chunks[chunkIndex]->add(element))
    {
        // if the array is full, split it
        LocalSortArray *newArray = new LocalSortArray();
        for (size_t i = MAX_SIZE / 2; i < MAX_SIZE; i++)
        {
            newArray->addWithoutSort(chunks[chunkIndex]->pop());
        }
        chunks.insert(chunks.begin() + chunkIndex + 1, newArray);
        size++;
        chunkIndex++;
    }

    return chunkIndex;
}

size_t SegmentedIntervalList::remove(BoundingRadiusProjection *element)
{
    // find the index of the element using binary search
    size_t index = binarySearch(element);

    chunks[index]->remove(element);

    // if the array is empty, remove it
    if (chunks[index]->getSize() == 0)
    {
        delete chunks[index];
        chunks.erase(chunks.begin() + index);
        size--;
    }

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
    size_t chunkIndex1 = binarySearch(element1);
    size_t arrayIndex1 = chunks[chunkIndex1]->binarySearch(element1);

    size_t chunkIndex2 = binarySearch(element2);
    size_t arrayIndex2 = chunks[chunkIndex2]->binarySearch(element2);

    swap(chunkIndex1, arrayIndex1, chunkIndex2, arrayIndex2);
}

void SegmentedIntervalList::swap(size_t chunkIndex1, size_t arrayIndex1, size_t chunkIndex2, size_t arrayIndex2)
{
    chunks[chunkIndex1]->swap(arrayIndex1, chunks[chunkIndex2], arrayIndex2);

    updateCheckpoint(chunkIndex1, arrayIndex1, chunkIndex2);
}

void SegmentedIntervalList::updateCheckpoint(size_t chunkIndex1, size_t arrayIndex1, size_t chunkIndex2)
{
    // chunkIndex1 is the chunk where the element was inserted
    // arrayIndex1 is the index of the element in the chunk
    // chunkIndex2 is the chunk where the element was removed
    // chunkIndex2 is always greater than chunkIndex1

    if (chunks[chunkIndex1]->get(arrayIndex1)->isEnd())
    {
        for(size_t i = chunkIndex1; i <= chunkIndex2; i++)
        {
            chunks[i]->removeCheckpoint(chunks[chunkIndex1]->get(arrayIndex1)->getCollider());
        }
    }
    else
    {
        for(size_t i = chunkIndex1; i <= chunkIndex2; i++)
        {
            chunks[i]->addCheckpoint(chunks[chunkIndex1]->get(arrayIndex1)->getCollider());
        }
    }
}
