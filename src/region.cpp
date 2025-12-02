#include "util.hpp"

Region<int>::Region()
{
    pixman_region32_init(&region);
}

Region<int>::Region(wlr_box rect)
{
    pixman_region32_init_rect(&region, rect.x, rect.y, rect.width, rect.height);
}

Region<int>::Region(const pixman_region32* other)
{
    pixman_region32_init(&region);
    pixman_region32_copy(&region, other);
}

Region<int>::Region(const Region<int>& other)
{
    pixman_region32_init(&region);
    pixman_region32_copy(&region, &other.region);
}

Region<int>& Region<int>::operator=(const Region<int>& other)
{
    if (this != &other) {
        pixman_region32_clear(&region);
        pixman_region32_copy(&region, &other.region);
    }
    return *this;
}

Region<int>::Region(Region<int>&& other)
{
    region = other.region;
    other.region = {};
    pixman_region32_init(&other.region);
}

Region<int>& Region<int>::operator=(Region<int>&& other)
{
    if (this != &other) {
        pixman_region32_fini(&region);
        region = other.region;
        other.region = {};
        pixman_region32_init(&other.region);
    }
    return *this;
}

Region<int>::~Region<int>()
{
    pixman_region32_fini(&region);
}

void Region<int>::translate(ivec2 delta)
{
    pixman_region32_translate(&region, delta.x, delta.y);
}

void Region<int>::clear()
{
    pixman_region32_clear(&region);
}

bool Region<int>::empty() const
{
    return pixman_region32_empty(&region);
}

std::span<const pixman_box32_t> Region<int>::rectangles() const
{
    int count;
    const pixman_box32* data = pixman_region32_rectangles(&region, &count);
    return std::span(data, count);
}

bool Region<int>::contains(ivec2 point) const
{
    pixman_box32_t box;
    return pixman_region32_contains_point(&region, point.x, point.y, &box);
}

template<>
void region_union<int>(Region<int>& out, const Region<int>& a, const Region<int>& b)
{
    pixman_region32_union(&out.region, &a.region, &b.region);
}

template<>
void region_subtract<int>(Region<int>& out, const Region<int>& a, const Region<int>& b)
{
    region_union(out, a, b);
    pixman_region32_subtract(&out.region, &a.region, &b.region);
}

template<>
void region_intersect<int>(Region<int>& out, const Region<int>& a, const Region<int>& b)
{
    pixman_region32_intersect(&out.region, &a.region, &b.region);
}
