#include "segmentedintervallist.h"

template <typename T>
SegmentedIntervalList<T>::SegmentedIntervalList()
    : size(0), arrays()
{
}

template <typename T>
SegmentedIntervalList<T>::~SegmentedIntervalList()
{
    clear();
}

template <typename T>
void SegmentedIntervalList<T>::add(T &element)
{
    // find the index where the element should be inserted using binary search
    while (true)
    {
        size_t low = 0;
        size_t high = size - 1;
        size_t mid = 0;

        while (low <= high)
        {
            mid = (low + high) / 2;

            if (element >= arrays[mid]->getMax() && element <= arrays[mid + 1]->getMax())
            {
                low = mid;
                break;
            }
            else if (element >= arrays[mid]->getMax())
            {
                low = mid + 1;
            }
            else if (element <= arrays[mid]->getMax())
            {
                high = mid - 1;
            }
        }

        // insert the element at the index
        if (!arrays[low]->add(element))
        {
            if (low == size - 1)
            {
                LocalSortArray<T> *newArray = new LocalSortArray<T>();
                arrays.push_back(newArray);
                size++;

                if (element < arrays[low]->getMax())
                {
                    newArray->add(arrays[low]->addAndPop(element));
                }
                else
                {
                    newArray->add(element);
                }

                return;
            }
            else
            {
                element = arrays[low]->addAndPop(element);
            }
        }

        return;
    }
}
