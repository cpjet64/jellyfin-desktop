#include "frame_acceptance.h"

#include <cstdlib>
#include <memory>

#define assert(condition) do { if (!(condition)) std::abort(); } while (false)

using namespace fixture_restart;

static FrameKey key{1, 2, 3, 4, 5, 6};

static std::shared_ptr<FrameRecord> frame(int index, std::uint64_t serial = 2)
{
    auto r = std::make_shared<FrameRecord>();
    r->key = key;
    r->serial = serial;
    r->thread = 10;
    r->actualContext = 11;
    r->width = 160;
    r->height = 90;
    r->rgba.resize(160 * 90 * 4);
    const int square = (index * 3) % 140;
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x) {
            int v = 32;
            if (y >= 35 && y < 55 && x >= square && x < square + 20)
                v = 220;
            for (int bit = 0; bit < 9; ++bit)
                if (y >= 8 && y < 20 && x >= 5 + bit * 16 && x < 17 + bit * 16)
                    v = index & (1 << bit) ? 235 : 60;
            const auto p = (y * 160 + x) * 4;
            r->rgba[p] = r->rgba[p + 1] = r->rgba[p + 2] = static_cast<std::uint8_t>(v);
            r->rgba[p + 3] = 255;
        }
    return r;
}

static FrameAcceptance armed()
{
    FrameAcceptance a;
    assert(a.baseline(frame(0, 1)));
    assert(a.arm(key, 1, 3000));
    return a;
}

static void decoderCases()
{
    for (int i = 0; i < 300; ++i)
        assert(decodeFrame(*frame(i)) == i);
    assert(!decodeFrame(*frame(300)));
    auto bad = frame(90);
    bad->width = 90;
    assert(!decodeFrame(*bad));
    bad = frame(90);
    std::fill(bad->rgba.begin(), bad->rgba.end(), std::uint8_t{0});
    assert(!decodeFrame(*bad));
    bad = frame(90);
    for (int y = 10; y < 18; ++y)
        for (int x = 7; x < 15; ++x)
            for (int c = 0; c < 3; ++c)
                bad->rgba[(y * 160 + x) * 4 + c] = 140;
    assert(!decodeFrame(*bad));
    bad = frame(90);
    for (int y = 0; y < 45; ++y)
        for (int x = 0; x < 160 * 4; ++x)
            std::swap(bad->rgba[y * 160 * 4 + x], bad->rgba[(89 - y) * 160 * 4 + x]);
    assert(!decodeFrame(*bad));
}

static void orderingAndIdentity()
{
    auto a = armed();
    auto unchanged = frame(0);
    unchanged->updateFlags = unchanged->nextFlags = ~std::uint64_t{};
    a.capture(unchanged);
    a.acknowledge(key, true, true, true);
    assert(!a.accepted());
    auto foreign = frame(90);
    foreign->key.process++;
    a.capture(foreign);
    foreign = frame(90);
    foreign->key.seek++;
    a.capture(foreign);
    a.capture(frame(90, 1));
    assert(!a.accepted());
    auto failed = frame(90);
    failed->renderResult = -1;
    a.capture(failed);
    a.capture(frame(86));
    assert(!a.accepted());
    a.capture(frame(90));
    assert(a.accepted() && a.target()->serial == 2);
    a.invalidate();
    a.capture(frame(90, 3));
    assert(!a.accepted());
    a = armed();
    a.capture(frame(90));
    assert(!a.accepted());
    a.acknowledge(key, false, true, true);
    a.acknowledge(key, true, false, false);
    assert(!a.accepted());
    a.acknowledge(key, false, true, false);
    assert(!a.accepted());
    a.acknowledge(key, false, false, true);
    assert(a.accepted());
    auto replacement = key;
    ++replacement.seek;
    assert(a.arm(replacement, 2, 3000));
    a.capture(frame(90, 3));
    a.acknowledge(replacement, true, true, true);
    assert(!a.accepted());
}

static void mutableAliasCannotChangeEvidence()
{
    FrameAcceptance a;
    auto base = frame(0, 1);
    assert(a.baseline(base));
    base->rgba = frame(90)->rgba;
    assert(a.arm(key, 1, 3000));
    auto target = frame(90);
    a.capture(target);
    target->rgba = frame(0)->rgba;
    target->key.process++;
    a.acknowledge(key, true, true, true);
    assert(a.accepted());
    assert(decodeFrame(*a.target()) == 90);
    assert(a.target()->key == key);
}

int main()
{
    decoderCases();
    orderingAndIdentity();
    mutableAliasCannotChangeEvidence();
}
