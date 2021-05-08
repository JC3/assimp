/*
---------------------------------------------------------------------------
Open Asset Import Library (assimp)
---------------------------------------------------------------------------

Copyright (c) 2006-2021, assimp team



All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the following
conditions are met:

* Redistributions of source code must retain the above
copyright notice, this list of conditions and the
following disclaimer.

* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the
following disclaimer in the documentation and/or other
materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
contributors may be used to endorse or promote products
derived from this software without specific prior
written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------
*/

#include "UnitTestPCH.h"
#include <assimp/IOStreamBuffer.h>
#include "TestIOStream.h"
#include "UnitTestFileGenerator.h"
#include <type_traits>

// Require variable to be a null-terminated array of char-sized elements.
#define AI_ASSERT_STRINGY_DATA(v) do { \
    ASSERT_EQ(1, sizeof(std::remove_extent<decltype(v)>::type)) << "Fix me: test only works for char data."; \
    ASSERT_EQ(0, (v)[sizeof(v) - 1]) << "Fix me: test assumes null-terminated data."; \
    } while (0)

class IOStreamBufferTest : public ::testing::Test {
    // empty
};

using namespace Assimp;

TEST_F( IOStreamBufferTest, creationTest ) {
    bool ok( true );
    try {
        IOStreamBuffer<char> myBuffer;
    } catch ( ... ) {
        ok = false;
    }
    EXPECT_TRUE( ok );
}

TEST_F( IOStreamBufferTest, accessCacheSizeTest ) {
    IOStreamBuffer<char> myBuffer1;
    EXPECT_NE( 0U, myBuffer1.cacheSize() );

    IOStreamBuffer<char> myBuffer2( 100 );
    EXPECT_EQ( 100U, myBuffer2.cacheSize() );
}

const char data[]{"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Qui\
sque luctus sem diam, ut eleifend arcu auctor eu. Vestibulum id est vel nulla l\
obortis malesuada ut sed turpis. Nulla a volutpat tortor. Nunc vestibulum portt\
itor sapien ornare sagittis volutpat."};


TEST_F( IOStreamBufferTest, open_close_Test ) {
    IOStreamBuffer<char> myBuffer;

    EXPECT_FALSE( myBuffer.open( nullptr ) );
    EXPECT_FALSE( myBuffer.close() );
    
    const auto dataSize = sizeof(data);
    const auto dataCount = dataSize / sizeof(*data);

    char fname[]={ "octest.XXXXXX" };
    auto* fs = MakeTmpFile(fname);
    ASSERT_NE(nullptr, fs);
    
    auto written = std::fwrite( data, sizeof(*data), dataCount, fs );
    EXPECT_NE( 0U, written );
    auto flushResult = std::fflush( fs );
	ASSERT_EQ(0, flushResult);
	std::fclose( fs );
	fs = std::fopen(fname, "r");
	ASSERT_NE(nullptr, fs);
    {
        TestDefaultIOStream myStream( fs, fname );

        EXPECT_TRUE( myBuffer.open( &myStream ) );
        EXPECT_FALSE( myBuffer.open( &myStream ) );
        EXPECT_TRUE( myBuffer.close() );
    }
    remove(fname);
}

TEST_F( IOStreamBufferTest, blockCountTest ) {

    const auto dataSize = sizeof(data);
    const auto dataCount = dataSize / sizeof(*data);

    char fname[]={ "blockcounttest.XXXXXX" };
    auto* fs = MakeTmpFile(fname);
    ASSERT_NE(nullptr, fs);

    auto written = std::fwrite( data, sizeof(*data), dataCount, fs );
    ASSERT_EQ( dataCount, written );

    auto flushResult = std::fflush(fs);
    ASSERT_EQ(0, flushResult);
    std::fclose(fs);
    fs = std::fopen(fname, "r");
    ASSERT_NE(nullptr, fs);

    const auto tCacheSize = 26u;

    IOStreamBuffer<char> myBuffer( tCacheSize );
    EXPECT_EQ(tCacheSize, myBuffer.cacheSize() );

    TestDefaultIOStream myStream( fs, fname );
    auto size = myStream.FileSize();
    auto numBlocks = size / myBuffer.cacheSize();
    if ( size % myBuffer.cacheSize() > 0 ) {
        numBlocks++;
    }
    EXPECT_TRUE( myBuffer.open( &myStream ) );
    EXPECT_EQ( 0, myBuffer.getFilePos() );
    EXPECT_EQ( numBlocks, myBuffer.getNumBlocks() );
    EXPECT_TRUE( myBuffer.close() );
}

TEST_F( IOStreamBufferTest, cacheReadDataLineTest ) {

    AI_ASSERT_STRINGY_DATA(data);

    // Exclude null terminator from data for this test.
    constexpr auto dataSize = sizeof(data) - 1;
    constexpr auto dataCount = dataSize;

    // Choose one that's not in the data.
    const char continuationToken = '$';

    // There are no continuation tokens, CRs, LFs, or NULs in the data to complicate this test.
    ASSERT_EQ(nullptr, memchr(data, (unsigned char)continuationToken, dataSize));
    ASSERT_EQ(nullptr, memchr(data, (unsigned char)'\n', dataSize));
    ASSERT_EQ(nullptr, memchr(data, (unsigned char)'\r', dataSize));
    ASSERT_EQ(nullptr, memchr(data, (unsigned char)'\0', dataSize));

    char fname[]={ "cacheReadDataLineTest.XXXXXX" };
    auto* fs = MakeTmpFile(fname);
    ASSERT_NE(nullptr, fs);

    auto written = std::fwrite( data, 1, dataSize, fs );
    ASSERT_EQ( dataSize, written );

    auto flushResult = std::fflush(fs);
    ASSERT_EQ(0, flushResult);
    std::fclose(fs);

    // Test with various combinations of cache sizes and read limits, relative
    // to the file size. Sizes chosen to be less than, equal to, or greater
    // than file size, plus some traditionally problematic extras (-2,-1,+1),
    // and 0 and 1 to test extremes. UINT_MAX is list terminator.
    ASSERT_GE(dataCount, (size_t)4) << "Fix me: Give me more data!";
    const unsigned cacheSizes[] = { 0, 1, dataCount / 2, dataCount - 2, dataCount - 1, dataCount, dataCount + 1, dataCount * 2, UINT_MAX };
    const unsigned readLimits[] = { 0, 1, dataCount / 2, dataCount - 2, dataCount - 1, dataCount, dataCount + 1, dataCount * 2, UINT_MAX };

    for (int cIndex = 0; cacheSizes[cIndex] != UINT_MAX; ++ cIndex) {
        for (int rIndex = 0; readLimits[rIndex] != UINT_MAX; ++ rIndex) {

            fs = std::fopen(fname, "r");
            ASSERT_NE(nullptr, fs);

            const auto tCacheSize = cacheSizes[cIndex];
            const auto tReadLimit = readLimits[rIndex];

            // open should fail for invalid cache sizes and succeed otherwise.
            const bool tExpectedOpen = (tCacheSize > 0);

            // read should fail if not open or if line too long.
            // note: >, not >=, as limit includes appended newline.
            const bool tExpectedRead = (tExpectedOpen && (tReadLimit > dataSize));

            IOStreamBuffer<char> myBuffer( tCacheSize );
            ASSERT_EQ(tCacheSize, myBuffer.cacheSize() );

            TestDefaultIOStream myStream( fs, fname );
            ASSERT_EQ(myStream.FileSize(), dataSize);

            std::vector<char> readBuffer;
            ASSERT_EQ( tExpectedOpen, myBuffer.open( &myStream ) );
            ASSERT_EQ( tExpectedRead, myBuffer.getNextDataLine( readBuffer, continuationToken, tReadLimit ) );
            ASSERT_EQ( tExpectedOpen, myBuffer.close() ); // close should fail if open failed

            ASSERT_LE( readBuffer.size(), tReadLimit ); // <- should remain true even on read failure
            if ( tExpectedRead ) {
                ASSERT_EQ( readBuffer.size(), dataCount + 1 ); // <- will be all data + a newline.
                ASSERT_EQ( 0, memcmp(&(readBuffer[0]), data, dataSize) );
                ASSERT_EQ( '\n', readBuffer[dataCount] );
            }

        }
    }

}
