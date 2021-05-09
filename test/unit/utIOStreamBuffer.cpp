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
#include <set>
#include <vector>
#include <cctype>


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

static const char gTestData[]{
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Qui"
    "sque luctus sem diam, ut eleifend arcu auctor eu. Vestibulum id est vel nulla l"
    "obortis malesuada ut sed turpis. Nulla a volutpat tortor. Nunc vestibulum portt"
    "itor sapien ornare sagittis volutpat."
};


static void iosbt_MakeTestFile (const void *content, size_t length, char fname[], bool binaryMode = false) {

    auto* fs = MakeTmpFile(fname, binaryMode);
    ASSERT_NE(nullptr, fs);

    if (length > 0) {
        ASSERT_NE( nullptr, content );
        auto written = std::fwrite( content, length, 1, fs );
        ASSERT_EQ( 1, written );
    }

    auto flushResult = std::fflush(fs);
    ASSERT_EQ(0, flushResult);
    std::fclose(fs);

}


TEST_F( IOStreamBufferTest, open_close_Test ) {
    IOStreamBuffer<char> myBuffer;

    EXPECT_FALSE( myBuffer.open( nullptr ) );
    EXPECT_FALSE( myBuffer.close() );
    
    const auto dataSize = sizeof(gTestData);
    //const auto dataCount = dataSize / sizeof(*gTestData);

    char fname[]={ "octest.XXXXXX" };
    iosbt_MakeTestFile(gTestData, dataSize, fname);

    auto* fs = std::fopen(fname, "r");
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

    const auto dataSize = sizeof(gTestData);
    //const auto dataCount = dataSize / sizeof(*gTestData);

    char fname[]={ "blockcounttest.XXXXXX" };
    iosbt_MakeTestFile(gTestData, dataSize, fname);

    auto* fs = std::fopen(fname, "r");
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


namespace Assimp {
struct ReadParameterSet {
    unsigned readLimit;         // Max elements param for getNextDataLine call.
    unsigned linesExpected;     // Total number of lines we expect to read.
    unsigned elementsExpected;  // Total number of elements we expect to read.
};
}

static constexpr unsigned MAX_LINES_BEFORE_ABORT = 1000u; // Max line count before concluding we're stuck.

template <typename element_t>
static void iosbt_RunReadDataLineTest (const void *content, size_t contentBytes,
                                       element_t continuationToken,
                                       const std::vector<unsigned> &cacheSizes,
                                       const std::vector<ReadParameterSet> &readParameterSets)
{

    ASSERT_NE(nullptr, content);
    ASSERT_GE(contentBytes, (size_t)0);
    ASSERT_FALSE(cacheSizes.empty());
    ASSERT_FALSE(readParameterSets.empty());

    char fname[] = { "runreaddatalinetest.XXXXXX" };
    iosbt_MakeTestFile(content, contentBytes, fname, true);

    for (auto tCacheSize : cacheSizes) {
        for (auto tReadParams : readParameterSets) {

            auto* fs = std::fopen(fname, "rb");
            ASSERT_NE(nullptr, fs);

            // open should fail for invalid cache sizes and succeed otherwise. read
            // should always fail if open fails.
            const bool tExpectedOpen = (tCacheSize > 0);
            const auto tExpectedLines = tExpectedOpen ? tReadParams.linesExpected : 0;
            const auto tExpectedElements = tExpectedOpen ? tReadParams.elementsExpected : 0;
            const auto tReadLimit = tReadParams.readLimit;

            IOStreamBuffer<element_t> myBuffer( tCacheSize );
            ASSERT_EQ( tCacheSize, myBuffer.cacheSize() );
            TestDefaultIOStream myStream( fs, fname );
            ASSERT_EQ( myStream.FileSize(), contentBytes );

            std::vector<element_t> readBuffer, allDataRead;
            unsigned linesRead = 0;
            bool gotLine;

            ASSERT_EQ( tExpectedOpen, myBuffer.open( &myStream ) );
            do {
                gotLine = myBuffer.getNextDataLine( readBuffer, continuationToken, tReadLimit );
                ASSERT_LE( readBuffer.size(), tReadLimit ); // <- should remain true even on read failure
                if ( gotLine ) {
                    ASSERT_FALSE( readBuffer.empty() );
                    ASSERT_EQ( (element_t)'\n', readBuffer.back() );
                    allDataRead.insert(allDataRead.end(), readBuffer.begin(), readBuffer.end());
                    ++ linesRead;
                    ASSERT_LE( linesRead, MAX_LINES_BEFORE_ABORT ) << "getNextDataLine seems to be stuck returning true";
                }
            } while ( gotLine );
            ASSERT_EQ( tExpectedOpen, myBuffer.close() ); // close should fail if open failed

            ASSERT_EQ( tExpectedLines, linesRead );
            ASSERT_EQ( tExpectedElements, allDataRead.size() );
            if ( tExpectedElements ) {
                size_t totalBytesReceived = allDataRead.size() * sizeof(element_t);
                if (totalBytesReceived > contentBytes) {
                    // There is exactly one reason for us to have seen more bytes than
                    // the input file had, which is that the content didn't end in a
                    // newline and IOStreamBuffer added one for us. Confirm that here.
                    ASSERT_EQ( totalBytesReceived, contentBytes + sizeof(element_t) );
                    ASSERT_EQ( '\n', allDataRead.back() );
                }
                size_t bytesToCompare = std::min( totalBytesReceived, contentBytes );
                ASSERT_EQ( 0, memcmp(content, &(allDataRead[0]), bytesToCompare) );
            }

        }
    }

}


TEST_F( IOStreamBufferTest, readDataLineTest_BufferBehavior ) {

    const char myData[] = {
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Qui"
        "sque luctus sem diam, ut eleifend arcu auctor eu. Vestibulum id est vel nulla l"
        "obortis malesuada ut sed turpis. Nulla a volutpat tortor. Nunc vestibulum portt"
        "itor sapien ornare sagittis volutpat."
    };
    const unsigned dataLen = (unsigned)strlen(myData);

    // Test with various combinations of cache sizes and read limits, relative
    // to the data length. Sizes chosen to be less than, equal to, or greater
    // than data length, plus some traditionally problematic extras (-2,-1,+1),
    // and 0 and 1 to test extremes. These were chosen to specifically test some
    // issues that were present in getNextDataLine.
    iosbt_RunReadDataLineTest<char>(myData, (size_t)dataLen, 0,
                                    { 0, 1, dataLen / 2, dataLen - 2, dataLen - 1, dataLen, dataLen + 1, dataLen * 2 },
                                    { {           0, 0,           0 },
                                      {           1, 0,           0 },
                                      { dataLen / 2, 0,           0 },
                                      { dataLen - 2, 0,           0 },
                                      { dataLen - 1, 0,           0 },
                                      {     dataLen, 0,           0 },
                                      { dataLen + 1, 1, dataLen + 1 },
                                      { dataLen * 2, 1, dataLen + 1 } });

}

TEST_F( IOStreamBufferTest, readDataLineTest_Vanilla ) {

    const char myData[] = {
        "first_6789ABCDEF\n"
        "second_789ABCDEF\n"
        "third_6789ABCDEF\n"
        "fourth_789ABCDEF\n"
        "fifth_6789ABCDEF\n"
    };
    const unsigned lineLength = 16;
    const unsigned lineCount = 5;
    const unsigned myDataLength = (unsigned)strlen(myData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, 0,
                                    { 1, lineLength - 1, lineLength, lineLength + 1, lineLength + 2, myDataLength * 2 },
                                    { {   lineLength - 1,         0,            0 },
                                      {       lineLength,         0,            0 },
                                      {   lineLength + 1, lineCount, myDataLength },
                                      { myDataLength * 2, lineCount, myDataLength } });

}

TEST_F( IOStreamBufferTest, readDataLineTest_NoNewlineAtEnd ) {

    const char myData[] = {
        "first_6789ABCDEF\n"
        "second_789ABCDEF\n"
        "third_6789ABCDEF\n"
        "fourth_789ABCDEF\n"
        "fifth_6789ABCDEF"
    };
    const unsigned lineLength = 16;
    const unsigned lineCount = 5;
    const unsigned myDataLength = (unsigned)strlen(myData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, 0,
                                    { 1, lineLength - 1, lineLength, lineLength + 1, lineLength + 2, myDataLength * 2 },
                                    { {   lineLength - 1,         0,                0 },
                                      {       lineLength,         0,                0 },
                                      {   lineLength + 1, lineCount, myDataLength + 1 },
                                      { myDataLength * 2, lineCount, myDataLength + 1 } });

}

TEST_F( IOStreamBufferTest, readDataLineTest_TwoNewlinesAtEnd ) {

    const char myData[] = {
        "first_6789ABCDEF\n"
        "second_789ABCDEF\n"
        "third_6789ABCDEF\n"
        "fourth_789ABCDEF\n"
        "fifth_6789ABCDEF\n"
        "\n"
    };
    const unsigned lineLength = 16;
    const unsigned lineCount = 6;
    const unsigned myDataLength = (unsigned)strlen(myData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, 0,
                                    { 1, lineLength - 1, lineLength, lineLength + 1, lineLength + 2, myDataLength * 2 },
                                    { {   lineLength - 1,         0,            0 },
                                      {       lineLength,         0,            0 },
                                      {   lineLength + 1, lineCount, myDataLength },
                                      { myDataLength * 2, lineCount, myDataLength } });

}

TEST_F( IOStreamBufferTest, readDataLineTest_TwoNewlinesAtStart ) {

    const char myData[] = {
        "\n"
        "first_6789ABCDEF\n"
        "second_789ABCDEF\n"
        "third_6789ABCDEF\n"
        "fourth_789ABCDEF\n"
        "fifth_6789ABCDEF\n"
    };
    const unsigned lineLength = 16;
    const unsigned lineCount = 6;
    const unsigned myDataLength = (unsigned)strlen(myData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, 0,
                                    { 1, lineLength - 1, lineLength, lineLength + 1, lineLength + 2, myDataLength * 2 },
                                    { {   lineLength - 1,         1,            1 },
                                      {       lineLength,         1,            1 },
                                      {   lineLength + 1, lineCount, myDataLength },
                                      { myDataLength * 2, lineCount, myDataLength } });

}

TEST_F( IOStreamBufferTest, readDataLineTest_AllEmptyLines ) {

    const char myData[] = { "\n\n\n\n\n" };
    const unsigned lineCount = 5;
    const unsigned myDataLength = (unsigned)strlen(myData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, 0,
                                    { 1, 2, myDataLength * 2 },
                                    { {                1, lineCount, myDataLength },
                                      {                2, lineCount, myDataLength },
                                      { myDataLength * 2, lineCount, myDataLength } });

}
