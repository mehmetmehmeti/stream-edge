#include "streaming/FrameBridge.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{

streaming::VideoFrameHandle makeFrame(uint64_t frameNumber)
{
    return streaming::VideoFrameHandle{
        .width = 640,
        .height = 480,
        .stride = 640 * 3,
        .channels = 3,
        .byteCount = 640 * 480 * 3,
        .format = streaming::PixelFormat::RGB888,
        .dataType = streaming::FrameDataType::UInt8,
        .memoryLocation = streaming::MemoryLocation::Device,
        .deviceId = 0,
        .devicePtr = reinterpret_cast<void*>(0x1000 + frameNumber),
        .frameNumber = frameNumber,
        .timestampNs = frameNumber * 1000,
    };
}

void testEmptyAcquire()
{
    streaming::FrameBridge bridge;
    streaming::VideoFrameHandle frame;

    assert(!bridge.acquireLatestFrame(frame));
    assert(bridge.publishedFrameCount() == 0);
    assert(bridge.acquiredFrameCount() == 0);
    assert(bridge.droppedFrameCount() == 0);
}

void testPublishOneFrameAcquireSameFrame()
{
    streaming::FrameBridge bridge;
    bridge.publishFrame(makeFrame(1));

    streaming::VideoFrameHandle acquired;
    assert(bridge.acquireLatestFrame(acquired));
    assert(acquired.frameNumber == 1);
    assert(acquired.width == 640);
    assert(acquired.height == 480);
    assert(acquired.stride == 1920);
    assert(acquired.channels == 3);
    assert(acquired.byteCount == 921600);
    assert(acquired.format == streaming::PixelFormat::RGB888);
    assert(acquired.dataType == streaming::FrameDataType::UInt8);
    assert(acquired.memoryLocation == streaming::MemoryLocation::Device);
    assert(acquired.deviceId == 0);
    assert(acquired.hasDeviceBuffer());
    assert(!acquired.hasLifetimeToken());
    assert(acquired.isSupportedFormat());
    assert(acquired.timestampNs == 1000);
    assert(bridge.publishedFrameCount() == 1);
    assert(bridge.acquiredFrameCount() == 1);
    assert(bridge.droppedFrameCount() == 0);
}

void testAcquireReturnsLatestFrame()
{
    streaming::FrameBridge bridge;
    bridge.publishFrame(makeFrame(1));
    bridge.publishFrame(makeFrame(2));
    bridge.publishFrame(makeFrame(3));

    streaming::VideoFrameHandle acquired;
    assert(bridge.acquireLatestFrame(acquired));
    assert(acquired.frameNumber == 3);
    assert(acquired.timestampNs == 3000);
    assert(bridge.publishedFrameCount() == 3);
    assert(bridge.acquiredFrameCount() == 1);
    assert(bridge.droppedFrameCount() == 2);
}

void testDroppedCountOnlyTracksUnacquiredFrames()
{
    streaming::FrameBridge bridge;

    bridge.publishFrame(makeFrame(1));

    streaming::VideoFrameHandle acquired;
    assert(bridge.acquireLatestFrame(acquired));
    assert(acquired.frameNumber == 1);

    bridge.publishFrame(makeFrame(2));
    bridge.publishFrame(makeFrame(3));

    assert(bridge.acquireLatestFrame(acquired));
    assert(acquired.frameNumber == 3);
    assert(bridge.publishedFrameCount() == 3);
    assert(bridge.acquiredFrameCount() == 2);
    assert(bridge.droppedFrameCount() == 1);
}

void testRepeatedAcquireDoesNotReplaySameFrame()
{
    streaming::FrameBridge bridge;
    bridge.publishFrame(makeFrame(8));

    streaming::VideoFrameHandle acquired;
    assert(bridge.acquireLatestFrame(acquired));
    assert(acquired.frameNumber == 8);
    assert(!bridge.acquireLatestFrame(acquired));
    assert(bridge.acquiredFrameCount() == 1);
    assert(bridge.droppedFrameCount() == 0);
}

void testAcquireFrameAfterUsesIndependentCursor()
{
    streaming::FrameBridge bridge;
    streaming::VideoFrameHandle acquired;

    bridge.publishFrame(makeFrame(12));
    assert(bridge.acquireFrameAfter(0, acquired));
    assert(acquired.frameNumber == 12);
    assert(!bridge.acquireFrameAfter(12, acquired));

    assert(bridge.acquireFrameAfter(0, acquired));
    assert(acquired.frameNumber == 12);

    bridge.publishFrame(makeFrame(13));
    assert(bridge.acquireFrameAfter(12, acquired));
    assert(acquired.frameNumber == 13);
}

void testWaitOnlyReturnsFreshFrames()
{
    using namespace std::chrono_literals;

    streaming::FrameBridge bridge;
    streaming::VideoFrameHandle acquired;

    assert(!bridge.waitForNewFrame(acquired, 1ms));
    bridge.publishFrame(makeFrame(9));
    assert(bridge.waitForNewFrame(acquired, 10ms));
    assert(acquired.frameNumber == 9);
    assert(!bridge.waitForNewFrame(acquired, 1ms));

    bridge.publishFrame(makeFrame(10));
    assert(bridge.waitForNewFrame(acquired, 10ms));
    assert(acquired.frameNumber == 10);
    assert(bridge.acquiredFrameCount() == 2);
}

void testWaitWakesWhenFrameIsPublished()
{
    using namespace std::chrono_literals;

    streaming::FrameBridge bridge;
    streaming::VideoFrameHandle acquired;
    bool received = false;

    std::thread consumer([&]() {
        received = bridge.waitForNewFrame(acquired, 500ms);
    });
    std::this_thread::sleep_for(5ms);
    bridge.publishFrame(makeFrame(11));
    consumer.join();

    assert(received);
    assert(acquired.frameNumber == 11);
}

void testIndependentConsumerCanAcquireFrameAfterPreview()
{
    using namespace std::chrono_literals;

    streaming::FrameBridge bridge;
    bridge.publishFrame(makeFrame(30));

    streaming::VideoFrameHandle previewFrame;
    assert(bridge.acquireLatestFrame(previewFrame));
    assert(previewFrame.frameNumber == 30);

    streaming::VideoFrameHandle publisherFrame;
    assert(bridge.waitForFrameAfter(0, publisherFrame, 10ms));
    assert(publisherFrame.frameNumber == 30);
    assert(!bridge.waitForFrameAfter(30, publisherFrame, 1ms));

    bridge.publishFrame(makeFrame(31));
    assert(bridge.waitForFrameAfter(30, publisherFrame, 10ms));
    assert(publisherFrame.frameNumber == 31);
}

void testReleaseTracksLastReleasedFrameNumber()
{
    streaming::FrameBridge bridge;
    assert(!bridge.lastReleasedFrameNumber());

    bridge.releaseFrame(42);
    assert(bridge.lastReleasedFrameNumber());
    assert(*bridge.lastReleasedFrameNumber() == 42);
}

void testFrameCanCarryGenericLifetimeToken()
{
    streaming::FrameBridge bridge;
    auto lifetimeToken = std::make_shared<int>(42);
    auto frame = makeFrame(12);
    frame.lifetimeToken = lifetimeToken;

    bridge.publishFrame(frame);

    streaming::VideoFrameHandle acquired;
    assert(bridge.acquireLatestFrame(acquired));
    assert(acquired.frameNumber == 12);
    assert(acquired.hasLifetimeToken());
    assert(acquired.lifetimeToken == lifetimeToken);
}

void testPreviousLifetimeTokenIsReleasedOutsideBridgeLock()
{
    streaming::FrameBridge bridge;
    bool deleterRan = false;

    {
        auto frame = makeFrame(21);
        frame.lifetimeToken = std::shared_ptr<const void>(
            new int(21), [&bridge, &deleterRan](const void* ptr) {
                delete static_cast<const int*>(ptr);
                (void)bridge.publishedFrameCount();
                deleterRan = true;
            });

        bridge.publishFrame(frame);
    }

    bridge.publishFrame(makeFrame(22));

    assert(deleterRan);
    assert(bridge.publishedFrameCount() == 2);
}

void testThreadSafePublishAcquire()
{
    streaming::FrameBridge bridge;
    constexpr uint64_t frameCount = 1000;

    std::thread producer([&bridge]() {
        for (uint64_t frameNumber = 1; frameNumber <= frameCount; ++frameNumber)
            bridge.publishFrame(makeFrame(frameNumber));
    });

    std::thread consumer([&bridge]() {
        streaming::VideoFrameHandle acquired;
        for (uint64_t attempt = 0; attempt < frameCount; ++attempt)
            (void)bridge.acquireLatestFrame(acquired);
    });

    producer.join();
    consumer.join();

    assert(bridge.publishedFrameCount() == frameCount);
    assert(bridge.acquiredFrameCount() <= bridge.publishedFrameCount());
    assert(bridge.droppedFrameCount() <= bridge.publishedFrameCount());
}

} // namespace

int main()
{
    testEmptyAcquire();
    testPublishOneFrameAcquireSameFrame();
    testAcquireReturnsLatestFrame();
    testDroppedCountOnlyTracksUnacquiredFrames();
    testRepeatedAcquireDoesNotReplaySameFrame();
    testAcquireFrameAfterUsesIndependentCursor();
    testWaitOnlyReturnsFreshFrames();
    testWaitWakesWhenFrameIsPublished();
    testIndependentConsumerCanAcquireFrameAfterPreview();
    testReleaseTracksLastReleasedFrameNumber();
    testFrameCanCarryGenericLifetimeToken();
    testPreviousLifetimeTokenIsReleasedOutsideBridgeLock();
    testThreadSafePublishAcquire();
}
