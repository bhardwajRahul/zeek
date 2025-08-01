// See the file "COPYING" in the main distribution directory for copyright.

#include "zeek/probabilistic/BitVector.h"

#include <openssl/sha.h>
#include <cassert>
#include <cstring>
#include <limits>

#include "zeek/broker/Data.h"
#include "zeek/digest.h"

namespace zeek::probabilistic::detail {

BitVector::size_type BitVector::npos = static_cast<BitVector::size_type>(-1);
BitVector::block_type BitVector::bits_per_block = std::numeric_limits<BitVector::block_type>::digits;

namespace {

constexpr uint8_t count_table[] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3,
                                   3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4,
                                   3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4,
                                   4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5,
                                   3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1, 2,
                                   2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5,
                                   4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5,
                                   5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
                                   3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5,
                                   5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};

} // namespace

BitVector::Reference::Reference(block_type& block, block_type i) : block(block), mask((block_type(1) << i)) {
    assert(i < bits_per_block);
}

BitVector::Reference& BitVector::Reference::Flip() {
    block ^= mask;
    return *this;
}

BitVector::Reference::operator bool() const { return (block & mask) != 0; }

bool BitVector::Reference::operator~() const { return (block & mask) == 0; }

BitVector::Reference& BitVector::Reference::operator=(bool x) {
    if ( x )
        block |= mask;
    else
        block &= ~mask;

    return *this;
}

BitVector::Reference& BitVector::Reference::operator=(const Reference& other) {
    if ( other )
        block |= mask;
    else
        block &= ~mask;

    return *this;
}

BitVector::Reference& BitVector::Reference::operator|=(bool x) {
    if ( x )
        block |= mask;

    return *this;
}

BitVector::Reference& BitVector::Reference::operator&=(bool x) {
    if ( ! x )
        block &= ~mask;

    return *this;
}

BitVector::Reference& BitVector::Reference::operator^=(bool x) {
    if ( x )
        block ^= mask;

    return *this;
}

BitVector::Reference& BitVector::Reference::operator-=(bool x) {
    if ( x )
        block &= ~mask;

    return *this;
}

BitVector::BitVector() { num_bits = 0; }

BitVector::BitVector(size_type size, bool value) : bits(bits_to_blocks(size), value ? ~block_type(0) : 0) {
    num_bits = size;
}

BitVector::BitVector(BitVector const& other) : bits(other.bits) { num_bits = other.num_bits; }

BitVector BitVector::operator~() const {
    BitVector b(*this);
    b.Flip();
    return b;
}

BitVector& BitVector::operator=(BitVector const& other) {
    bits = other.bits;
    return *this;
}

BitVector BitVector::operator<<(size_type n) const {
    BitVector b(*this);
    return b <<= n;
}

BitVector BitVector::operator>>(size_type n) const {
    BitVector b(*this);
    return b >>= n;
}

BitVector& BitVector::operator<<=(size_type n) {
    if ( n >= num_bits )
        return Reset();

    if ( n > 0 ) {
        size_type last = Blocks() - 1;
        size_type div = n / bits_per_block;
        block_type r = bit_index(n);
        block_type* b = &bits[0];

        assert(Blocks() >= 1);
        assert(div <= last);

        if ( r != 0 ) {
            for ( size_type i = last - div; i > 0; --i )
                b[i + div] = (b[i] << r) | (b[i - 1] >> (bits_per_block - r));

            b[div] = b[0] << r;
        }

        else {
            for ( size_type i = last - div; i > 0; --i )
                b[i + div] = b[i];

            b[div] = b[0];
        }

        std::fill_n(b, div, block_type(0));
        zero_unused_bits();
    }

    return *this;
}

BitVector& BitVector::operator>>=(size_type n) {
    if ( n >= num_bits )
        return Reset();

    if ( n > 0 ) {
        size_type last = Blocks() - 1;
        size_type div = n / bits_per_block;
        block_type r = bit_index(n);
        block_type* b = &bits[0];

        assert(Blocks() >= 1);
        assert(div <= last);

        if ( r != 0 ) {
            for ( size_type i = last - div; i > 0; --i )
                b[i - div] = (b[i] >> r) | (b[i + 1] << (bits_per_block - r));

            b[last - div] = b[last] >> r;
        }

        else {
            for ( size_type i = div; i <= last; ++i )
                b[i - div] = b[i];
        }

        std::fill_n(b + (Blocks() - div), div, block_type(0));
    }

    return *this;
}

BitVector& BitVector::operator&=(BitVector const& other) {
    assert(Size() >= other.Size());

    for ( size_type i = 0; i < Blocks(); ++i )
        bits[i] &= other.bits[i];

    return *this;
}

BitVector& BitVector::operator|=(BitVector const& other) {
    assert(Size() >= other.Size());

    for ( size_type i = 0; i < Blocks(); ++i )
        bits[i] |= other.bits[i];

    return *this;
}

BitVector& BitVector::operator^=(BitVector const& other) {
    assert(Size() >= other.Size());

    for ( size_type i = 0; i < Blocks(); ++i )
        bits[i] ^= other.bits[i];

    return *this;
}

BitVector& BitVector::operator-=(BitVector const& other) {
    assert(Size() >= other.Size());

    for ( size_type i = 0; i < Blocks(); ++i )
        bits[i] &= ~other.bits[i];

    return *this;
}

BitVector operator&(BitVector const& x, BitVector const& y) {
    BitVector b(x);
    return b &= y;
}

BitVector operator|(BitVector const& x, BitVector const& y) {
    BitVector b(x);
    return b |= y;
}

BitVector operator^(BitVector const& x, BitVector const& y) {
    BitVector b(x);
    return b ^= y;
}

BitVector operator-(BitVector const& x, BitVector const& y) {
    BitVector b(x);
    return b -= y;
}

bool operator==(BitVector const& x, BitVector const& y) { return x.num_bits == y.num_bits && x.bits == y.bits; }

bool operator!=(BitVector const& x, BitVector const& y) { return ! (x == y); }

bool operator<(BitVector const& x, BitVector const& y) {
    assert(x.Size() == y.Size());

    for ( BitVector::size_type r = x.Blocks(); r > 0; --r ) {
        BitVector::size_type i = r - 1;

        if ( x.bits[i] < y.bits[i] )
            return true;

        else if ( x.bits[i] > y.bits[i] )
            return false;
    }

    return false;
}

void BitVector::Resize(size_type n, bool value) {
    size_type old = Blocks();
    size_type required = bits_to_blocks(n);
    block_type block_value = value ? ~block_type(0) : block_type(0);

    if ( required != old )
        bits.resize(required, block_value);

    if ( value && (n > num_bits) && extra_bits() )
        bits[old - 1] |= (block_value << extra_bits());

    num_bits = n;
    zero_unused_bits();
}

void BitVector::Clear() {
    bits.clear();
    num_bits = 0;
}

void BitVector::PushBack(bool bit) {
    size_type s = Size();
    Resize(s + 1);
    Set(s, bit);
}

void BitVector::Append(block_type block) {
    size_type excess = extra_bits();

    if ( excess ) {
        assert(! Empty());
        bits.push_back(block >> (bits_per_block - excess));
        bits[Blocks() - 2] |= (block << excess);
    }

    else {
        bits.push_back(block);
    }

    num_bits += bits_per_block;
}

BitVector& BitVector::Set(size_type i, bool bit) {
    assert(i < num_bits);

    if ( bit )
        bits[block_index(i)] |= bit_mask(i);
    else
        Reset(i);

    return *this;
}

BitVector& BitVector::Set() {
    std::ranges::fill(bits, ~block_type(0));
    zero_unused_bits();
    return *this;
}

BitVector& BitVector::Reset(size_type i) {
    assert(i < num_bits);
    bits[block_index(i)] &= ~bit_mask(i);
    return *this;
}

BitVector& BitVector::Reset() {
    std::ranges::fill(bits, block_type(0));
    return *this;
}

BitVector& BitVector::Flip(size_type i) {
    assert(i < num_bits);
    bits[block_index(i)] ^= bit_mask(i);
    return *this;
}

BitVector& BitVector::Flip() {
    for ( size_type i = 0; i < Blocks(); ++i )
        bits[i] = ~bits[i];

    zero_unused_bits();
    return *this;
}

bool BitVector::operator[](size_type i) const {
    assert(i < num_bits);
    return (bits[block_index(i)] & bit_mask(i)) != 0;
}

BitVector::Reference BitVector::operator[](size_type i) {
    assert(i < num_bits);
    return {bits[block_index(i)], bit_index(i)};
}

BitVector::size_type BitVector::Count() const {
    std::vector<block_type>::const_iterator first = bits.begin();
    size_t n = 0;
    size_type length = Blocks();

    while ( length ) {
        block_type block = *first;

        while ( block ) {
            // TODO: use _popcnt if available.
            n += count_table[block & ((1u << 8) - 1)];
            block >>= 8;
        }

        ++first;
        --length;
    }

    return n;
}

BitVector::size_type BitVector::Blocks() const { return bits.size(); }

BitVector::size_type BitVector::Size() const { return num_bits; }

bool BitVector::Empty() const { return bits.empty(); }

bool BitVector::AllZero() const {
    for ( unsigned long long bit : bits ) {
        if ( bit )
            return false;
    }

    return true;
}

BitVector::size_type BitVector::FindFirst() const { return find_from(0); }

BitVector::size_type BitVector::FindNext(size_type i) const {
    if ( i >= (Size() - 1) || Size() == 0 )
        return npos;

    ++i;
    size_type bi = block_index(i);
    block_type block = bits[bi] & (~block_type(0) << bit_index(i));
    return block ? bi * bits_per_block + lowest_bit(block) : find_from(bi + 1);
}

uint64_t BitVector::Hash() const {
    u_char buf[SHA256_DIGEST_LENGTH];
    uint64_t digest;
    auto* ctx = zeek::detail::hash_init(zeek::detail::Hash_SHA256);

    for ( size_type i = 0; i < Blocks(); ++i )
        zeek::detail::hash_update(ctx, &bits[i], sizeof(bits[i]));

    zeek::detail::hash_final(ctx, buf);
    memcpy(&digest, buf, sizeof(digest)); // Use the first bytes as digest
    return digest;
}

std::optional<BrokerData> BitVector::Serialize() const {
    BrokerListBuilder builder;
    builder.Reserve(2 + bits.size());

    builder.AddCount(num_bits);
    builder.AddCount(bits.size());

    for ( auto bit : bits )
        builder.AddCount(bit);

    return std::move(builder).Build();
}

std::unique_ptr<BitVector> BitVector::Unserialize(BrokerDataView data) {
    if ( ! data.IsList() )
        return nullptr;

    auto v = data.ToList();

    if ( v.Size() < 2 || ! v[0].IsCount() || ! v[1].IsCount() )
        return nullptr;

    auto num_bits = v[0].ToCount();
    auto size = v[1].ToCount();

    if ( v.Size() != 2 + size )
        return nullptr;

    auto bv = std::make_unique<BitVector>();
    bv->num_bits = num_bits;

    for ( size_t i = 0; i < size; ++i ) {
        auto x = v[2 + i];
        if ( ! x.IsCount() )
            return nullptr;

        bv->bits.push_back(x.ToCount());
    }

    return bv;
}

BitVector::size_type BitVector::lowest_bit(block_type block) {
    block_type x = block - (block & (block - 1));
    size_type log = 0;

    while ( x >>= 1 )
        ++log;

    return log;
}

BitVector::block_type BitVector::extra_bits() const { return bit_index(Size()); }

void BitVector::zero_unused_bits() {
    if ( extra_bits() )
        bits.back() &= ~(~block_type(0) << extra_bits());
}

BitVector::size_type BitVector::find_from(size_type i) const {
    while ( i < Blocks() && bits[i] == 0 )
        ++i;

    if ( i >= Blocks() )
        return npos;

    return i * bits_per_block + lowest_bit(bits[i]);
}

} // namespace zeek::probabilistic::detail
