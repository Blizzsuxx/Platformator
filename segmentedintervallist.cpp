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

    // check if the arrays are sorted
    for (size_t i = 1; i < size; i++)
    {
        while (*chunks[i - 1]->getMax() > *chunks[i]->getMin())
        {
            // if the min element belongs to the previous array, move it to the previous array
            if (i == 1 || *chunks[i]->getMin() >= *chunks[i - 2]->getMax())
            {
                chunk
            }
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
    if (!chunks[index]->add(element))
    {
        // if the array is full, split it
        LocalSortArray *newArray = new LocalSortArray();
        for (size_t i = MAX_SIZE / 2; i < MAX_SIZE; i++)
        {
            newArray->addWithoutSort(chunks[index]->pop());
        }
        chunks.insert(chunks.begin() + index + 1, newArray);
        size++;
    }

    return index;
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

    // update checkpoints
    chunks[chunkIndex1]->get(arrayIndex1)->getCollider()->getPairProjection()
}
