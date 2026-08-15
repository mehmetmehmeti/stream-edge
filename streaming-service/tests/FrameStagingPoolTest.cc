#include "streaming/FrameStagingPool.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace
{

struct FakeDeviceMemory
{
    uint64_t mallocCount{0};
    uint64_t freeCount{0};
    uint64_t copyCount{0};
    uint64_t synchronizeCount{0};
    std::vector<void*> allocated;
};

streaming::FrameStagingPool::DeviceMemoryOps makeFakeOps(
    const std::shared_ptr<FakeDeviceMemory>& memory)
{
    return streaming::FrameStagingPool::DeviceMemoryOps{
        .malloc = [memory](void** ptr, std::size_t bytes) {
            *ptr = std::malloc(bytes);
            if (*ptr == nullptr)
                return cudaErrorMemoryAllocation;

            ++memory->mallocCount;
            memory->allocated.push_back(*ptr);
            return cudaSuccess;
        },
        .free = [memory](void* ptr) {
            if (ptr != nullptr)
            {
                ++memory->freeCount;
                std::free(ptr);
            }
            return cudaSuccess;
        },
        .copyAsync = [memory](void* dst,
                              const void* src,
                              std::size_t bytes,
                              cudaMemcpyKind,
                              cudaStream_t) {
            std::memcpy(dst, src, bytes);
            ++memory->copyCount;
            return cudaSuccess;
        },
        .copy2DAsync = [memory](void* dst,
                                std::size_t dstPitch,
                                const void* src,
                                std::size_t srcPitch,
                                std::size_t width,
                                std::size_t height,
                                cudaMemcpyKind,
                                cudaStream_t) {
            for (std::size_t row = 0; row < height; ++row)
            {
                std::memcpy(static_cast<unsigned char*>(dst) + row * dstPitch,
                            static_cast<const unsigned char*>(src) + row * srcPitch,
                            width);
            }
            ++memory->copyCount;
            return cudaSuccess;
        },
        .streamSynchronize = [memory](cudaStream_t) {
            ++memory->synchronizeCount;
            return cudaSuccess;
        },
    };
}

void testAcquireAndReleaseSlot()
{
    auto fakeMemory = std::make_shared<FakeDeviceMemory>();
    auto pool = std::make_shared<streaming::FrameStagingPool>(2, makeFakeOps(fakeMemory));

    assert(pool->slotCount() == 2);
    assert(pool->availableSlotCount() == 2);

    auto lease = pool->acquire(128);
    assert(lease);
    assert(lease->devicePtr() != nullptr);
    assert(lease->capacityBytes() == 128);
    assert(pool->availableSlotCount() == 1);

    lease.reset();
    assert(pool->availableSlotCount() == 2);
}

void testSlotNotReusedUntilLeaseDestroyed()
{
    auto fakeMemory = std::make_shared<FakeDeviceMemory>();
    auto pool = std::make_shared<streaming::FrameStagingPool>(1, makeFakeOps(fakeMemory));

    auto firstLease = pool->acquire(64);
    assert(firstLease);

    auto secondLease = pool->acquire(64);
    assert(!secondLease);
    assert(pool->droppedAcquireCount() == 1);

    firstLease.reset();

    secondLease = pool->acquire(64);
    assert(secondLease);
    assert(pool->droppedAcquireCount() == 1);
}

void testCopyIntoStagingMemory()
{
    auto fakeMemory = std::make_shared<FakeDeviceMemory>();
    auto pool = std::make_shared<streaming::FrameStagingPool>(1, makeFakeOps(fakeMemory));
    auto lease = pool->acquire(4);
    assert(lease);

    const unsigned char source[] = {1, 2, 3, 4};
    assert(pool->copyFromDevice(*lease, source, sizeof(source)) == cudaSuccess);
    assert(fakeMemory->copyCount == 1);
    assert(fakeMemory->synchronizeCount == 1);
    assert(std::memcmp(lease->devicePtr(), source, sizeof(source)) == 0);
}

void testAsyncCopyDoesNotSynchronize()
{
    auto fakeMemory = std::make_shared<FakeDeviceMemory>();
    auto pool = std::make_shared<streaming::FrameStagingPool>(1, makeFakeOps(fakeMemory));
    auto lease = pool->acquire(4);
    assert(lease);

    const unsigned char source[] = {5, 6, 7, 8};
    assert(pool->copyFromDeviceAsync(*lease, source, sizeof(source), nullptr) == cudaSuccess);
    assert(fakeMemory->copyCount == 1);
    assert(fakeMemory->synchronizeCount == 0);
    assert(std::memcmp(lease->devicePtr(), source, sizeof(source)) == 0);
}

void testPitchedCopyPacksRows()
{
    auto fakeMemory = std::make_shared<FakeDeviceMemory>();
    auto pool = std::make_shared<streaming::FrameStagingPool>(1, makeFakeOps(fakeMemory));
    auto lease = pool->acquire(6);
    assert(lease);

    const unsigned char source[] = {1, 2, 3, 99, 4, 5, 6, 99};
    const unsigned char expected[] = {1, 2, 3, 4, 5, 6};
    assert(pool->copy2DFromDevice(*lease, source, 4, 3, 2) == cudaSuccess);
    assert(fakeMemory->copyCount == 1);
    assert(fakeMemory->synchronizeCount == 1);
    assert(std::memcmp(lease->devicePtr(), expected, sizeof(expected)) == 0);
}

void testPoolFreesAllocatedMemoryOnDestroy()
{
    auto fakeMemory = std::make_shared<FakeDeviceMemory>();
    {
        auto pool = std::make_shared<streaming::FrameStagingPool>(2, makeFakeOps(fakeMemory));
        auto firstLease = pool->acquire(64);
        auto secondLease = pool->acquire(128);
        assert(firstLease);
        assert(secondLease);
        assert(fakeMemory->mallocCount == 2);
    }

    assert(fakeMemory->freeCount == 2);
}

void testLeaseKeepsPoolAlive()
{
    auto fakeMemory = std::make_shared<FakeDeviceMemory>();
    std::shared_ptr<streaming::FrameStagingPool::Lease> lease;

    {
        auto pool = std::make_shared<streaming::FrameStagingPool>(1, makeFakeOps(fakeMemory));
        lease = pool->acquire(64);
        assert(lease);
        assert(pool->availableSlotCount() == 0);
    }

    assert(fakeMemory->freeCount == 0);
    lease.reset();
    assert(fakeMemory->freeCount == 1);
}

} // namespace

int main()
{
    testAcquireAndReleaseSlot();
    testSlotNotReusedUntilLeaseDestroyed();
    testCopyIntoStagingMemory();
    testAsyncCopyDoesNotSynchronize();
    testPitchedCopyPacksRows();
    testPoolFreesAllocatedMemoryOnDestroy();
    testLeaseKeepsPoolAlive();
}
