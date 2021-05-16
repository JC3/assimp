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

static void iosbt_RunBlockCountTest (const char *fname, size_t tCacheSize) {

    auto* fs = std::fopen(fname, "r");
    ASSERT_NE(nullptr, fs);

    IOStreamBuffer<char> myBuffer( tCacheSize );
    EXPECT_EQ(tCacheSize, myBuffer.cacheSize() );

    TestDefaultIOStream myStream( fs, fname );
    auto size = myStream.FileSize();
    auto numBlocks = size / myBuffer.cacheSize();
    if ( size % myBuffer.cacheSize() > 0 ) {
        numBlocks++;
    }
    ASSERT_TRUE( myBuffer.open( &myStream ) );
    EXPECT_EQ( 0, myBuffer.getFilePos() );
    EXPECT_EQ( numBlocks, myBuffer.getNumBlocks() );
    unsigned numActualBlocks = 0;
    while (myBuffer.readNextBlock()) {
        ++ numActualBlocks;
        ASSERT_LE( numActualBlocks, 1000u ) << "readNextBlock appears stuck in a loop.";
    }
#if 0 // For CRLF input files, there's no easy way to know, so we'll have to just live with it.
    EXPECT_EQ( numBlocks, numActualBlocks );
#else
    if ( numBlocks != numActualBlocks ) {
        // If you're here investigating: Don't worry, this is just informative. It's not
        // a cause for alarm.
        ASSIMP_LOG_DEBUG_F( "Note: Block count off (cache=", tCacheSize, ", filesize=",
                            size, "): calculated=", numBlocks, ", actual=", numActualBlocks );
    }
#endif
    EXPECT_TRUE( myBuffer.close() );

}

TEST_F( IOStreamBufferTest, blockCountTest ) {

    // This is equivalent to the original test; it's just been moved to a util function.
    char fname[]={ "blockcounttest.XXXXXX" };   
    iosbt_MakeTestFile(gTestData, sizeof(gTestData), fname);
    iosbt_RunBlockCountTest(fname, 26);

}

TEST_F( IOStreamBufferTest, blockCountTest_CRLF ) {

    const char myData[] = {
        "first_6789ABCDEF\r\n"
        "second_789ABCDEF\r\n"
        "third_6789ABCDEF\r\n"
        "fourth_789ABCDEF\r\n"
        "fifth_6789ABCDEF\r\n"
    };
    const size_t myDataLength = (size_t)strlen(myData);

    char fname[]={ "blockcounttest_crlf.XXXXXX" };
    iosbt_MakeTestFile(myData, myDataLength, fname, true);
    iosbt_RunBlockCountTest(fname, 1);
    iosbt_RunBlockCountTest(fname, 16);
    iosbt_RunBlockCountTest(fname, 17);
    iosbt_RunBlockCountTest(fname, 18);
    iosbt_RunBlockCountTest(fname, 26);
    iosbt_RunBlockCountTest(fname, myDataLength / 2);
    iosbt_RunBlockCountTest(fname, myDataLength - 1);
    iosbt_RunBlockCountTest(fname, myDataLength);
    iosbt_RunBlockCountTest(fname, myDataLength + 1);

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
                                       const std::vector<ReadParameterSet> &readParameterSets,
                                       const void *expectedContent = nullptr, size_t expectedContentBytes = 0)
{

    if (!expectedContent) {
        expectedContent = content;
        expectedContentBytes = contentBytes;
    }

    ASSERT_NE(nullptr, content);
    ASSERT_GE(contentBytes, (size_t)0);
    ASSERT_NE(nullptr, expectedContent);
    ASSERT_GE(expectedContentBytes, (size_t)0);
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

            //ASSIMP_LOG_VERBOSE_DEBUG_F("cache=", tCacheSize, " limit=", tReadLimit, " xlines=", tReadParams.linesExpected, " xelems=", tReadParams.elementsExpected);

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

            EXPECT_EQ( tExpectedLines, linesRead );
            EXPECT_EQ( tExpectedElements, allDataRead.size() );
            if ( tExpectedElements ) {
                size_t totalBytesReceived = allDataRead.size() * sizeof(element_t);
                if (totalBytesReceived > expectedContentBytes) {
                    // There is exactly one reason for us to have seen more bytes than
                    // the input file had, which is that the content didn't end in a
                    // newline and IOStreamBuffer added one for us. Confirm that here.
                    ASSERT_EQ( totalBytesReceived, expectedContentBytes + sizeof(element_t) );
                    ASSERT_EQ( '\n', allDataRead.back() );
                }
                size_t bytesToCompare = std::min( totalBytesReceived, expectedContentBytes );
                EXPECT_EQ( 0, memcmp(expectedContent, &(allDataRead[0]), bytesToCompare) );
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

TEST_F( IOStreamBufferTest, readDataLineTest_Ideal ) {

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

static void iosbt_RunCRLFTest (const char *fopenmode) {

    const char myData[] = {
        "first_6789ABCDEF\r\n"
        "second_789ABCDEF\r\n"
        "third_6789ABCDEF\r\n"
        "fourth_789ABCDEF\r\n"
        "fifth_6789ABCDEF\r\n"
    };
    const unsigned lineLength = 16; // not including line endings
    const unsigned lineCount = 5;
    const unsigned myDataLen = (unsigned)strlen(myData);

    char fname[] = { "readdatalinetest_crlf.XXXXXX" };
    iosbt_MakeTestFile(myData, myDataLen, fname, true);

    // Let's do this test in challenge mode with a variety of cache sizes:
    std::vector<size_t> cacheSizes = {
        1,                      // always a good test
        15, 16, 17, 18, 19,     // (lineLength - 1) thru (bytes per line + 1)
        myDataLen - lineCount,  // data length if CRLFs changed to LFs
        myDataLen,              // data length
        myDataLen - 1,          // because why not
        myDataLen + 1,          // same
        IOStreamBuffer<char>::DefaultBufferSize  // default value
    };

    for (size_t cacheSize : cacheSizes) {

        auto* fs = std::fopen(fname, fopenmode);
        ASSERT_NE(nullptr, fs);
        TestDefaultIOStream myStream( fs, fname );
        ASSERT_EQ( myStream.FileSize(), myDataLen );

        IOStreamBuffer<char> myBuffer( cacheSize );
        ASSERT_EQ( myBuffer.cacheSize(), cacheSize );

        std::vector<char> readBuffer;
        ASSERT_TRUE( myBuffer.open( &myStream ) );

        bool gotLine;
        unsigned linesRead = 0;

        do {
            gotLine = myBuffer.getNextDataLine(readBuffer, 0);
            if ( linesRead < lineCount ) {
                ASSERT_TRUE( gotLine ) << "failed to read a line when there were still lines left.";
                EXPECT_EQ( lineLength + 1, readBuffer.size() ) << "# elements read doesn't match expected line length.";
                EXPECT_EQ( '\n', readBuffer.back() ) << "line isn't terminated with an LF";
                EXPECT_EQ( 0, strchr(&(readBuffer[0]), '\r') ) << "a CR was read as part of the data";
                EXPECT_EQ( 1, std::count(readBuffer.begin(), readBuffer.end(), '\n') ) << "a LF was read as part of the data";
            } else {
                EXPECT_FALSE( gotLine ) << "read a line when there shouldn't have been more lines.";
                if ( gotLine ) { // if we *did* get an unexpected line; check it anyways:
                    EXPECT_FALSE( readBuffer.empty() );
                    if ( !readBuffer.empty() ) {
                        EXPECT_EQ( lineLength + 1, readBuffer.size() );
                        EXPECT_EQ( '\n', readBuffer.back() );
                        EXPECT_EQ( 0, strchr(&(readBuffer[0]), '\r') );
                        EXPECT_EQ( 1, std::count(readBuffer.begin(), readBuffer.end(), '\n') );
                    }
                    FAIL();
                }
            }
            ++ linesRead;
            ASSERT_LE( linesRead, MAX_LINES_BEFORE_ABORT ) << "getNextDataLine seems to be stuck returning true";
        } while ( gotLine );

        ASSERT_TRUE( myBuffer.close() );

    }

}

// On POSIX systems this will be identical to the _BinaryMode test below.
// On Windows this will change CRLFs to LFs before IOStreamBuffer sees the data.
TEST_F( IOStreamBufferTest, readDataLineTest_CRLF_FilePosition ) {

    iosbt_RunCRLFTest("r");

}

// On both POSIX and Windows systems, this will preserve CRLFs on read.
TEST_F( IOStreamBufferTest, readDataLineTest_CRLF_FilePosition_BinaryMode ) {

    iosbt_RunCRLFTest("rb");

}

TEST_F( IOStreamBufferTest, readDataLineTest_MixedLineEndings ) {

    const char myData[] = {
        "first_6789ABCDEF\n"
        "second_789ABCDEF\r\n"
        "third_6789ABCDEF\r"
        "fourth_789ABCDEF\r"
        "\r"
    };
    const char expectedData[] = {
        "first_6789ABCDEF\n"
        "second_789ABCDEF\n"
        "third_6789ABCDEF\n"
        "fourth_789ABCDEF\n"
        "\n"
    };
    const unsigned lineCount = 5;
    const unsigned lineLength = 16;
    const unsigned myDataLength = (unsigned)strlen(myData);
    const unsigned expectedDataLength = (unsigned)strlen(expectedData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, 0,
                                    { 1, lineLength - 1, lineLength, lineLength + 1, lineLength + 2, myDataLength * 2 },
                                    { {   lineLength - 1,         0,                  0 },
                                      {       lineLength,         0,                  0 },
                                      {   lineLength + 1,         1,     lineLength + 1 },
                                      { myDataLength * 2, lineCount, expectedDataLength } },
                                    expectedData, expectedDataLength);

}


TEST_F( IOStreamBufferTest, readDataLineTest_Continuations_Ideal ) {

    const char myData[] = {
        "first_6789ABCDEF$\n"
        "second_789ABCDEF\n"
        "third_6789ABCDEF$\n"
        "fourth_789ABCDEF\n"
    };
    const char expectedData[] = {
        "first_6789ABCDEF"
        "second_789ABCDEF\n"
        "third_6789ABCDEF"
        "fourth_789ABCDEF\n"
    };
    const unsigned lineLength = 32;
    const unsigned lineCount = 2;
    const unsigned myDataLength = (unsigned)strlen(myData);
    const unsigned expectedDataLength = (unsigned)strlen(expectedData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, '$',
                                    { 1, lineLength - 1, lineLength, lineLength + 1, lineLength + 2, myDataLength * 2 },
                                    { {   lineLength - 1,         0,                0 },
                                      {       lineLength,         0,                0 },
                                      {   lineLength + 1, lineCount, (lineLength + 1) * lineCount },
                                      { myDataLength * 2, lineCount, (lineLength + 1) * lineCount } },
                                    expectedData, (size_t)expectedDataLength);

}

TEST_F( IOStreamBufferTest, readDataLineTest_Continuations_TokensInData ) {

    const char myData[] = {
        "first$6789ABCDEF\n"
        "second$$89ABCDEF\n"
        "third$6$89ABCDEF\n"
        "fourth$$8$ABCDEF\n"
    };
    const unsigned lineLength = 16;
    const unsigned lineCount = 4;
    const unsigned myDataLength = (unsigned)strlen(myData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, '$',
                                    { 1, lineLength - 1, lineLength, lineLength + 1, lineLength + 2, myDataLength * 2 },
                                    { {   lineLength - 1,         0,                0 },
                                      {       lineLength,         0,                0 },
                                      {   lineLength + 1, lineCount, (lineLength + 1) * lineCount },
                                      { myDataLength * 2, lineCount, (lineLength + 1) * lineCount } });

}

TEST_F( IOStreamBufferTest, readDataLineTest_Continuations_Consecutive ) {

    const char myData[] = {
        "first_6789ABCDEF$\n"
        "second_789ABCDEF$\n"
        "third_6789ABCDEF$\n"
        "fourth_789ABCDEF\n"
    };
    const char expectedData[] = {
        "first_6789ABCDEF"
        "second_789ABCDEF"
        "third_6789ABCDEF"
        "fourth_789ABCDEF\n"
    };
    const unsigned lineLength = 64;
    const unsigned lineCount = 1;
    const unsigned myDataLength = (unsigned)strlen(myData);
    const unsigned expectedDataLength = (unsigned)strlen(expectedData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, '$',
                                    { 1, lineLength - 1, lineLength, lineLength + 1, lineLength + 2, myDataLength * 2 },
                                    { {   lineLength - 1,         0,                0 },
                                      {       lineLength,         0,                0 },
                                      {   lineLength + 1, lineCount, (lineLength + 1) * lineCount },
                                      { myDataLength * 2, lineCount, (lineLength + 1) * lineCount } },
                                    expectedData, (size_t)expectedDataLength);

}

TEST_F( IOStreamBufferTest, readDataLineTest_Continuations_EOF ) {

    const char myData[] = {
        "first_6789ABCDEF\n"
        "second_789ABCDEF\n"
        "third_6789ABCDEF\n"
        "fourth_789ABCDEF$"
    };
    const char expectedContent[] = {
        "first_6789ABCDEF\n"
        "second_789ABCDEF\n"
        "third_6789ABCDEF\n"
        "fourth_789ABCDEF\n"
    };
    const unsigned lineLength = 16;
    const unsigned lineCount = 4;
    const unsigned myDataLength = (unsigned)strlen(myData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, '$',
                                    { 1, lineLength - 1, lineLength, lineLength + 1, lineLength + 2, myDataLength * 2 },
                                    { {   lineLength - 1,         0,                0 },
                                      {       lineLength,         0,                0 },
                                      {   lineLength + 1, lineCount - 1, (lineLength + 1) * (lineCount - 1) },
                                      {   lineLength + 2, lineCount, (lineLength + 1) * lineCount },
                                      { myDataLength * 2, lineCount, (lineLength + 1) * lineCount } },
                                    expectedContent, (size_t)strlen(expectedContent));

}

TEST_F( IOStreamBufferTest, readDataLineTest_Continuations_ConsecutiveEOF ) {

    const char myData[] = {
        "first_6789ABCDEF$\n"
        "second_789ABCDEF$\n"
        "third_6789ABCDEF$\n"
        "fourth_789ABCDEF$"
    };
    const char expectedData[] = {
        "first_6789ABCDEF"
        "second_789ABCDEF"
        "third_6789ABCDEF"
        "fourth_789ABCDEF\n"
    };
    const unsigned lineLength = 64;
    const unsigned lineCount = 1;
    const unsigned myDataLength = (unsigned)strlen(myData);
    const unsigned expectedDataLength = (unsigned)strlen(expectedData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, '$',
                                    { 1, lineLength - 1, lineLength, lineLength + 1, lineLength + 2, myDataLength * 2 },
                                    { {   lineLength - 1,         0,                  0 },
                                      {       lineLength,         0,                  0 },
                                      {   lineLength + 1,         0,                  0 },
                                      {   lineLength + 2, lineCount, expectedDataLength },
                                      { myDataLength * 2, lineCount, expectedDataLength } },
                                    expectedData, (size_t)expectedDataLength);

}

TEST_F( IOStreamBufferTest, readDataLineTest_Continuations_EmptyLines ) {

    const char myData[] = {
        "$\n"
        "\n"
        "$\n"
        "$\n"
        "\n"
    };
    const char expectedData[] = {
        "\n"
        "\n"
    };
    const unsigned lineLength = 0;
    const unsigned lineCount = 2;
    const unsigned myDataLength = (unsigned)strlen(myData);
    const unsigned expectedDataLength = (unsigned)strlen(expectedData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, '$',
                                    { 1, 2, 3, myDataLength * 2 },
                                    { {                1,         0,                            0 },
                                      {                2, lineCount, (lineLength + 1) * lineCount },
                                      { myDataLength * 2, lineCount, (lineLength + 1) * lineCount } },
                                    expectedData, (size_t)expectedDataLength);

}

TEST_F( IOStreamBufferTest, readDataLineTest_Continuations_CRLF ) {

    const char myData[] = {
        "$\r\n"
        "\r\n"
        "$\r\n"
        "$\r\n"
        "\r\n"
        "$\r\n"
        "LINE\r\n"
        "LINE$\r\n"
        "\r\n"
        "LINE$\r\n"
        "LINE$\r\n"
        "LINE\r\n"
    };
    const char expectedData[] = {
        "\n"
        "\n"
        "LINE\n"
        "LINE\n"
        "LINELINELINE\n"
    };
    const unsigned lineCount = 5;
    const unsigned myDataLength = (unsigned)strlen(myData);
    const unsigned expectedDataLength = (unsigned)strlen(expectedData);

    iosbt_RunReadDataLineTest<char>(myData, (size_t)myDataLength, '$',
                                    { 1,2,3,4,5,6,7,8,9, myDataLength * 2 },
                                    { {                1,         0,                  0 },
                                      {                2,         0,                  0 },
                                      {                3,         2,              1 + 1 },
                                      {                5,         2,              1 + 1 },
                                      {                6,         3,          1 + 1 + 5 },
                                      {                7,         4,      1 + 1 + 5 + 5 },
                                      {               14, lineCount, expectedDataLength },
                                      { myDataLength * 2, lineCount, expectedDataLength } },
                                    expectedData, (size_t)expectedDataLength);

}
