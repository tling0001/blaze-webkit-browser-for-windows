#include "SharedSurfaceProtocol.h"

#include <cstdlib>
#include <iostream>

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
    require(ring.tryBeginPresent(presented, dropped), "completed frame not available");
    require(presented.sequence == third.sequence && dropped == 2,
            "presenter did not select newest frame and drop stale ready frames");
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

    std::cout << "PASS: versioned descriptor validation, bounded three-slot frame ring, "
                 "monotonic frame IDs, newest-frame selection, stale-frame dropping, "
                 "resize generations, and stale-ticket rejection.\n";
    return 0;
}
