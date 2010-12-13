//===-- StringExtractor.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Utility/StringExtractor.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes

static inline int
xdigit_to_sint (char ch)
{
    if (ch >= 'a' && ch <= 'f')
        return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F')
        return 10 + ch - 'A';
    return ch - '0';
}

static inline unsigned int
xdigit_to_uint (uint8_t ch)
{
    if (ch >= 'a' && ch <= 'f')
        return 10u + ch - 'a';
    if (ch >= 'A' && ch <= 'F')
        return 10u + ch - 'A';
    return ch - '0';
}

//----------------------------------------------------------------------
// StringExtractor constructor
//----------------------------------------------------------------------
StringExtractor::StringExtractor() :
    m_packet(),
    m_index (0)
{
}


StringExtractor::StringExtractor(const char *packet_cstr) :
    m_packet(),
    m_index (0)
{
    if (packet_cstr)
        m_packet.assign (packet_cstr);
}


//----------------------------------------------------------------------
// StringExtractor copy constructor
//----------------------------------------------------------------------
StringExtractor::StringExtractor(const StringExtractor& rhs) :
    m_packet (rhs.m_packet),
    m_index (rhs.m_index)
{

}

//----------------------------------------------------------------------
// StringExtractor assignment operator
//----------------------------------------------------------------------
const StringExtractor&
StringExtractor::operator=(const StringExtractor& rhs)
{
    if (this != &rhs)
    {
        m_packet = rhs.m_packet;
        m_index = rhs.m_index;

    }
    return *this;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
StringExtractor::~StringExtractor()
{
}


char
StringExtractor::GetChar (char fail_value)
{
    if (m_index < m_packet.size())
    {
        char ch = m_packet[m_index];
        ++m_index;
        return ch;
    }
    m_index = UINT32_MAX;
    return fail_value;
}

uint32_t
StringExtractor::GetNumHexASCIICharsAtFilePos (uint32_t max) const
{
    uint32_t idx = m_index;
    const size_t size = m_packet.size();
    while (idx < size && idx - m_index < max && isxdigit(m_packet[idx]))
        ++idx;
    return idx - m_index;
}
//----------------------------------------------------------------------
// Extract a signed character from two hex ASCII chars in the packet
// string
//----------------------------------------------------------------------
int8_t
StringExtractor::GetHexS8 (int8_t fail_value)
{
    if (GetNumHexASCIICharsAtFilePos(2))
    {
        char hi_nibble_char = m_packet[m_index];
        char lo_nibble_char = m_packet[m_index+1];

        if (isxdigit(hi_nibble_char) && isxdigit(lo_nibble_char))
        {
            char hi_nibble = xdigit_to_sint (hi_nibble_char);
            char lo_nibble = xdigit_to_sint (lo_nibble_char);
            m_index += 2;
            return (hi_nibble << 4) + lo_nibble;
        }
    }
    m_index = UINT32_MAX;
    return fail_value;
}

//----------------------------------------------------------------------
// Extract an unsigned character from two hex ASCII chars in the packet
// string
//----------------------------------------------------------------------
uint8_t
StringExtractor::GetHexU8 (uint8_t fail_value)
{
    if (GetNumHexASCIICharsAtFilePos(2))
    {
        uint8_t hi_nibble_char = m_packet[m_index];
        uint8_t lo_nibble_char = m_packet[m_index+1];

        if (isxdigit(hi_nibble_char) && isxdigit(lo_nibble_char))
        {
            uint8_t hi_nibble = xdigit_to_sint (hi_nibble_char);
            uint8_t lo_nibble = xdigit_to_sint (lo_nibble_char);
            m_index += 2;
            return (hi_nibble << 4) + lo_nibble;
        }
    }
    m_index = UINT32_MAX;
    return fail_value;
}

uint32_t
StringExtractor::GetHexMaxU32 (bool little_endian, uint32_t fail_value)
{
    uint32_t result = 0;
    uint32_t nibble_count = 0;

    if (little_endian)
    {
        uint32_t shift_amount = 0;
        while (m_index < m_packet.size() && ::isxdigit (m_packet[m_index]))
        {
            // Make sure we don't exceed the size of a uint32_t...
            if (nibble_count >= (sizeof(uint32_t) * 2))
            {
                m_index = UINT32_MAX;
                return fail_value;
            }

            uint8_t nibble_lo;
            uint8_t nibble_hi = xdigit_to_sint (m_packet[m_index]);
            ++m_index;
            if (m_index < m_packet.size() && ::isxdigit (m_packet[m_index]))
            {
                nibble_lo = xdigit_to_sint (m_packet[m_index]);
                ++m_index;
                result |= ((uint32_t)nibble_hi << (shift_amount + 4));
                result |= ((uint32_t)nibble_lo << shift_amount);
                nibble_count += 2;
                shift_amount += 8;
            }
            else
            {
                result |= ((uint32_t)nibble_hi << shift_amount);
                nibble_count += 1;
                shift_amount += 4;
            }

        }
    }
    else
    {
        while (m_index < m_packet.size() && ::isxdigit (m_packet[m_index]))
        {
            // Make sure we don't exceed the size of a uint32_t...
            if (nibble_count >= (sizeof(uint32_t) * 2))
            {
                m_index = UINT32_MAX;
                return fail_value;
            }

            uint8_t nibble = xdigit_to_sint (m_packet[m_index]);
            // Big Endian
            result <<= 4;
            result |= nibble;

            ++m_index;
            ++nibble_count;
        }
    }
    return result;
}

uint64_t
StringExtractor::GetHexMaxU64 (bool little_endian, uint64_t fail_value)
{
    uint64_t result = 0;
    uint32_t nibble_count = 0;

    if (little_endian)
    {
        uint32_t shift_amount = 0;
        while (m_index < m_packet.size() && ::isxdigit (m_packet[m_index]))
        {
            // Make sure we don't exceed the size of a uint64_t...
            if (nibble_count >= (sizeof(uint64_t) * 2))
            {
                m_index = UINT32_MAX;
                return fail_value;
            }

            uint8_t nibble_lo;
            uint8_t nibble_hi = xdigit_to_sint (m_packet[m_index]);
            ++m_index;
            if (m_index < m_packet.size() && ::isxdigit (m_packet[m_index]))
            {
                nibble_lo = xdigit_to_sint (m_packet[m_index]);
                ++m_index;
                result |= ((uint64_t)nibble_hi << (shift_amount + 4));
                result |= ((uint64_t)nibble_lo << shift_amount);
                nibble_count += 2;
                shift_amount += 8;
            }
            else
            {
                result |= ((uint64_t)nibble_hi << shift_amount);
                nibble_count += 1;
                shift_amount += 4;
            }

        }
    }
    else
    {
        while (m_index < m_packet.size() && ::isxdigit (m_packet[m_index]))
        {
            // Make sure we don't exceed the size of a uint64_t...
            if (nibble_count >= (sizeof(uint64_t) * 2))
            {
                m_index = UINT32_MAX;
                return fail_value;
            }

            uint8_t nibble = xdigit_to_sint (m_packet[m_index]);
            // Big Endian
            result <<= 4;
            result |= nibble;

            ++m_index;
            ++nibble_count;
        }
    }
    return result;
}

size_t
StringExtractor::GetHexBytes (void *dst_void, size_t dst_len, uint8_t fail_fill_value)
{
    uint8_t *dst = (uint8_t*)dst_void;
    size_t bytes_extracted = 0;
    while (bytes_extracted < dst_len && GetBytesLeft ())
    {
        dst[bytes_extracted] = GetHexU8 (fail_fill_value);
        if (IsGood())
            ++bytes_extracted;
        else
            break;
    }

    for (size_t i = bytes_extracted; i < dst_len; ++i)
        dst[i] = fail_fill_value;

    return bytes_extracted;
}


// Consume ASCII hex nibble character pairs until we have decoded byte_size
// bytes of data.

uint64_t
StringExtractor::GetHexWithFixedSize (uint32_t byte_size, bool little_endian, uint64_t fail_value)
{
    if (byte_size <= 8 && GetBytesLeft() >= byte_size * 2)
    {
        uint64_t result = 0;
        uint32_t i;
        if (little_endian)
        {
            // Little Endian
            uint32_t shift_amount;
            for (i = 0, shift_amount = 0;
                 i < byte_size && m_index != UINT32_MAX;
                 ++i, shift_amount += 8)
            {
                result |= ((uint64_t)GetHexU8() << shift_amount);
            }
        }
        else
        {
            // Big Endian
            for (i = 0; i < byte_size && m_index != UINT32_MAX; ++i)
            {
                result <<= 8;
                result |= GetHexU8();
            }
        }
    }
    m_index = UINT32_MAX;
    return fail_value;
}

bool
StringExtractor::GetNameColonValue (std::string &name, std::string &value)
{
    // Read something in the form of NNNN:VVVV; where NNNN is any character
    // that is not a colon, followed by a ':' character, then a value (one or
    // more ';' chars), followed by a ';'
    if (m_index < m_packet.size())
    {
        const size_t colon_idx = m_packet.find (':', m_index);
        if (colon_idx != std::string::npos)
        {
            const size_t semicolon_idx = m_packet.find (';', colon_idx);
            if (semicolon_idx != std::string::npos)
            {
                name.assign (m_packet, m_index, colon_idx - m_index);
                value.assign (m_packet, colon_idx + 1, semicolon_idx - (colon_idx + 1));
                m_index = semicolon_idx + 1;
                return true;
            }
        }
    }
    m_index = UINT32_MAX;
    return false;
}
