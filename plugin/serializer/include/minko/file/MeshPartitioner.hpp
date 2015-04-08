/*
Copyright (c) 2014 Aerys

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include "minko/Common.hpp"
#include "minko/SerializerCommon.hpp"
#include "minko/file/AbstractWriterPreprocessor.hpp"

namespace minko
{
    namespace file
    {
        class MeshPartitioner :
            public AbstractWriterPreprocessor<std::shared_ptr<scene::Node>>
        {
        public:
            typedef std::shared_ptr<MeshPartitioner> Ptr;

            typedef std::shared_ptr<scene::Node> NodePtr;

            typedef std::shared_ptr<component::Surface> SurfacePtr;

            struct SurfaceIndexer
            {
                std::function<std::size_t(SurfacePtr)>      hash;
                std::function<bool(SurfacePtr, SurfacePtr)> equal;
            };

            struct Options
            {
                static const unsigned int none = 0;

                static const unsigned int mergeSurfaces                     = 1 << 0;
                static const unsigned int createOneNodePerSurface           = 1 << 1;
                static const unsigned int uniformizeSize                    = 1 << 3;
                static const unsigned int applyCrackFreePolicy              = 1 << 4;

                static const unsigned int all                               = mergeSurfaces |
                                                                              createOneNodePerSurface |
                                                                              uniformizeSize |
                                                                              applyCrackFreePolicy;

                int                                                     maxNumTrianglesPerNode;
                int                                                     maxNumIndicesPerNode;

                unsigned int                                            flags;

                std::function<math::vec3(NodePtr)>                      partitionMaxSizeFunction;

                std::function<void(NodePtr, math::vec3&, math::vec3&)>  worldBoundsFunction;

                std::function<bool(NodePtr)>                            nodeFilterFunction;
                SurfaceIndexer                                          surfaceIndexer;

                Options();
            };

        private:
            typedef std::shared_ptr<file::AssetLibrary> AssetLibraryPtr;

        private:
            struct OctreeNode;

            typedef std::shared_ptr<OctreeNode> OctreeNodePtr;
            typedef std::weak_ptr<OctreeNode> OctreeNodeWeakPtr;

            typedef std::shared_ptr<data::HalfEdge> HalfEdgePtr;
            typedef std::shared_ptr<data::HalfEdgeCollection> HalfEdgeCollectionPtr;

            typedef std::shared_ptr<geometry::Geometry> GeometryPtr;

            struct OctreeNode
            {
                OctreeNode(int depth,
                           const math::vec3& minBound,
                           const math::vec3& maxBound,
                           OctreeNodePtr parent) :
                    depth(depth),
                    minBound(minBound),
                    maxBound(maxBound),
                    triangles(1, std::vector<unsigned int>()),
                    sharedTriangles(1, std::vector<unsigned int>()),
                    indices(1, std::set<unsigned int>()),
                    sharedIndices(1, std::set<unsigned int>()),
                    parent(parent),
                    children()
                {
                }

                int                                     depth;
                math::vec3                              minBound;
                math::vec3                              maxBound;

                std::vector<std::vector<unsigned int>>  triangles;
                std::vector<std::vector<unsigned int>>  sharedTriangles;

                std::vector<std::set<unsigned int>>     indices;
                std::vector<std::set<unsigned int>>     sharedIndices;

                OctreeNodeWeakPtr                       parent;

                std::vector<OctreeNodePtr>              children;
            };

            struct Vec3Hash : public minko::Hash<math::vec3>
            {
                inline
                std::size_t
                operator()(const math::vec3& value) const
                {
                    auto seed = std::size_t();

                    hash_combine<float, std::hash<float>>(seed, value.x);
                    hash_combine<float, std::hash<float>>(seed, value.y);
                    hash_combine<float, std::hash<float>>(seed, value.z);

                    return seed;
                }
            };

            struct PartitionInfo
            {
                NodePtr                     root;
                std::vector<SurfacePtr>     surfaces;

                bool                        useRootSpace;

                std::vector<unsigned int>   indices;
                std::vector<float>          vertices;

                math::vec3                  minBound;
                math::vec3                  maxBound;

                unsigned int                vertexSize;
                unsigned int                positionAttributeOffset;

                int                         baseDepth;

                std::vector<HalfEdgePtr>    halfEdges;

                std::unordered_set<
                    unsigned int
                >                           protectedIndices;
                std::unordered_map<
                    math::vec3,
                    std::unordered_set<unsigned int>,
                    Vec3Hash
                >                           mergedIndices;
                std::unordered_set<
                    unsigned int
                >                           markedDiscontinousIndices;

                OctreeNodePtr               rootPartitionNode;
            };

        private:
            Options             _options;

            AssetLibraryPtr     _assetLibrary;

            math::vec3          _worldMinBound;
            math::vec3          _worldMaxBound;

        public:
            ~MeshPartitioner() = default;

            inline
            static
            Ptr
            create(Options options)
            {
                auto instance = Ptr(new MeshPartitioner());

                instance->_options = options;

                return instance;
            }

            void
            process(NodePtr& node, AssetLibraryPtr assetLibrary);

        private:
            MeshPartitioner();

            static
            SurfaceIndexer
            defaultSurfaceIndexer();

            static
            bool
            defaultNodeFilterFunction(NodePtr node);

            static
            void
            defaultWorldBoundsFunction(NodePtr root, math::vec3& minBound, math::vec3& maxBound);

            static
            math::vec3
            defaultPartitionMaxSizeFunction(NodePtr root);

            OctreeNodePtr
            pickBestPartitions(OctreeNodePtr        root,
                               const math::vec3&    modelMinBound,
                               const math::vec3&    modelMaxBound,
                               PartitionInfo&       partitionInfo);

            OctreeNodePtr
            ensurePartitionSizeIsValid(OctreeNodePtr        node,
                                       const math::vec3&    maxSize,
                                       PartitionInfo&       partitionInfo);

            GeometryPtr
            createGeometry(GeometryPtr                      referenceGeometry,
                           const std::vector<unsigned int>& triangleIndices,
                           PartitionInfo&                   partitionInfo);

            void
            markProtectedVertices(GeometryPtr                                               geometry,
                                  const std::unordered_map<unsigned short, unsigned int>&   indices,
                                  PartitionInfo&                                            partitionInfo);

            std::vector<std::vector<SurfacePtr>>
            mergeSurfaces(const std::vector<SurfacePtr>& surfaces);

            bool
            buildGlobalIndex(PartitionInfo& partitionInfo);

            bool
            buildHalfEdges(PartitionInfo& partitionInfo);

            bool
            buildPartitions(PartitionInfo& partitionInfo);

            void
            registerSharedTriangle(OctreeNodePtr    partitionNode,
                                   unsigned int     triangleIndex,
                                   PartitionInfo&   partitionInfo);

            bool
            patchNode(NodePtr           node,
                      PartitionInfo&    partitionInfo);

            static
            int
            indexAt(int x, int y, int z);

            void
            insertTriangle(OctreeNodePtr        partitionNode,
                           unsigned int         triangleIndex,
                           PartitionInfo&       partitionInfo);

            void
            splitNode(OctreeNodePtr     partitionNode,
                      PartitionInfo&    partitionInfo);

            int
            countTriangles(OctreeNodePtr partitionNode);

            static
            math::vec3
            positionAt(unsigned int         index,
                       const PartitionInfo& partitionInfo);

            static
            int
            computeDepth(OctreeNodePtr partitionNode);
        };
    }
}