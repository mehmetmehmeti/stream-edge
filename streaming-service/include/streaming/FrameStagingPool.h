#pragma once

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace streaming
{

class FrameStagingPool : public std::enable_shared_from_this<FrameStagingPool>
{
public:
    struct DeviceMemoryOps
    {
        std::function<cudaError_t(void**, std::size_t)> malloc;
        std::function<cudaError_t(void*)> free;
        std::function<cudaError_t(void*, const void*, std::size_t, cudaMemcpyKind, cudaStream_t)> copyAsync;
        std::function<cudaError_t(void*, std::size_t, const void*, std::size_t,
                                  std::size_t, std::size_t, cudaMemcpyKind, cudaStream_t)>
            copy2DAsync;
        std::function<cudaError_t(cudaStream_t)> streamSynchronize;
    };

    class Lease
    {
    public:
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&&) = delete;
        Lease& operator=(Lease&&) = delete;
        ~Lease();

        void* devicePtr() const;
        uint64_t capacityBytes() const;
        std::size_t slotIndex() const;

    private:
        friend class FrameStagingPool;

        Lease(std::shared_ptr<FrameStagingPool> pool,
              std::size_t slotIndex,
              void* devicePtr,
              uint64_t capacityBytes);

        std::shared_ptr<FrameStagingPool> pool_;
        std::size_t slotIndex_;
        void* devicePtr_;
        uint64_t capacityBytes_;
    };

    explicit FrameStagingPool(std::size_t slotCount = 3);
    FrameStagingPool(std::size_t slotCount, DeviceMemoryOps ops);
    ~FrameStagingPool();

    FrameStagingPool(const FrameStagingPool&) = delete;
    FrameStagingPool& operator=(const FrameStagingPool&) = delete;
    FrameStagingPool(FrameStagingPool&&) = delete;
    FrameStagingPool& operator=(FrameStagingPool&&) = delete;

    std::shared_ptr<Lease> acquire(uint64_t requiredBytes);
    cudaError_t copyFromDevice(const Lease& lease,
                               const void* sourceDevicePtr,
                               uint64_t byteCount,
                               cudaStream_t stream = nullptr);
    cudaError_t copyFromDeviceAsync(const Lease& lease,
                                    const void* sourceDevicePtr,
                                    uint64_t byteCount,
                                    cudaStream_t stream);
    cudaError_t copy2DFromDevice(const Lease& lease,
                                 const void* sourceDevicePtr,
                                 uint64_t sourcePitch,
                                 uint64_t rowBytes,
                                 uint64_t height,
                                 cudaStream_t stream = nullptr);

    std::size_t slotCount() const;
    std::size_t availableSlotCount() const;
    uint64_t droppedAcquireCount() const;

private:
    struct Slot
    {
        void* devicePtr{nullptr};
        uint64_t capacityBytes{0};
        bool inUse{false};
    };

    static DeviceMemoryOps defaultDeviceMemoryOps();

    cudaError_t ensureCapacity(Slot& slot, uint64_t requiredBytes);
    void release(std::size_t slotIndex);
    void freeSlot(Slot& slot);

    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    DeviceMemoryOps ops_;
    uint64_t droppedAcquireCount_{0};
};

} // namespace streaming
