/**
 *    Copyright (C) 2012 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <boost/scoped_ptr.hpp>
#include <string>
#include <vector>

#include "mongo/db/jsobj.h"
#include "mongo/dbtests/mock/mock_conn_registry.h"
#include "mongo/dbtests/mock/mock_remote_db_server.h"
#include "mongo/s/chunk_version.h"
#include "mongo/s/collection_metadata.h"
#include "mongo/s/metadata_loader.h"
#include "mongo/s/type_chunk.h"
#include "mongo/s/type_collection.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/net/hostandport.h"

namespace {

    using boost::scoped_ptr;
    using mongo::BSONObj;
    using mongo::BSONArray;
    using mongo::ChunkType;
    using mongo::ConnectionString;
    using mongo::CollectionMetadata;
    using mongo::CollectionType;
    using mongo::Date_t;
    using mongo::HostAndPort;
    using mongo::MAXKEY;
    using mongo::MetadataLoader;
    using mongo::MINKEY;
    using mongo::OID;
    using mongo::ChunkVersion;
    using mongo::MockConnRegistry;
    using mongo::MockRemoteDBServer;
    using mongo::Status;
    using std::string;
    using std::vector;

    const std::string CONFIG_HOST_PORT = "$dummy_config:27017";

    class NoChunkFixture : public mongo::unittest::Test {
    protected:
        void setUp() {
            _dummyConfig.reset( new MockRemoteDBServer( CONFIG_HOST_PORT ) );
            mongo::ConnectionString::setConnectionHook( MockConnRegistry::get()->getConnStrHook() );
            MockConnRegistry::get()->addServer( _dummyConfig.get() );

            OID epoch = OID::gen();
            _dummyConfig->insert( CollectionType::ConfigNS, BSON(CollectionType::ns("test.foo") <<
                    CollectionType::keyPattern(BSON("a" << 1)) <<
                    CollectionType::unique(false) <<
                    CollectionType::updatedAt(1ULL) <<
                    CollectionType::epoch(epoch)) );

            ConnectionString configLoc( CONFIG_HOST_PORT );
            MetadataLoader loader( configLoc );

            Status status = loader.makeCollectionMetadata( "test.foo",
                                                           "shard0000",
                                                           NULL,
                                                           &_metadata );
            ASSERT( status.isOK() );
        }

        void tearDown() {
            MockConnRegistry::get()->clear();
        }

        const CollectionMetadata& getCollMetadata() const {
            return _metadata;
        }

    private:
        scoped_ptr<MockRemoteDBServer> _dummyConfig;
        CollectionMetadata _metadata;
    };

    TEST_F(NoChunkFixture, BasicBelongsToMe) {
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << MINKEY)) );
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << 10)) );
    }

    TEST_F(NoChunkFixture, CompoudKeyBelongsToMe) {
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << 1 << "b" << 2)) );
    }

    TEST_F(NoChunkFixture, getNextFromEmpty) {
        ChunkType nextChunk;
        ASSERT( getCollMetadata().getNextChunk( BSONObj(), &nextChunk ) );
    }

    TEST_F(NoChunkFixture, FirstChunkClonePlus) {
        ChunkType chunk;
        chunk.setMin( BSON("a" << 10) );
        chunk.setMax( BSON("a" << 20) );

        string errMsg;
        const ChunkVersion version( 99, 0, OID() );
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().clonePlus( chunk,
                                                                            version,
                                                                            &errMsg ) );

        ASSERT( errMsg.empty() );
        ASSERT_EQUALS( 1u, cloned->getNumChunks() );
        ASSERT_EQUALS( cloned->getShardVersion().toLong(), version.toLong() );
        ASSERT_EQUALS( cloned->getCollVersion().toLong(), version.toLong() );
        ASSERT( cloned->keyBelongsToMe(BSON("a" << 15)) );
    }

    TEST_F(NoChunkFixture, MustHaveVersionForFirstChunk) {
        ChunkType chunk;
        chunk.setMin( BSON("a" << 10) );
        chunk.setMax( BSON("a" << 20) );

        string errMsg;
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().clonePlus( chunk,
                                                                            ChunkVersion( 0,
                                                                                          0,
                                                                                          OID() ),
                                                                            &errMsg ) );

        ASSERT( cloned == NULL );
        ASSERT_FALSE( errMsg.empty() );
    }

    /**
     * Fixture with single chunk containing:
     * [10->20)
     */
    class SingleChunkFixture : public mongo::unittest::Test {
    protected:
        void setUp() {
            _dummyConfig.reset( new MockRemoteDBServer( CONFIG_HOST_PORT ) );
            mongo::ConnectionString::setConnectionHook( MockConnRegistry::get()->getConnStrHook() );
            MockConnRegistry::get()->addServer( _dummyConfig.get() );

            OID epoch = OID::gen();
            ChunkVersion chunkVersion = ChunkVersion( 1, 0, epoch );

            BSONObj collFoo = BSON(CollectionType::ns("test.foo") <<
                    CollectionType::keyPattern(BSON("a" << 1)) <<
                    CollectionType::unique(false) <<
                    CollectionType::updatedAt(1ULL) <<
                    CollectionType::epoch(epoch));
            _dummyConfig->insert( CollectionType::ConfigNS, collFoo );

            BSONObj fooSingle = BSON(ChunkType::name("test.foo-a_10") <<
                    ChunkType::ns("test.foo") <<
                    ChunkType::min(BSON("a" << 10)) <<
                    ChunkType::max(BSON("a" << 20)) <<
                    ChunkType::DEPRECATED_lastmod(chunkVersion.toLong()) <<
                    ChunkType::DEPRECATED_epoch(epoch) <<
                    ChunkType::shard("shard0000"));
            _dummyConfig->insert( ChunkType::ConfigNS, fooSingle );

            ConnectionString configLoc( CONFIG_HOST_PORT );
            MetadataLoader loader( configLoc );

            Status status = loader.makeCollectionMetadata( "test.foo",
                                                           "shard0000",
                                                           NULL,
                                                           &_metadata );
            ASSERT( status.isOK() );
        }

        void tearDown() {
            MockConnRegistry::get()->clear();
        }

        const CollectionMetadata& getCollMetadata() const {
            return _metadata;
        }

    private:
        scoped_ptr<MockRemoteDBServer> _dummyConfig;
        CollectionMetadata _metadata;
    };

    TEST_F(SingleChunkFixture, BasicBelongsToMe) {
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << 10)) );
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << 15)) );
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << 19)) );
    }

    TEST_F(SingleChunkFixture, DoesntBelongsToMe) {
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << 0)) );
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << 9)) );
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << 20)) );
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << 1234)) );
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << MINKEY)) );
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << MAXKEY)) );
    }

    TEST_F(SingleChunkFixture, CompoudKeyBelongsToMe) {
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << 15 << "a" << 14)) );
    }

    TEST_F(SingleChunkFixture, getNextFromEmpty) {
        ChunkType nextChunk;
        ASSERT( getCollMetadata().getNextChunk( BSONObj(), &nextChunk ) );
        ASSERT_EQUALS( 0, nextChunk.getMin().woCompare(BSON("a" << 10)) );
        ASSERT_EQUALS( 0, nextChunk.getMax().woCompare(BSON("a" << 20)) );
    }

    TEST_F(SingleChunkFixture, GetNextFromLast) {
        ChunkType nextChunk;
        ASSERT( getCollMetadata().getNextChunk( BSONObj(), &nextChunk ) );
    }

    TEST_F(SingleChunkFixture, LastChunkCloneMinus) {
        ChunkType chunk;
        chunk.setMin( BSON("a" << 10) );
        chunk.setMax( BSON("a" << 20) );

        string errMsg;
        const ChunkVersion zeroVersion( 0, 0, OID() );
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().cloneMinus( chunk,
                                                                             zeroVersion,
                                                                             &errMsg ) );

        ASSERT( errMsg.empty() );
        ASSERT_EQUALS( 0u, cloned->getNumChunks() );
        ASSERT_EQUALS( cloned->getShardVersion().toLong(), zeroVersion.toLong() );
        ASSERT_EQUALS( cloned->getCollVersion().toLong(),
                       getCollMetadata().getCollVersion().toLong() );
        ASSERT_FALSE( cloned->keyBelongsToMe(BSON("a" << 15)) );
    }

    TEST_F(SingleChunkFixture, LastChunkMinusCantHaveNonZeroVersion) {
        ChunkType chunk;
        chunk.setMin( BSON("a" << 10) );
        chunk.setMax( BSON("a" << 20) );

        string errMsg;
        ChunkVersion version( 99, 0, OID() );
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().cloneMinus( chunk,
                                                                             version,
                                                                             &errMsg ) );

        ASSERT( cloned == NULL );
        ASSERT_FALSE( errMsg.empty() );
    }

    /**
     * Fixture with single chunk containing:
     * [(min, min)->(max, max))
     */
    class SingleChunkMinMaxCompoundKeyFixture : public mongo::unittest::Test {
    protected:
        void setUp() {
            _dummyConfig.reset( new MockRemoteDBServer( CONFIG_HOST_PORT ) );
            mongo::ConnectionString::setConnectionHook( MockConnRegistry::get()->getConnStrHook() );
            MockConnRegistry::get()->addServer( _dummyConfig.get() );

            OID epoch = OID::gen();
            ChunkVersion chunkVersion = ChunkVersion( 1, 0, epoch );

            BSONObj collFoo = BSON(CollectionType::ns("test.foo") <<
                    CollectionType::keyPattern(BSON("a" << 1)) <<
                    CollectionType::unique(false) <<
                    CollectionType::updatedAt(1ULL) <<
                    CollectionType::epoch(epoch));
            _dummyConfig->insert( CollectionType::ConfigNS, collFoo );

            BSONObj fooSingle = BSON(ChunkType::name("test.foo-a_MinKey") <<
                    ChunkType::ns("test.foo") <<
                    ChunkType::min(BSON("a" << MINKEY << "b" << MINKEY)) <<
                    ChunkType::max(BSON("a" << MAXKEY << "b" << MAXKEY)) <<
                    ChunkType::DEPRECATED_lastmod(chunkVersion.toLong()) <<
                    ChunkType::DEPRECATED_epoch(epoch) <<
                    ChunkType::shard("shard0000"));
            _dummyConfig->insert( ChunkType::ConfigNS, fooSingle );

            ConnectionString configLoc( CONFIG_HOST_PORT );
            MetadataLoader loader( configLoc );

            Status status = loader.makeCollectionMetadata( "test.foo",
                                                           "shard0000",
                                                           NULL,
                                                           &_metadata );
            ASSERT( status.isOK() );
        }

        void tearDown() {
            MockConnRegistry::get()->clear();
        }

        const CollectionMetadata& getCollMetadata() const {
            return _metadata;
        }

    private:
        scoped_ptr<MockRemoteDBServer> _dummyConfig;
        CollectionMetadata _metadata;
    };

    // Note: no tests for single key belongsToMe because they are not allowed
    // if shard key is compound.

    TEST_F(SingleChunkMinMaxCompoundKeyFixture, CompoudKeyBelongsToMe) {
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << MINKEY << "b" << MINKEY)) );
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << MAXKEY << "b" << MAXKEY)) );
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << MINKEY << "b" << 10)) );
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << 10 << "b" << 20)) );
    }

    /**
     * Fixture with chunks:
     * [(10, 0)->(20, 0)), [(30, 0)->(40, 0))
     */
    class TwoChunksWithGapCompoundKeyFixture : public mongo::unittest::Test {
    protected:
        void setUp() {
            _dummyConfig.reset( new MockRemoteDBServer( CONFIG_HOST_PORT ) );
            mongo::ConnectionString::setConnectionHook( MockConnRegistry::get()->getConnStrHook() );
            MockConnRegistry::get()->addServer( _dummyConfig.get() );

            OID epoch = OID::gen();
            ChunkVersion chunkVersion = ChunkVersion( 1, 0, epoch );

            BSONObj collFoo = BSON(CollectionType::ns("test.foo") <<
                    CollectionType::keyPattern(BSON("a" << 1)) <<
                    CollectionType::unique(false) <<
                    CollectionType::updatedAt(1ULL) <<
                    CollectionType::epoch(epoch));
            _dummyConfig->insert( CollectionType::ConfigNS, collFoo );

            _dummyConfig->insert( ChunkType::ConfigNS, BSON(ChunkType::name("test.foo-a_10") <<
                    ChunkType::ns("test.foo") <<
                    ChunkType::min(BSON("a" << 10 << "b" << 0)) <<
                    ChunkType::max(BSON("a" << 20 << "b" << 0)) <<
                    ChunkType::DEPRECATED_lastmod(chunkVersion.toLong()) <<
                    ChunkType::DEPRECATED_epoch(epoch) <<
                    ChunkType::shard("shard0000")) );

            _dummyConfig->insert( ChunkType::ConfigNS, BSON(ChunkType::name("test.foo-a_10") <<
                    ChunkType::ns("test.foo") <<
                    ChunkType::min(BSON("a" << 30 << "b" << 0)) <<
                    ChunkType::max(BSON("a" << 40 << "b" << 0)) <<
                    ChunkType::DEPRECATED_lastmod(chunkVersion.toLong()) <<
                    ChunkType::DEPRECATED_epoch(epoch) <<
                    ChunkType::shard("shard0000")) );

            ConnectionString configLoc( CONFIG_HOST_PORT );
            MetadataLoader loader( configLoc );

            Status status = loader.makeCollectionMetadata( "test.foo",
                                                           "shard0000",
                                                           NULL,
                                                           &_metadata );
            ASSERT( status.isOK() );
        }

        void tearDown() {
            MockConnRegistry::get()->clear();
        }

        const CollectionMetadata& getCollMetadata() const {
            return _metadata;
        }

    private:
        scoped_ptr<MockRemoteDBServer> _dummyConfig;
        CollectionMetadata _metadata;
    };

    TEST_F(TwoChunksWithGapCompoundKeyFixture, ClonePlusBasic) {
        ChunkType chunk;
        chunk.setMin( BSON("a" << 40 << "b" << 0) );
        chunk.setMax( BSON("a" << 50 << "b" << 0) );

        string errMsg;
        ChunkVersion version( 1, 0, OID() );
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().clonePlus( chunk,
                                                                             version,
                                                                             &errMsg ) );

        ASSERT( errMsg.empty() );
        ASSERT_EQUALS( 2u, getCollMetadata().getNumChunks() );
        ASSERT_EQUALS( 3u, cloned->getNumChunks() );

        // TODO: test maxShardVersion, maxCollVersion

        ASSERT_FALSE( cloned->keyBelongsToMe(BSON("a" << 25 << "b" << 0)) );
        ASSERT_FALSE( cloned->keyBelongsToMe(BSON("a" << 29 << "b" << 0)) );
        ASSERT( cloned->keyBelongsToMe(BSON("a" << 30 << "b" << 0)) );
        ASSERT( cloned->keyBelongsToMe(BSON("a" << 45 << "b" << 0)) );
        ASSERT( cloned->keyBelongsToMe(BSON("a" << 49 << "b" << 0)) );
        ASSERT_FALSE( cloned->keyBelongsToMe(BSON("a" << 50 << "b" << 0)) );
    }

    TEST_F(TwoChunksWithGapCompoundKeyFixture, ClonePlusOverlappingRange) {
        ChunkType chunk;
        chunk.setMin( BSON("a" << 15 << "b" << 0) );
        chunk.setMax( BSON("a" << 25 << "b" << 0) );

        string errMsg;
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().clonePlus( chunk,
                                                                            ChunkVersion( 1,
                                                                                          0,
                                                                                          OID() ),
                                                                            &errMsg ) );
        ASSERT( cloned == NULL );
        ASSERT_FALSE( errMsg.empty() );
        ASSERT_EQUALS( 2u, getCollMetadata().getNumChunks() );
    }

    TEST_F(TwoChunksWithGapCompoundKeyFixture, CloneMinusBasic) {
        ChunkType chunk;
        chunk.setMin( BSON("a" << 10 << "b" << 0) );
        chunk.setMax( BSON("a" << 20 << "b" << 0) );

        string errMsg;
        ChunkVersion version( 2, 0, OID() );
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().cloneMinus( chunk,
                                                                             version,
                                                                             &errMsg ) );

        ASSERT( errMsg.empty() );
        ASSERT_EQUALS( 2u, getCollMetadata().getNumChunks() );
        ASSERT_EQUALS( 1u, cloned->getNumChunks() );

        // TODO: test maxShardVersion, maxCollVersion

        ASSERT_FALSE( cloned->keyBelongsToMe(BSON("a" << 5 << "b" << 0)) );
        ASSERT_FALSE( cloned->keyBelongsToMe(BSON("a" << 15 << "b" << 0)) );
        ASSERT( cloned->keyBelongsToMe(BSON("a" << 30 << "b" << 0)) );
        ASSERT( cloned->keyBelongsToMe(BSON("a" << 35 << "b" << 0)) );
        ASSERT_FALSE( cloned->keyBelongsToMe(BSON("a" << 40 << "b" << 0)) );
    }

    TEST_F(TwoChunksWithGapCompoundKeyFixture, CloneMinusNonExisting) {
        ChunkType chunk;
        chunk.setMin( BSON("a" << 25 << "b" << 0) );
        chunk.setMax( BSON("a" << 28 << "b" << 0) );

        string errMsg;
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().cloneMinus( chunk,
                                                                             ChunkVersion( 1,
                                                                                           0,
                                                                                           OID() ),
                                                                             &errMsg ) );
        ASSERT( cloned == NULL );
        ASSERT_FALSE( errMsg.empty() );
        ASSERT_EQUALS( 2u, getCollMetadata().getNumChunks() );
    }

    TEST_F(TwoChunksWithGapCompoundKeyFixture, CloneSplitBasic) {
        const BSONObj min( BSON("a" << 10 << "b" << 0) );
        const BSONObj max( BSON("a" << 20 << "b" << 0) );

        ChunkType chunk;
        chunk.setMin( min );
        chunk.setMax( max );

        const BSONObj split1( BSON("a" << 15 << "b" << 0) );
        const BSONObj split2( BSON("a" << 18 << "b" << 0) );
        vector<BSONObj> splitKeys;
        splitKeys.push_back( split1 );
        splitKeys.push_back( split2 );
        ChunkVersion version( 1, 99, OID() ); // first chunk 1|99 , second 1|100

        string errMsg;
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().cloneSplit( chunk,
                                                                             splitKeys,
                                                                             version,
                                                                             &errMsg ) );

        version.incMinor(); /* second chunk 1|100, first split point */
        version.incMinor(); /* third chunk 1|101, second split point */
        ASSERT_EQUALS( cloned->getShardVersion().toLong(), version.toLong() /* 1|101 */);
        ASSERT_EQUALS( cloned->getCollVersion().toLong(), version.toLong() );
        ASSERT_EQUALS( getCollMetadata().getNumChunks(), 2u );
        ASSERT_EQUALS( cloned->getNumChunks(), 4u );
        ASSERT( cloned->keyBelongsToMe( min ) );
        ASSERT( cloned->keyBelongsToMe( split1 ) );
        ASSERT( cloned->keyBelongsToMe( split2 ) );
        ASSERT( !cloned->keyBelongsToMe( max ) );
    }

    TEST_F(TwoChunksWithGapCompoundKeyFixture, CloneSplitOutOfRangeSplitPoint) {
        ChunkType chunk;
        chunk.setMin( BSON("a" << 10 << "b" << 0) );
        chunk.setMax( BSON("a" << 20 << "b" << 0) );

        vector<BSONObj> splitKeys;
        splitKeys.push_back( BSON("a" << 5 << "b" << 0) );

        string errMsg;
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().cloneSplit( chunk,
                                                                             splitKeys,
                                                                             ChunkVersion( 1,
                                                                                           0,
                                                                                           OID() ),
                                                                             &errMsg ) );

        ASSERT( cloned == NULL );
        ASSERT_FALSE( errMsg.empty() );
        ASSERT_EQUALS( 2u, getCollMetadata().getNumChunks() );
    }

    TEST_F(TwoChunksWithGapCompoundKeyFixture, CloneSplitBadChunkRange) {
        const BSONObj min( BSON("a" << 10 << "b" << 0) );
        const BSONObj max( BSON("a" << 25 << "b" << 0) );

        ChunkType chunk;
        chunk.setMin( BSON("a" << 10 << "b" << 0) );
        chunk.setMax( BSON("a" << 25 << "b" << 0) );

        vector<BSONObj> splitKeys;
        splitKeys.push_back( BSON("a" << 15 << "b" << 0) );

        string errMsg;
        scoped_ptr<CollectionMetadata> cloned( getCollMetadata().cloneSplit( chunk,
                                                                             splitKeys,
                                                                             ChunkVersion( 1,
                                                                                           0,
                                                                                           OID() ),
                                                                             &errMsg ) );

        ASSERT( cloned == NULL );
        ASSERT_FALSE( errMsg.empty() );
        ASSERT_EQUALS( 2u, getCollMetadata().getNumChunks() );
    }

    /**
     * Fixture with chunk containing:
     * [min->10) , [10->20) , <gap> , [30->max)
     */
    class ThreeChunkWithRangeGapFixture : public mongo::unittest::Test {
    protected:
        void setUp() {
            _dummyConfig.reset( new MockRemoteDBServer( CONFIG_HOST_PORT ) );
            mongo::ConnectionString::setConnectionHook( MockConnRegistry::get()->getConnStrHook() );
            MockConnRegistry::get()->addServer( _dummyConfig.get() );

            OID epoch( OID::gen() );

            _dummyConfig->insert( CollectionType::ConfigNS, BSON(CollectionType::ns("x.y") <<
                    CollectionType::dropped(false) <<
                    CollectionType::keyPattern(BSON("a" << 1)) <<
                    CollectionType::unique(false) <<
                    CollectionType::updatedAt(1ULL) <<
                    CollectionType::epoch(epoch)) );

            {
                ChunkVersion version( 1, 1, epoch );
                _dummyConfig->insert( ChunkType::ConfigNS, BSON(ChunkType::name("x.y-a_MinKey") <<
                        ChunkType::ns("x.y") <<
                        ChunkType::min(BSON("a" << MINKEY)) <<
                        ChunkType::max(BSON("a" << 10)) <<
                        ChunkType::DEPRECATED_lastmod(version.toLong()) <<
                        ChunkType::DEPRECATED_epoch(version.epoch()) <<
                        ChunkType::shard("shard0000")) );
            }

            {
                ChunkVersion version( 1, 3, epoch );
                _dummyConfig->insert( ChunkType::ConfigNS, BSON(ChunkType::name("x.y-a_10") <<
                        ChunkType::ns("x.y") <<
                        ChunkType::min(BSON("a" << 10)) <<
                        ChunkType::max(BSON("a" << 20)) <<
                        ChunkType::DEPRECATED_lastmod(version.toLong()) <<
                        ChunkType::DEPRECATED_epoch(version.epoch()) <<
                        ChunkType::shard("shard0000")) );
            }

            {
                ChunkVersion version( 1, 2, epoch );
                _dummyConfig->insert( ChunkType::ConfigNS, BSON(ChunkType::name("x.y-a_30") <<
                        ChunkType::ns("x.y") <<
                        ChunkType::min(BSON("a" << 30)) <<
                        ChunkType::max(BSON("a" << MAXKEY)) <<
                        ChunkType::DEPRECATED_lastmod(version.toLong()) <<
                        ChunkType::DEPRECATED_epoch(version.epoch()) <<
                        ChunkType::shard("shard0000")) );
            }

            ConnectionString configLoc( CONFIG_HOST_PORT );
            MetadataLoader loader( configLoc );

            Status status = loader.makeCollectionMetadata( "test.foo",
                                                           "shard0000",
                                                           NULL,
                                                           &_metadata );
            ASSERT( status.isOK() );
        }

        void tearDown() {
            MockConnRegistry::get()->clear();
        }

        const CollectionMetadata& getCollMetadata() const {
            return _metadata;
        }

    private:
        scoped_ptr<MockRemoteDBServer> _dummyConfig;
        CollectionMetadata _metadata;
    };

    TEST_F(ThreeChunkWithRangeGapFixture, ShardOwnsDoc) {
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << 5)) );
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << 10)) );
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << 30)) );
        ASSERT( getCollMetadata().keyBelongsToMe(BSON("a" << 40)) );
    }

    TEST_F(ThreeChunkWithRangeGapFixture, ShardDoesntOwnDoc) {
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << 25)) );
        ASSERT_FALSE( getCollMetadata().keyBelongsToMe(BSON("a" << MAXKEY)) );
    }

    TEST_F(ThreeChunkWithRangeGapFixture, GetNextFromEmpty) {
        ChunkType nextChunk;
        ASSERT_FALSE( getCollMetadata().getNextChunk( BSONObj(), &nextChunk ) );
        ASSERT_EQUALS( 0, nextChunk.getMin().woCompare(BSON("a" << MINKEY)) );
        ASSERT_EQUALS( 0, nextChunk.getMax().woCompare(BSON("a" << 10)) );
    }

    TEST_F(ThreeChunkWithRangeGapFixture, GetNextFromMiddle) {
        ChunkType nextChunk;
        ASSERT_FALSE( getCollMetadata().getNextChunk(BSON("a" << 10), &nextChunk) );
        ASSERT_EQUALS( 0, nextChunk.getMin().woCompare(BSON("a" << 30)) );
        ASSERT_EQUALS( 0, nextChunk.getMax().woCompare(BSON("a" << MAXKEY)) );
    }

    TEST_F(ThreeChunkWithRangeGapFixture, GetNextFromLast) {
        ChunkType nextChunk;
        ASSERT( getCollMetadata().getNextChunk(BSON("a" << 30), &nextChunk) );
    }
} // unnamed namespace
