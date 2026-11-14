#include "SharedSurfaceProtocol.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using namespace Blaze::Gpu;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

SharedSurfaceDescriptor makeDescriptor()
{
    SharedSurfaceDescriptor value{};
    value.structSize = sizeof(value);
    value.protocolVersion = kSurfaceProtocolVersion;
    value.viewIdentifier = 42;
    value.resizeGeneration = 1;
    value.width = 1280;
    value.height = 720;
    value.pixelFormat = kPixelFormatBgra8Unorm;
    value.synchronizationKind = kSyncKeyedMutex;
    value.slotCount = 3;
    value.adapter = { 0x12345678, 1 };
    value.slots = {{
        { 1001, 0, 1, 2 },
        { 1002, 0, 1, 2 },
        { 1003, 0, 1, 2 }
    }};
    return value;
}
}

void exerciseConcurrentRing()
{
    constexpr int producerCount = 4;
    constexpr int framesPerProducer = 1500;
    BoundedFrameRing ring(3);
    require(ring.beginGeneration(1), "concurrent ring generation rejected");

    std::atomic<int> activeProducers{ producerCount };
    std::atomic<std::uint64_t> published{ 0 };
    std::atomic<std::uint64_t> presented{ 0 };
    std::atomic<std::uint64_t> dropped{ 0 };
    std::atomic<bool> failed{ false };
    std::vector<std::thread> producers;
    producers.reserve(producerCount);

    for (int producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back([&] {
            for (int frame = 0; frame < framesPerProducer; ++frame) {
                FrameTicket ticket{};
                while (!ring.tryBeginRender(ticket))
                    std::this_thread::yield();
                if (!ring.publish(ticket)) {
                    failed.store(true);
                    break;
                }
                published.fetch_add(1);
            }
            activeProducers.fetch_sub(1);
        });
    }

    std::thread presenter([&] {
        while (activeProducers.load() != 0
            || presented.load() + dropped.load() < published.load()) {
            FrameTicket ticket{};
            std::array<FrameTicket, kMaximumRingSlots> droppedTickets{};
            std::uint32_t droppedNow = 0;
            if (!ring.tryBeginPresent(ticket, droppedTickets, droppedNow)) {
                std::this_thread::yield();
                continue;
            }
            for (std::uint32_t index = 0; index < droppedNow; ++index) {
                if (!ring.completeDrop(droppedTickets[index]))
                    failed.store(true);
                else
                    dropped.fetch_add(1);
            }
            if (ring.completePresent(ticket) != PresentResult::Presented)
                failed.store(true);
            else
                presented.fetch_add(1);
        }
    });

    for (auto& producer : producers)
        producer.join();
    presenter.join();
    require(!failed.load(), "concurrent ring reported an invalid state transition");
    require(published.load() == producerCount * framesPerProducer,
            "concurrent producers did not publish every reserved frame");
    require(presented.load() + dropped.load() == published.load(),
            "presenter lost or double-counted a published frame");
    require(ring.slotCount() == 3, "concurrent access corrupted ring configuration");
}

int main()
{
    auto descriptor = makeDescriptor();
    require(isValid(descriptor), "valid BGRA8 three-slot descriptor rejected");

    auto malformed = descriptor;
    malformed.slotCount = 4;
    require(!isValid(malformed), "unbounded slot count accepted");
    malformed = descriptor;
    malformed.slots[1].sharedResourceToken = malformed.slots[0].sharedResourceToken;
    require(!isValid(malformed), "duplicate shared-resource token accepted");
    malformed = descriptor;
    malformed.width = kMaximumSurfaceDimension + 1;
    require(!isValid(malformed), "oversized surface accepted");

    require(BoundedFrameRing(1).slotCount() == 0, "undersized frame ring accepted");
    require(BoundedFrameRing(4).slotCount() == 0, "oversized frame ring accepted");
    BoundedFrameRing ring(3);
    require(ring.beginGeneration(1), "initial generation rejected");
    FrameTicket first{}, second{}, third{}, fourth{}, presented{};
    require(ring.tryBeginRender(first), "first render reservation failed");
    require(ring.tryBeginRender(second), "second render reservation failed");
    require(ring.tryBeginRender(third), "third render reservation failed");
    require(!ring.tryBeginRender(fourth), "ring allowed an unbounded fourth frame");
    require(ring.publish(first) && ring.publish(second) && ring.publish(third),
            "valid render publication failed");

    std::uint32_t dropped = 0;
    std::array<FrameTicket, kMaximumRingSlots> droppedTickets{};
    require(ring.tryBeginPresent(presented, droppedTickets, dropped), "completed frame not available");
    require(presented.sequence == third.sequence && dropped == 2,
            "presenter did not select newest frame and mark stale frames for handoff");
    require(droppedTickets[0].sequence == first.sequence && droppedTickets[1].sequence == second.sequence,
            "stale-frame handoff tickets were not returned to the consumer");
    require(ring.completeDrop(droppedTickets[0]) && ring.completeDrop(droppedTickets[1]),
            "stale frames could not be released after their consumer handoff");
    require(!ring.completeDrop(droppedTickets[0]), "double stale-frame completion was accepted");
    require(ring.completePresent(presented) == PresentResult::Presented,
            "present completion failed");
    require(ring.completePresent(presented) == PresentResult::Invalid,
            "double completion was accepted");

    FrameReadyMessage ready{};
    ready.structSize = sizeof(ready);
    ready.protocolVersion = kSurfaceProtocolVersion;
    ready.viewIdentifier = descriptor.viewIdentifier;
    ready.resizeGeneration = descriptor.resizeGeneration;
    ready.frameSequence = presented.sequence;
    ready.slotIndex = presented.slotIndex;
    ready.producerReleaseKey = 1;
    ready.consumerReleaseKey = 2;
    ready.producerQpc = 1234;
    require(isValid(ready, descriptor), "valid frame-ready message rejected");
    auto invalidReady = ready;
    invalidReady.producerReleaseKey = 99;
    require(!isValid(invalidReady, descriptor), "unknown producer release key accepted");
    invalidReady = ready;
    invalidReady.consumerReleaseKey = 99;
    require(!isValid(invalidReady, descriptor), "unknown consumer return key accepted");
    invalidReady = ready;
    invalidReady.reserved = 1;
    require(!isValid(invalidReady, descriptor), "nonzero frame reserved field accepted");
    invalidReady = ready;
    invalidReady.producerQpc = 0;
    require(!isValid(invalidReady, descriptor), "frame without producer timestamp accepted");
    malformed = descriptor;
    malformed.slots[0].consumerReturnKey = malformed.slots[0].consumerAcquireKey;
    require(!isValid(malformed), "colliding keyed-mutex ownership keys accepted");
    ready.resizeGeneration++;
    require(!isValid(ready, descriptor), "stale-generation frame-ready message accepted");

    FrameTicket inFlight{};
    require(ring.tryBeginRender(inFlight), "render reservation before resize failed");
    require(ring.beginGeneration(2), "new resize generation rejected");
    require(!ring.publish(inFlight), "old-generation frame published after resize");
    require(ring.completePresent(inFlight) == PresentResult::Invalid,
            "old-generation ticket completed after resize");
    require(!ring.beginGeneration(2), "duplicate resize generation accepted");
    require(ring.tryBeginRender(fourth) && fourth.generation == 2
            && fourth.sequence > presented.sequence,
            "new generation did not preserve globally monotonic frame sequence");

    exerciseConcurrentRing();

    std::cout << "PASS: versioned descriptor validation, thread-safe bounded three-slot frame ring, "
                 "monotonic frame IDs, newest-frame selection, explicit stale-frame handoff, "
                 "resize generations, stale-ticket rejection, and concurrent producer/presenter stress.\n";
    return 0;
}
