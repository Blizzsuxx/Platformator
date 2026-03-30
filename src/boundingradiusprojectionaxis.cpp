#include "collider.h"

BoundingRadiusProjectionAxis::BoundingRadiusProjectionAxis(Collider *collider, float min, float max)
    : min(BoundingRadiusProjection(collider, min, false)),
      max(BoundingRadiusProjection(collider, max, true)),
      proxies()
{
}

BoundingRadiusProjectionAxis::~BoundingRadiusProjectionAxis()
{
    for (BoundingRadiusProjectionAxisProxy *proxy : proxies)
    {
        delete proxy;
    }
    proxies.clear();
}

BoundingRadiusProjection *BoundingRadiusProjectionAxis::getMin()
{
    return &min;
}

BoundingRadiusProjection *BoundingRadiusProjectionAxis::getMax()
{
    return &max;
}

bool BoundingRadiusProjectionAxis::operator==(const BoundingRadiusProjectionAxis &other) const
{
    return min == other.min && max == other.max;
}

bool BoundingRadiusProjectionAxis::operator!=(const BoundingRadiusProjectionAxis &other) const
{
    return !(*this == other);
}

BoundingRadiusProjectionAxisProxy *BoundingRadiusProjectionAxis::createProxyForList(SegmentedIntervalList *list)
{
    BoundingRadiusProjectionAxisProxy *proxy = new BoundingRadiusProjectionAxisProxy{BoundingRadiusProjectionProxy(&min, nullptr), BoundingRadiusProjectionProxy(&max, nullptr), list, proxies.size()};
    proxies.push_back(proxy);
    return proxy;
}

BoundingRadiusProjectionAxisProxy *BoundingRadiusProjectionAxis::getProxyForList(SegmentedIntervalList *list)
{
    for (BoundingRadiusProjectionAxisProxy *proxy : proxies)
    {
        if (proxy->ownerList == list)
        {
            return proxy;
        }
    }

    return nullptr;
}

void BoundingRadiusProjectionAxis::removeProxy(SegmentedIntervalList *list)
{
    for (size_t i = 0; i < proxies.size(); i++)
    {
        if (proxies[i]->ownerList == list)
        {
            removeProxy(proxies[i]);
            return;
        }
    }
}

void BoundingRadiusProjectionAxis::removeProxy(BoundingRadiusProjectionAxisProxy *proxy)
{
    if (proxy == nullptr)
    {
        return;
    }

    size_t lastIndex = proxies.size() - 1;
    size_t proxyIndex = proxy->colliderProxyIndex;

    BoundingRadiusProjectionAxisProxy *movedProxy = proxies[lastIndex];
    proxies[proxyIndex] = movedProxy;
    movedProxy->colliderProxyIndex = proxyIndex;

    proxies.pop_back();
    delete proxy;
    return;
}

std::vector<BoundingRadiusProjectionAxisProxy *> &BoundingRadiusProjectionAxis::getProxies()
{
    return proxies;
}