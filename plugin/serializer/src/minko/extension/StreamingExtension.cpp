/*
Copyright (c) 2013 Aerys

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

#include "minko/StreamingOptions.hpp"
#include "minko/StreamingTypes.hpp"
#include "minko/component/AbstractAnimation.hpp"
#include "minko/component/Animation.hpp"
#include "minko/component/MasterLodScheduler.hpp"
#include "minko/component/POPGeometryLodScheduler.hpp"
#include "minko/component/Skinning.hpp"
#include "minko/component/Surface.hpp"
#include "minko/component/TextureLodScheduler.hpp"
#include "minko/data/Provider.hpp"
#include "minko/deserialize/LodSchedulerDeserializer.hpp"
#include "minko/deserialize/TypeDeserializer.hpp"
#include "minko/extension/StreamingExtension.hpp"
#include "minko/file/AbstractSerializerParser.hpp"
#include "minko/file/AssetLibrary.hpp"
#include "minko/file/Dependency.hpp"
#include "minko/file/GeometryParser.hpp"
#include "minko/file/GeometryWriter.hpp"
#include "minko/file/LinkedAsset.hpp"
#include "minko/file/MeshPartitioner.hpp"
#include "minko/file/POPGeometryParser.hpp"
#include "minko/file/POPGeometryWriter.hpp"
#include "minko/file/Options.hpp"
#include "minko/file/PNGWriter.hpp"
#include "minko/file/POPGeometryWriterPreprocessor.hpp"
#include "minko/file/SceneParser.hpp"
#include "minko/file/SceneWriter.hpp"
#include "minko/file/StreamedTextureParser.hpp"
#include "minko/file/StreamedTextureWriter.hpp"
#include "minko/file/StreamedTextureWriterPreprocessor.hpp"
#include "minko/file/WriterOptions.hpp"
#include "minko/geometry/Bone.hpp"
#include "minko/geometry/Skin.hpp"
#include "minko/material/Material.hpp"
#include "minko/render/AbstractContext.hpp"
#include "minko/render/AbstractTexture.hpp"
#include "minko/render/IndexBuffer.hpp"
#include "minko/render/Texture.hpp"
#include "minko/render/VertexBuffer.hpp"
#include "minko/scene/Node.hpp"
#include "minko/scene/NodeSet.hpp"
#include "minko/serialize/LodSchedulerSerializer.hpp"

using namespace minko;
using namespace minko::animation;
using namespace minko::component;
using namespace minko::data;
using namespace minko::deserialize;
using namespace minko::extension;
using namespace minko::file;
using namespace minko::geometry;
using namespace minko::material;
using namespace minko::render;
using namespace minko::scene;
using namespace minko::serialize;

StreamingExtension::StreamingExtension() :
    AbstractExtension(),
    _sceneStreamingComplete(Signal<Ptr>::create()),
    _sceneStreamingProgress(Signal<Ptr, float>::create()),
    _sceneStreamingActive(Signal<Ptr>::create()),
    _sceneStreamingInactive(Signal<Ptr>::create()),
    _numActiveParsers(0u),
    _totalProgressRate(0.f)
{
}

AbstractExtension::Ptr
StreamingExtension::bind()
{
    SceneWriter::registerComponent(
        &typeid(POPGeometryLodScheduler),
        &LodSchedulerSerializer::serializePOPGeometryLodScheduler
    );

    SceneParser::registerComponent(
        POP_GEOMETRY_LOD_SCHEDULER,
        &LodSchedulerDeserializer::deserializePOPGeometryLodScheduler
    );

    SceneWriter::registerComponent(
        &typeid(TextureLodScheduler),
        &LodSchedulerSerializer::serializeTextureLodScheduler
    );

    SceneParser::registerComponent(
        TEXTURE_LOD_SCHEDULER,
        &LodSchedulerDeserializer::deserializeTextureLodScheduler
    );

    return shared_from_this();
}

void
StreamingExtension::initializeForWriting(StreamingOptions::Ptr          streamingOptions,
AbstractWriter<Node::Ptr>::Ptr writer)
{
    this->streamingOptions(streamingOptions);

    if (_streamingOptions->geometryStreamingIsActive())
    {
        file::Dependency::setGeometryFunction(std::bind(
            &StreamingExtension::serializePOPGeometry,
            std::static_pointer_cast<StreamingExtension>(shared_from_this()),
            std::placeholders::_1,
            std::placeholders::_2,
            std::placeholders::_3,
            std::placeholders::_4,
            std::placeholders::_5,
            std::placeholders::_6,
            std::placeholders::_7),
            [](Geometry::Ptr geometry) -> bool
            {
                return
                    geometry->data()->hasProperty("type") &&
                    geometry->data()->get<std::string>("type") == "pop";
            },
            11
        );

        if (writer != nullptr)
        {
            auto meshPartitionerFlags = MeshPartitioner::Options::createOneNodePerSurface;

            if (_streamingOptions->mergeSurfacesOnPartitioning())
                meshPartitionerFlags |= MeshPartitioner::Options::mergeSurfaces;

            if (_streamingOptions->useSharedClusterHierarchyOnPartitioning())
                meshPartitionerFlags |= MeshPartitioner::Options::uniformizeSize;

            if (_streamingOptions->applyCrackFreePolicyOnPartitioning())
                meshPartitionerFlags |= MeshPartitioner::Options::applyCrackFreePolicy;

            auto meshPartitionerOptions = MeshPartitioner::Options();
            meshPartitionerOptions.flags = meshPartitionerFlags;

            meshPartitionerOptions.nodeFilterFunction = [](Node::Ptr node) -> bool
            {
                auto surfaces = node->components<Surface>();

                if (surfaces.empty())
                    return false;

                for (auto surface : surfaces)
                {
                    if (surface->geometry()->data()->hasProperty("type") &&
                        surface->geometry()->data()->get<std::string>("type") == "pop")
                        return true;
                }

                return false;
            };

            writer
                ->registerPreprocessor(POPGeometryWriterPreprocessor::create())
                ->registerPreprocessor(MeshPartitioner::create(meshPartitionerOptions));
        }
    }
    

    if (_streamingOptions->textureStreamingIsActive())
    {
        file::Dependency::setTextureFunction(std::bind(
            &StreamingExtension::serializeStreamedTexture,
            std::static_pointer_cast<StreamingExtension>(shared_from_this()),
            std::placeholders::_1,
            std::placeholders::_2,
            std::placeholders::_3,
            std::placeholders::_4,
            std::placeholders::_5,
            std::placeholders::_6
        ));

        if (writer != nullptr)
        {
            auto streamedTextureWriterPreprocessor = StreamedTextureWriterPreprocessor::create();
            auto streamedTextureWriterPreprocessorOptions = StreamedTextureWriterPreprocessor::Options();

            streamedTextureWriterPreprocessorOptions.flags =
                StreamedTextureWriterPreprocessor::Options::computeVertexColor;

            streamedTextureWriterPreprocessor->options(streamedTextureWriterPreprocessorOptions);

            writer->registerPreprocessor(streamedTextureWriterPreprocessor);
        }
    }
}

void
StreamingExtension::initialize(StreamingOptions::Ptr streamingOptions)
{
    this->streamingOptions(streamingOptions);

    if (streamingOptions->geometryStreamingIsActive())
    {
        file::AbstractSerializerParser::registerAssetFunction(
            uint(minko::serialize::StreamedAssetType::STREAMED_GEOMETRY_ASSET),
            std::bind(
            &StreamingExtension::deserializePOPGeometry,
            std::static_pointer_cast<StreamingExtension>(shared_from_this()),
            std::placeholders::_1,
            std::placeholders::_2,
            std::placeholders::_3,
            std::placeholders::_4,
            std::placeholders::_5,
            std::placeholders::_6,
            std::placeholders::_7,
            std::placeholders::_8)
        );
    }

    if (streamingOptions->textureStreamingIsActive())
    {
        file::AbstractSerializerParser::registerAssetFunction(
            uint(minko::serialize::StreamedAssetType::STREAMED_TEXTURE_ASSET),
            std::bind(
                &StreamingExtension::deserializeStreamedTexture,
                std::static_pointer_cast<StreamingExtension>(shared_from_this()),
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3,
                std::placeholders::_4,
                std::placeholders::_5,
            	std::placeholders::_6,
            	std::placeholders::_7,
            	std::placeholders::_8
            )
        );
    }
}


msgpack::type::tuple<uint, short, std::string>
StreamingExtension::serializePOPGeometry(Dependency::Ptr                dependency,
                                         AssetLibrary::Ptr              assetLibrary,
                                         Geometry::Ptr                  geometry,
                                         uint                           resourceId,
                                         Options::Ptr                   options,
                                         WriterOptions::Ptr             writerOptions,
                                         std::vector<SerializedAsset>&  includeDependencies)
{
    const auto assetIsNull = writerOptions->assetIsNull(geometry->uuid());

    auto writer = POPGeometryWriter::create();

    writer->streamingOptions(_streamingOptions);

    auto assetType = STREAMED_GEOMETRY_ASSET;

    auto filename = assetLibrary->geometryName(geometry);

    filename = writerOptions->geometryUriFunction()(filename);

    writer->data(writerOptions->geometryFunction()(filename, geometry));

    auto content = std::string();

    auto hasHeader = !assetIsNull;
    auto headerSize = 0;

    auto linkedAsset = LinkedAsset::create();

    const auto linkedAssetId = dependency->registerDependency(linkedAsset);

    writer->linkedAsset(linkedAsset, linkedAssetId);

    if (!assetIsNull && writerOptions->embedMode() & WriterOptions::EmbedMode::Geometry)
    {
        content = writer->embedAll(assetLibrary, options, writerOptions, dependency);

        headerSize = content.size();

        linkedAsset
            ->linkType(LinkedAsset::LinkType::Internal);
    }
    else
    {
        hasHeader = false;

        linkedAsset
            ->filename(filename)
            ->linkType(LinkedAsset::LinkType::External);

        auto headerData = std::vector<unsigned char>();

        if (!assetIsNull)
        {
            writer->write(filename, assetLibrary, options, writerOptions, dependency, {}, headerData);

            headerSize = headerData.size();
        }

        if (hasHeader)
        {
            linkedAsset
                ->offset(headerSize);

            content = std::string(headerData.begin(), headerData.end());
        }
        else
        {
            std::stringstream contentStream;

            contentStream << static_cast<short>(linkedAssetId);

            content = contentStream.str();
        }
    }

    const auto metadata = static_cast<unsigned int>(hasHeader ? 1u << 31 : 0u) +
                          static_cast<unsigned int>((headerSize & 0x0fff) << 16) +
                          static_cast<unsigned int>(assetType);

    return SerializedAsset(metadata, resourceId, content);
}

void
StreamingExtension::deserializePOPGeometry(unsigned short					metaData,
										   AssetLibrary::Ptr				assetLibrary,
                                           Options::Ptr                     options,
										   const std::string&               completePath,
                                           const std::vector<unsigned char>& data,
										   Dependency::Ptr                  dependencies,
										   short							assetRef,
										   std::list<JobManager::Job::Ptr>&	jobList)
{
    auto hasHeader = false;
    auto streamedAssetHeaderSize = 0;
    auto streamedAssetHeaderData = std::vector<unsigned char>();
    auto linkedAsset = LinkedAsset::Ptr();

    getStreamedAssetHeader(
        metaData,
        data,
        completePath,
        dependencies,
        options,
        streamedAssetHeaderData,
        hasHeader,
        streamedAssetHeaderSize,
        linkedAsset
    );

    auto geometryData = Provider::create();

    auto parser = POPGeometryParser::create(geometryData);

    registerParser(parser);

    parser->streamingOptions(_streamingOptions);
    parser->dependency(dependencies);

    if (linkedAsset != nullptr)
    {
        parser->linkedAsset(linkedAsset);
    }

    static auto geometryId = 0u;
    static const auto extensionName = "geometry";

    // TODO serialize geometry name
    const auto defaultName = std::to_string(geometryId++) + "." + extensionName;

    const auto filenameLastSeparatorPosition = defaultName.find_last_of("/");
    const auto filenameWithExtension = defaultName.substr(
        filenameLastSeparatorPosition == std::string::npos ? 0 : filenameLastSeparatorPosition + 1
    );
    const auto filename = filenameWithExtension.substr(0, filenameWithExtension.find_last_of("."));

    auto uniqueFilename = filenameWithExtension;
    while (assetLibrary->geometry(uniqueFilename))
    {
        uniqueFilename = filename + std::to_string(geometryId++) + "." + extensionName;
    }

    parser->parse(
        uniqueFilename,
        uniqueFilename,
        options,
        streamedAssetHeaderData,
        assetLibrary
    );

    auto geometry = Geometry::Ptr();

    geometry = assetLibrary->geometry(uniqueFilename);

    dependencies->registerReference(assetRef, geometry);

    jobList.push_back(parser);

    _streamingOptions->masterLodScheduler()->registerGeometry(geometry, geometryData);
}

msgpack::type::tuple<uint, short, std::string>
StreamingExtension::serializeStreamedTexture(std::shared_ptr<file::Dependency>		dependency,
                                             std::shared_ptr<file::AssetLibrary>	assetLibrary,
											 render::AbstractTexture::Ptr			texture,
											 uint									resourceId,
											 std::shared_ptr<file::Options>			options,
                                             std::shared_ptr<file::WriterOptions>   writerOptions)
{
    const auto assetIsNull = writerOptions->assetIsNull(texture->uuid());

    auto writer = StreamedTextureWriter::create();

    auto assetType = STREAMED_TEXTURE_ASSET;

    auto filename = assetLibrary->textureName(texture);

    filename = writerOptions->textureUriFunction()(filename);

    writer->data(writerOptions->textureFunction()(filename, texture));

    auto content = std::string();

    auto hasHeader = !assetIsNull;
    auto headerSize = 0;

    auto linkedAsset = LinkedAsset::create();

    const auto linkedAssetId = dependency->registerDependency(linkedAsset);

    writer->linkedAsset(linkedAsset, linkedAssetId);

    if (!assetIsNull && writerOptions->embedMode() & WriterOptions::EmbedMode::Texture)
    {
        content = writer->embedAll(assetLibrary, options, writerOptions, dependency);

        headerSize = content.size();

        linkedAsset
            ->linkType(LinkedAsset::LinkType::Internal);
    }
    else
    {
        hasHeader = false;

        linkedAsset
            ->filename(filename)
            ->linkType(LinkedAsset::LinkType::External);

        auto headerData = std::vector<unsigned char>();

        if (!assetIsNull)
        {
            writer->write(filename, assetLibrary, options, writerOptions, dependency, {}, headerData);

            headerSize = headerData.size();
        }

        if (hasHeader)
        {
            linkedAsset
                ->offset(headerSize);

            content = std::string(headerData.begin(), headerData.end());
        }
        else
        {
            std::stringstream contentStream;

            contentStream << static_cast<short>(linkedAssetId);

            content = contentStream.str();
        }
    }

    const auto metadata = static_cast<unsigned int>(hasHeader ? 1u << 31 : 0u) +
                          static_cast<unsigned int>((headerSize & 0x0fff) << 16) +
                          static_cast<unsigned int>(assetType);

    return SerializedAsset(metadata, resourceId, content);
}

void
StreamingExtension::deserializeStreamedTexture(unsigned short											metaData,
											   std::shared_ptr<file::AssetLibrary>						assetLibrary,
                                               Options::Ptr                                             options,
											   const std::string&										assetCompletePath,
                                               const std::vector<unsigned char>&                        data,
											   std::shared_ptr<file::Dependency>						dependencies,
											   short													assetRef,
											   std::list<std::shared_ptr<component::JobManager::Job>>&	jobList)
{
    auto hasHeader = false;
    auto streamedAssetHeaderSize = 0;
    auto streamedAssetHeaderData = std::vector<unsigned char>();
    auto linkedAsset = LinkedAsset::Ptr();

    if (!getStreamedAssetHeader(
        metaData,
        data,
        assetCompletePath,
        dependencies,
        options,
        streamedAssetHeaderData,
        hasHeader,
        streamedAssetHeaderSize,
        linkedAsset
    ))
    {
        return;
    }

    auto textureData = Provider::create();

    auto parser = StreamedTextureParser::create(textureData);

    registerParser(parser);

    parser->streamingOptions(_streamingOptions);
    parser->dependency(dependencies);

    if (linkedAsset != nullptr)
    {
        parser->linkedAsset(linkedAsset);
    }

    static auto textureId = 0u;
    static const auto extensionName = "texture";

    // TODO serialize texture name
    const auto defaultName = std::to_string(textureId++) + "." + extensionName;

    const auto filenameLastSeparatorPosition = defaultName.find_last_of("/");
    const auto filenameWithExtension = defaultName.substr(
        filenameLastSeparatorPosition == std::string::npos ? 0 : filenameLastSeparatorPosition + 1
    );
    const auto filename = filenameWithExtension.substr(0, filenameWithExtension.find_last_of("."));

    auto uniqueFilename = filenameWithExtension;
    while (assetLibrary->texture(uniqueFilename))
    {
        uniqueFilename = filename + std::to_string(textureId++) + "." + extensionName;
    }

    parser->parse(
        uniqueFilename,
        uniqueFilename,
        options,
        streamedAssetHeaderData,
        assetLibrary
    );

    auto texture = AbstractTexture::Ptr();

    texture = assetLibrary->texture(uniqueFilename);

    if (texture == nullptr)
    {
        texture = assetLibrary->cubeTexture(uniqueFilename);
    }

    dependencies->registerReference(assetRef, texture);

    jobList.push_back(parser);

    _streamingOptions->masterLodScheduler()->registerTexture(texture, textureData);
}

void
StreamingExtension::registerParser(AbstractStreamedAssetParser::Ptr parser)
{
    auto parserEntryIt = _parsers.insert(std::make_pair(parser, ParserEntry()));

    auto& parserEntry = parserEntryIt.first->second;

    parserEntry.completeSlot = parser->AbstractParser::complete()->connect(
        [this](AbstractParser::Ptr parserThis) -> void
        {
            _parsers.erase(std::static_pointer_cast<AbstractStreamedAssetParser>(parserThis));

            if (_parsers.empty())
            {
                sceneStreamingComplete()->execute(std::static_pointer_cast<StreamingExtension>(shared_from_this()));
            }
        }
    );

    parserEntry.progressSlot = parser->progress()->connect(
        [this](AbstractStreamedAssetParser::Ptr parserThis, float progressRate) -> void
        {
            auto& parserEntry = _parsers.at(parserThis);

            const auto previousProgressRate = parserEntry.progressRate;

            parserEntry.progressRate = progressRate;

            _totalProgressRate += (progressRate - previousProgressRate) / _parsers.size();
        }
    );

    parserEntry.activeSlot = parser->active()->connect(
        [this](AbstractStreamedAssetParser::Ptr) -> void
        {
            if (_numActiveParsers == 0)
            {
                sceneStreamingActive()->execute(std::static_pointer_cast<StreamingExtension>(shared_from_this()));
            }

            ++_numActiveParsers;
        }
    );

    parserEntry.inactiveSlot = parser->inactive()->connect(
        [this](AbstractStreamedAssetParser::Ptr) -> void
        {
            --_numActiveParsers;

            if (_numActiveParsers == 0)
            {
                sceneStreamingInactive()->execute(std::static_pointer_cast<StreamingExtension>(shared_from_this()));
            }
        }
    );
}

bool
StreamingExtension::getStreamedAssetHeader(unsigned short                       metadata,
                                           const std::vector<unsigned char>&    data,
                                           const std::string&                   filename,
                                           Dependency::Ptr                      dependency,
                                           Options::Ptr                         options,
                                           std::vector<unsigned char>&          streamedAssetHeaderData,
                                           bool&                                hasHeader,
                                           int&                                 streamedAssetHeaderSize,
                                           LinkedAsset::Ptr&                    linkedAsset)
{
    hasHeader = (((metadata & 0xf000) >> 15) == 1 ? true : false);
    streamedAssetHeaderSize = static_cast<unsigned int>(metadata & 0x0fff);

    if (hasHeader)
    {
        streamedAssetHeaderData = data;
    }
    else
    {
        std::stringstream dataStream(std::string(data.begin(), data.end()));

        short linkedAssetId = 0;

        dataStream >> linkedAssetId;

        linkedAsset = dependency->getLinkedAssetReference(linkedAssetId);

        const auto assetHeaderSize = MINKO_SCENE_HEADER_SIZE + 2;

        auto linkedAssetResolutionSuccessful = true;

        auto linkedAssetErrorSlot = linkedAsset->error()->connect(
            [&](LinkedAsset::Ptr    linkedAssetThis,
                const Error&        error)
            {
                linkedAssetResolutionSuccessful = false;
            }
        );

        {
            auto headerOptions = options->clone()
                ->loadAsynchronously(false)
                ->seekingOffset(0)
                ->seekedLength(assetHeaderSize)
                ->storeDataIfNotParsed(false);

            auto linkedAssetCompleteSlot = linkedAsset->complete()->connect(
                [&](LinkedAsset::Ptr                    linkedAssetThis,
                    const std::vector<unsigned char>&   linkedAssetData) -> void
                {
                    const auto streamedAssetHeaderSizeOffset = assetHeaderSize - 2;

                    streamedAssetHeaderSize = assetHeaderSize +
                                              (linkedAssetData[streamedAssetHeaderSizeOffset] << 8) +
                                              linkedAssetData[streamedAssetHeaderSizeOffset + 1];
                }
            );

            linkedAsset->resolve(headerOptions);
        }

        if (!linkedAssetResolutionSuccessful)
            return false;

        {
            auto headerOptions = options->clone()
                ->loadAsynchronously(false)
                ->seekingOffset(0)
                ->seekedLength(streamedAssetHeaderSize)
                ->storeDataIfNotParsed(false);

            auto linkedAssetCompleteSlot = linkedAsset->complete()->connect(
                [&](LinkedAsset::Ptr                    linkedAssetThis,
                    const std::vector<unsigned char>&   linkedAssetData) -> void
                {
                    streamedAssetHeaderData = linkedAssetData;
                }
            );

            linkedAsset->resolve(headerOptions);
        }

        if (!linkedAssetResolutionSuccessful)
            return false;

        linkedAsset->offset(linkedAsset->offset() + streamedAssetHeaderSize + 2);
    }

    return true;
}