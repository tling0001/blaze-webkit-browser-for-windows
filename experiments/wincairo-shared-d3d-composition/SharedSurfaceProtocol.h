#pragma once

// Versioned, fixed-width ABI shared by the browser process and WebKit GPU process.
// This header deliberately contains no WinUI, WebKit, COM, or STL types so it can
// be included by both sides of the eventual IPC bridge.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <type_traits>

namespace Blaze::Gpu {

inline constexpr std::uint32_t kSurfaceProtocolVersion = 2;
inline constexpr std::uint32_t kMinimumRingSlots = 2;
inline constexpr std::uint32_t kMaximumRingSlots = 3;
inline constexpr std::uint32_t kMaximumSurfaceDimension = 16384;
inline constexpr std::uint32_t kPixelFormatBgra8Unorm = 87; // DXGI_FORMAT_B8G8R8A8_UNORM
inline constexpr std::uint32_t kSyncKeyedMutex = 1;

struct AdapterLuid {
    std::uint32_t lowPart;
    std::int32_t highPart;
};

struct SharedTextureSlotDescriptor {
    // Legacy DXGI shared-resource token, not an NT HANDLE. Its interpretation is
    // tied to the pinned ANGLE import extension and must not be CloseHandle'd.
    std::uint64_t sharedResourceToken;
    std::uint64_t producerAcquireKey;
    std::uint64_t consumerAcquireKey;
    // Key released by the consumer after presentation; the producer reacquires it.
    std::uint64_t consumerReturnKey;
};

struct SharedSurfaceDescriptor {
    std::uint32_t structSize;
    std::uint32_t protocolVersion;
    std::uint64_t viewIdentifier;
    std::uint64_t resizeGeneration;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pixelFormat;
    std::uint32_t synchronizationKind;
    std::uint32_t slotCount;
    std::uint32_t reserved;
    AdapterLuid adapter;
    std::array<SharedTextureSlotDescriptor, kMaximumRingSlots> slots;
};

struct FrameReadyMessage {
    std::uint32_t structSize;
    std::uint32_t protocolVersion;
    std::uint64_t viewIdentifier;
    std::uint64_t resizeGeneration;
    std::uint64_t frameSequence;
    std::uint32_t slotIndex;
    std::uint32_t reserved;
    std::uint64_t producerReleaseKey;
    std::uint64_t consumerReleaseKey;
    std::uint64_t producerQpc;
};

constexpr bool isValid(const SharedSurfaceDescriptor& descriptor) noexcept
{
    if (descriptor.structSize != sizeof(SharedSurfaceDescriptor)
        || descriptor.protocolVersion != kSurfaceProtocolVersion
        || !descriptor.viewIdentifier || !descriptor.resizeGeneration
        || !descriptor.width || !descriptor.height
        || descriptor.width > kMaximumSurfaceDimension
        || descriptor.height > kMaximumSurfaceDimension
        || descriptor.pixelFormat != kPixelFormatBgra8Unorm
        || descriptor.synchronizationKind != kSyncKeyedMutex
        || descriptor.slotCount < kMinimumRingSlots
        || descriptor.slotCount > kMaximumRingSlots
        || descriptor.reserved != 0)
        return false;

    for (std::uint32_t i = 0; i < descriptor.slotCount; ++i) {
        const auto& slot = descriptor.slots[i];
        if (!slot.sharedResourceToken
            || slot.producerAcquireKey == slot.consumerAcquireKey
            || slot.producerAcquireKey == slot.consumerReturnKey
            || slot.consumerAcquireKey == slot.consumerReturnKey)
            return false;
        for (std::uint32_t j = 0; j < i; ++j) {
            if (slot.sharedResourceToken == descriptor.slots[j].sharedResourceToken)
                return false;
        }
    }
    return true;
}

constexpr bool isValid(const FrameReadyMessage& frame,
                       const SharedSurfaceDescriptor& descriptor) noexcept
{
    return frame.structSize == sizeof(FrameReadyMessage)
        && frame.protocolVersion == kSurfaceProtocolVersion
        && frame.viewIdentifier == descriptor.viewIdentifier
        && frame.resizeGeneration == descriptor.resizeGeneration
        && frame.frameSequence != 0
        && frame.slotIndex < descriptor.slotCount
        && frame.reserved == 0
        && frame.producerQpc != 0
        && (frame.producerReleaseKey == descriptor.slots[frame.slotIndex].producerAcquireKey
            || frame.producerReleaseKey == descriptor.slots[frame.slotIndex].consumerAcquireKey)
        && frame.consumerReleaseKey == descriptor.slots[frame.slotIndex].consumerReturnKey;
}

enum class SlotState : std::uint8_t { Free, Rendering, Ready, Presenting };

struct FrameTicket {
    std::uint64_t generation{};
    std::uint64_t sequence{};
    std::uint32_t slotIndex{};
};

enum class PresentResult : std::uint8_t { Presented, Dropped, Invalid };

class BoundedFrameRing {
public:
    explicit BoundedFrameRing(std::uint32_t slotCount) noexcept
        : m_slotCount(slotCount >= kMinimumRingSlots && slotCount <= kMaximumRingSlots ? slotCount : 0)
    {
    }

    // State transitions are serialized across producer and presenter threads.
    // Never hold this lock while waiting on a D3D fence/keyed mutex or presenting.
    // A full ring drops work instead of blocking.
    bool tryBeginRender(FrameTicket& ticket) noexcept
    {
        std::lock_guard lock(m_mutex);
        if (!m_slotCount || m_generation == 0
            || m_nextSequence == std::numeric_limits<std::uint64_t>::max())
            return false;
        for (std::uint32_t i = 0; i < m_slotCount; ++i) {
            auto& slot = m_slots[i];
            if (slot.state != SlotState::Free)
                continue;
            slot.state = SlotState::Rendering;
            slot.sequence = ++m_nextSequence;
            slot.generation = m_generation;
            ticket = { m_generation, slot.sequence, i };
            return true;
        }
        return false;
    }

    bool publish(const FrameTicket& ticket) noexcept
    {
        std::lock_guard lock(m_mutex);
        auto* slot = find(ticket);
        if (!slot || slot->state != SlotState::Rendering)
            return false;
        slot->state = SlotState::Ready;
        return true;
    }

    // Chooses the newest completed frame and releases older ready frames.
    // The UI thread only polls this state; it never waits for a producer fence.
    bool tryBeginPresent(FrameTicket& ticket, std::uint32_t& dropped) noexcept
    {
        std::lock_guard lock(m_mutex);
        dropped = 0;
        Slot* newest = nullptr;
        std::uint32_t newestIndex = 0;
        for (std::uint32_t i = 0; i < m_slotCount; ++i) {
            auto& slot = m_slots[i];
            if (slot.state == SlotState::Ready
                && (!newest || slot.sequence > newest->sequence)) {
                newest = &slot;
                newestIndex = i;
            }
        }
        if (!newest)
            return false;

        for (std::uint32_t i = 0; i < m_slotCount; ++i) {
            auto& slot = m_slots[i];
            if (i != newestIndex && slot.state == SlotState::Ready) {
                slot.state = SlotState::Free;
                ++dropped;
            }
        }
        newest->state = SlotState::Presenting;
        ticket = { newest->generation, newest->sequence, newestIndex };
        return true;
    }

    PresentResult completePresent(const FrameTicket& ticket) noexcept
    {
        std::lock_guard lock(m_mutex);
        auto* slot = find(ticket);
        if (!slot || slot->state != SlotState::Presenting)
            return PresentResult::Invalid;
        slot->state = SlotState::Free;
        return PresentResult::Presented;
    }

    // A resize creates a new immutable generation. Tickets from the old
    // generation can no longer publish or complete against the replacement.
    bool beginGeneration(std::uint64_t generation) noexcept
    {
        std::lock_guard lock(m_mutex);
        if (!m_slotCount || !generation || generation <= m_generation)
            return false;
        m_generation = generation;
        for (auto& slot : m_slots)
            slot = {};
        return true;
    }

    std::uint64_t generation() const noexcept { std::lock_guard lock(m_mutex); return m_generation; }
    std::uint64_t lastIssuedSequence() const noexcept { std::lock_guard lock(m_mutex); return m_nextSequence; }
    std::uint32_t slotCount() const noexcept { std::lock_guard lock(m_mutex); return m_slotCount; }

private:
    struct Slot {
        SlotState state{ SlotState::Free };
        std::uint64_t generation{};
        std::uint64_t sequence{};
    };

    constexpr Slot* find(const FrameTicket& ticket) noexcept
    {
        if (ticket.slotIndex >= m_slotCount || ticket.generation != m_generation)
            return nullptr;
        auto& slot = m_slots[ticket.slotIndex];
        if (slot.generation != ticket.generation || slot.sequence != ticket.sequence)
            return nullptr;
        return &slot;
    }

    mutable std::mutex m_mutex;
    std::array<Slot, kMaximumRingSlots> m_slots{};
    std::uint32_t m_slotCount{};
    std::uint64_t m_generation{};
    std::uint64_t m_nextSequence{};
};

static_assert(std::is_standard_layout_v<SharedSurfaceDescriptor>);
static_assert(std::is_trivially_copyable_v<SharedSurfaceDescriptor>);
static_assert(std::is_standard_layout_v<FrameReadyMessage>);
static_assert(std::is_trivially_copyable_v<FrameReadyMessage>);
static_assert(sizeof(SharedSurfaceDescriptor) == 152, "Shared-surface IPC ABI changed; update the protocol version and consumers.");
static_assert(sizeof(FrameReadyMessage) == 64, "Frame-ready IPC ABI changed; update the protocol version and consumers.");

} // namespace Blaze::Gpu
