/*
 * libjingle
 * Copyright 2012 Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "talk/base/basictypes.h"

#include "talk/base/gunit.h"

namespace talk_base {

TEST(BasicTypesTest, Endian) {
  uint16 v16 = 0x1234u;
  uint8 first_byte = *reinterpret_cast<uint8*>(&v16);
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  EXPECT_EQ(0x34u, first_byte);
#elif defined(ARCH_CPU_BIG_ENDIAN)
  EXPECT_EQ(0x12u, first_byte);
#endif
}

TEST(BasicTypesTest, SizeOf) {
  EXPECT_EQ(1u, sizeof(int8));  // NOLINT Using sizeof(type)
  EXPECT_EQ(1u, sizeof(uint8));  // NOLINT
  EXPECT_EQ(2u, sizeof(int16));  // NOLINT
  EXPECT_EQ(2u, sizeof(uint16));  // NOLINT
  EXPECT_EQ(4u, sizeof(int32));  // NOLINT
  EXPECT_EQ(4u, sizeof(uint32));  // NOLINT
  EXPECT_EQ(8u, sizeof(int64));  // NOLINT
  EXPECT_EQ(8u, sizeof(uint64));  // NOLINT
}

// TODO: Test all macros in basictypes.h

}  // namespace talk_base
