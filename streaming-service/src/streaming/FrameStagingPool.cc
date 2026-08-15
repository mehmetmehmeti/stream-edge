#include "streaming/FrameStagingPool.h"

#include <algorithm>
#include <utility>

namespace streaming
{

FrameStagingPool::DeviceMemoryOps FrameStagingPool::defaultDeviceMemoryOps()
{
    return DeviceMemoryOps{
        .malloc = [](void** ptr, std::size_t bytes) { return cudaMalloc(ptr, bytes); },
        .free = [](void* ptr) { return cudaFree(ptr); },
        .copyAsync = [](void* dst,
                        const void* src,
                        std::size_t bytes,
                        cudaMemcpyKind kind,
                        cudaStream_t stream) {
            return cudaMemcpyAsync(dst, src, bytes, kind, stream);
        },
        .copy2DAsync = [](void* dst,
                          std::size_t dstPitch,
                          const void* src,
                          std::size_t srcPitch,
                          std::size_t width,
                          std::size_t height,
                          cudaMemcpyKind kind,
                          cudaStream_t stream) {
            return cudaMemcpy2DAsync(
                dst, dstPitch, src, srcPitch, width, height, kind, stream);
        },
        .streamSynchronize = [](cudaStream_t stream) { return cudaStreamSynchronize(stream); },
    };
}

FrameStagingPool::Lease::Lease(std::shared_ptr<FrameStagingPool> pool,
                               std::size_t slotIndex,
                               void* devicePtr,
                               uint64_t capacityBytes)
    : pool_(std::move(pool)),
      slotIndex_(slotIndex),
      devicePtr_(devicePtr),
      capacityBytes_(capacityBytes)
{
}

FrameStagingPool::Lease::~Lease()
{
    pool_->release(slotIndex_);
}

void* FrameStagingPool::Lease::devicePtr() const
{
    return devicePtr_;
}

uint64_t FrameStagingPool::Lease::capacityBytes() const
{
    return capacityBytes_;
}

std::size_t FrameStagingPool::Lease::slotIndex() const
{
    return slotIndex_;
}

FrameStagingPool::FrameStagingPool(std::size_t slotCount)
    : FrameStagingPool(slotCount, defaultDeviceMemoryOps())
{
}

FrameStagingPool::FrameStagingPool(std::size_t slotCount, DeviceMemoryOps ops)
    : slots_(std::max<std::size_t>(slotCount, 1)),
      ops_(std::move(ops))
{
}

FrameStagingPool::~FrameStagingPool()
{
    std::lock_guard lock(mutex_);

    for (auto& slot : slots_)
        freeSlot(slot);
}

std::shared_ptr<FrameStagingPool::Lease> FrameStagingPool::acquire(uint64_t requiredBytes)
{
    std::lock_guard lock(mutex_);

    for (std::size_t index = 0; index < slots_.size(); ++index)
    {
        auto& slot = slots_[index];
        if (slot.inUse)
            continue;

        if (ensureCapacity(slot, requiredBytes) != cudaSuccess)
            return nullptr;

        slot.inUse = true;
        return std::shared_ptr<Lease>(
            new Lease(shared_from_this(), index, slot.devicePtr, slot.capacityBytes));
    }

    ++droppedAcquireCount_;
    return nullptr;
}

cudaError_t FrameStagingPool::copyFromDevice(const Lease& lease,
                                             const void* sourceDevicePtr,
                                             uint64_t byteCount,
                                             cudaStream_t stream)
{
    auto status = copyFromDeviceAsync(lease, sourceDevicePtr, byteCount, stream);
    if (status != cudaSuccess)
        return status;

    return ops_.streamSynchronize(stream);
}

cudaError_t FrameStagingPool::copyFromDeviceAsync(const Lease& lease,
                                                  const void* sourceDevicePtr,
                                                  uint64_t byteCount,
                                                  cudaStream_t stream)
{
    if (sourceDevicePtr == nullptr || lease.devicePtr() == nullptr)
        return cudaErrorInvalidValue;

    if (byteCount > lease.capacityBytes())
        return cudaErrorInvalidValue;

    return ops_.copyAsync(lease.devicePtr(),
                          sourceDevicePtr,
                          static_cast<std::size_t>(byteCount),
                          cudaMemcpyDeviceToDevice,
                          stream);
}

cudaError_t FrameStagingPool::copy2DFromDevice(const Lease& lease,
                                                const void* sourceDevicePtr,
                                                uint64_t sourcePitch,
                                                uint64_t rowBytes,
                                                uint64_t height,
                                                cudaStream_t stream)
{
    if (sourceDevicePtr == nullptr || lease.devicePtr() == nullptr ||
        rowBytes == 0 || height == 0 || sourcePitch < rowBytes ||
        rowBytes > lease.capacityBytes() / height)
    {
        return cudaErrorInvalidValue;
    }

    cudaError_t status = cudaErrorInvalidValue;
    if (ops_.copy2DAsync)
    {
        status = ops_.copy2DAsync(lease.devicePtr(),
                                  static_cast<std::size_t>(rowBytes),
                                  sourceDevicePtr,
                                  static_cast<std::size_t>(sourcePitch),
                                  static_cast<std::size_t>(rowBytes),
                                  static_cast<std::size_t>(height),
                                  cudaMemcpyDeviceToDevice,
                                  stream);
    }
    else if (sourcePitch == rowBytes && ops_.copyAsync)
    {
        status = ops_.copyAsync(lease.devicePtr(),
                                sourceDevicePtr,
                                static_cast<std::size_t>(rowBytes * height),
                                cudaMemcpyDeviceToDevice,
                                stream);
    }

    if (status != cudaSuccess)
        return status;

    return ops_.streamSynchronize(stream);
}

std::size_t FrameStagingPool::slotCount() const
{
    std::lock_guard lock(mutex_);
    return slots_.size();
}

std::size_t FrameStagingPool::availableSlotCount() const
{
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(
        std::count_if(slots_.begin(), slots_.end(), [](const Slot& slot) {
            return !slot.inUse;
        }));
}

uint64_t FrameStagingPool::droppedAcquireCount() const
{
    std::lock_guard lock(mutex_);
    return droppedAcquireCount_;
}

cudaError_t FrameStagingPool::ensureCapacity(Slot& slot, uint64_t requiredBytes)
{
    if (requiredBytes == 0)
        return cudaErrorInvalidValue;

    if (slot.capacityBytes >= requiredBytes)
        return cudaSuccess;

    freeSlot(slot);

    void* devicePtr = nullptr;
    auto status = ops_.malloc(&devicePtr, static_cast<std::size_t>(requiredBytes));
    if (status != cudaSuccess)
        return status;

    slot.devicePtr = devicePtr;
    slot.capacityBytes = requiredBytes;
    return cudaSuccess;
}

void FrameStagingPool::release(std::size_t slotIndex)
{
    std::lock_guard lock(mutex_);

    if (slotIndex < slots_.size())
        slots_[slotIndex].inUse = false;
}

void FrameStagingPool::freeSlot(Slot& slot)
{
    if (slot.devicePtr != nullptr)
        (void)ops_.free(slot.devicePtr);

    slot.devicePtr = nullptr;
    slot.capacityBytes = 0;
    slot.inUse = false;
}

} // namespace streaming
